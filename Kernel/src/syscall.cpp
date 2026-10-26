/*
 * AxolynOS - Capa de llamadas al sistema.
 *
 * Mecanismo: la aplicacion ejecuta `int 0x80` (vector AX_SYSCALL_VECTOR con
 * DPL 3).  El trampolin (Kernel/src/isr_stubs.S) apila el marco, cambia a la
 * pila del kernel y llama a ax_syscall_entry() con:
 *
 *      nr   = RAX
 *      args = { RDI, RSI, RDX, R10, R8, R9 }   (6 argumentos)
 *
 * El valor de retorno se escribe en args[AX_SYSCALL_RET_INDEX] y el
 * trampolin lo copia a RAX antes de volver a user mode.
 *
 * CONVENCION DE RETORNO (identica en todas las syscalls):
 *
 *      ret >= 0   -> exito; el valor es el resultado de la operacion.
 *      ret <  0   -> error; el valor es -errno (ver apis/include/axos/abi.h).
 *
 * Documentacion: manuales/09-syscalls.md
 */

#include "ax_types.h"
#include "ax_syscall.h"
#include "ax_syscall_entry.h"
#include "ax_kernel.h"
#include "ax_axfs.h"
#include "ax_console.h"
#include "ax_memory.h"
#include "ax_module.h"
#include "ax_ipc.h"
#include "ax_sched.h"
#include "ax_power.h"
#include "ax_disk.h"

/* ------------------------------------------------------------------ */
/* Tabla de handlers                                                   */
/* ------------------------------------------------------------------ */
/*
 * Hay dos rangos:
 *
 *   0 .. 63      syscalls del kernel (las que define AX_SYS_* en abi.h)
 *   100 .. 163   syscalls reservadas a los modulos del kernel
 *
 * Los modulos registran las suyas con ax_syscall_register().
 */
#define AX_SYSCALL_TABLE_SIZE  64
#define AX_MODULE_SYSCALL_BASE 100
#define AX_MODULE_SYSCALL_MAX  64

static ax_syscall_handler g_handlers[AX_SYSCALL_TABLE_SIZE];
static ax_syscall_handler g_module_handlers[AX_MODULE_SYSCALL_MAX];
static uint64_t           g_call_count = 0;

/* Puente con el gestor de la maquina virtual (apps de bytecode). */
static int (*g_vm_dispatch)(uint64_t nr, uint64_t* args, uint64_t* ret) = nullptr;

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */
static void ax_memcpy(void* dst, const void* src, uint64_t n)
{
    uint8_t*       d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) {
        *d++ = *s++;
    }
}

static void ax_memset(void* dst, int value, uint64_t n)
{
    uint8_t* d = (uint8_t*)dst;
    while (n--) {
        *d++ = (uint8_t)value;
    }
}

static void ax_strlcpy(char* dst, const char* src, uint64_t max)
{
    uint64_t i = 0;
    if (!dst || !max) {
        return;
    }
    if (src) {
        while (i + 1 < max && src[i]) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

/*
 * Comprueba que un puntero pertenece a la region de user mode.  Como el
 * sistema usa un unico espacio de direcciones, esta es la unica barrera
 * frente a que una aplicacion pase un puntero del kernel.
 */
static bool ax_user_ptr_ok(uint64_t ptr, uint64_t len)
{
    if (ptr < AX_USER_VIRT_BASE || ptr >= AX_USER_REGION_END) {
        return false;
    }
    if (len > AX_USER_REGION_END - ptr) {
        return false;
    }
    return true;
}

/* Copia una cadena de user mode a un buffer del kernel. */
static bool ax_user_str(uint64_t addr, char* out, uint64_t max)
{
    if (!out || max == 0) {
        return false;
    }
    if (addr < AX_USER_VIRT_BASE || addr >= AX_USER_REGION_END) {
        return false;
    }
    const char* s = (const char*)(uintptr_t)addr;
    uint64_t i = 0;
    while (i + 1 < max && s[i]) {
        out[i] = s[i];
        i++;
    }
    out[i] = '\0';
    return true;
}

/* ------------------------------------------------------------------ */
/* Handlers del kernel                                                 */
/* ------------------------------------------------------------------ */

/* AX_SYS_EXIT: termina el proceso actual.  No retorna nunca. */
static int sys_exit(uint64_t* args, uint64_t* ret)
{
    (void)ret;
    int code = (int)(int64_t)args[0];

    AxThread* cur = ax_current_thread();
    if (cur) {
        cur->exit_code = code;
    }
    ax_printf("[proc] exit %d\n", code);

    /* ax_thread_exit() no retorna: cambia al siguiente hilo. */
    ax_thread_exit(code);
    return 0;
}

static int sys_write(uint64_t* args, uint64_t* ret)
{
    int      fd  = (int)(int64_t)args[0];
    uint64_t buf = args[1];
    uint64_t len = args[2];

    if (len == 0) {
        *ret = 0;
        return 0;
    }
    if (!ax_user_ptr_ok(buf, len)) {
        return AX_EINVAL;
    }

    int n = ax_proc_write(fd, (const void*)(uintptr_t)buf, len);
    if (n < 0) {
        return n;
    }
    *ret = (uint64_t)n;
    return 0;
}

static int sys_read(uint64_t* args, uint64_t* ret)
{
    int      fd  = (int)(int64_t)args[0];
    uint64_t buf = args[1];
    uint64_t len = args[2];

    if (len == 0) {
        *ret = 0;
        return 0;
    }
    if (!ax_user_ptr_ok(buf, len)) {
        return AX_EINVAL;
    }

    int n = ax_proc_read(fd, (void*)(uintptr_t)buf, len);
    if (n < 0) {
        return n;
    }
    *ret = (uint64_t)n;
    return 0;
}

static int sys_open(uint64_t* args, uint64_t* ret)
{
    char path[256];
    if (!ax_user_str(args[0], path, sizeof(path))) {
        return AX_EINVAL;
    }
    int fd = ax_proc_open(path, args[1]);
    if (fd < 0) {
        return fd;
    }
    *ret = (uint64_t)fd;
    return 0;
}

static int sys_close(uint64_t* args, uint64_t* ret)
{
    (void)ret;
    return ax_proc_close((int)(int64_t)args[0]);
}

static int sys_lseek(uint64_t* args, uint64_t* ret)
{
    int r = ax_proc_lseek((int)(int64_t)args[0], (int64_t)args[1],
                          (int)(int64_t)args[2]);
    if (r < 0) {
        return r;
    }
    *ret = (uint64_t)r;
    return 0;
}

static int sys_gets(uint64_t* args, uint64_t* ret)
{
    uint64_t buf = args[0];
    uint64_t max = args[1];

    if (max == 0) {
        return AX_EINVAL;
    }
    if (!ax_user_ptr_ok(buf, max)) {
        return AX_EINVAL;
    }

    int n = ax_proc_gets((char*)(uintptr_t)buf, max);
    if (n < 0) {
        return n;
    }
    *ret = (uint64_t)n;
    return 0;
}

static int sys_sleep(uint64_t* args, uint64_t* ret)
{
    (void)ret;
    uint64_t ms = args[0];
    if (ms) {
        ax_thread_sleep(ms);
    }
    return 0;
}

static int sys_time(uint64_t* args, uint64_t* ret)
{
    (void)args;
    *ret = ax_uptime_ms();
    return 0;
}

static int sys_getpid(uint64_t* args, uint64_t* ret)
{
    (void)args;
    *ret = ax_proc_current_pid();
    return 0;
}

static int sys_spawn(uint64_t* args, uint64_t* ret)
{
    char path[256];
    if (!ax_user_str(args[0], path, sizeof(path))) {
        return AX_EINVAL;
    }

    uint64_t argv_addr = args[1];
    int      argc      = (int)(int64_t)args[2];

    if (argc < 0 || argc > 64) {
        return AX_EINVAL;
    }

    /* El array argv vive en user mode: se copia al kernel antes de usarlo. */
    char  names[65][128];
    const char* argv[65];

    for (int i = 0; i < argc; ++i) {
        uint64_t ptr = 0;
        if (!ax_user_ptr_ok(argv_addr + (uint64_t)i * 8, 8)) {
            return AX_EINVAL;
        }
        ax_memcpy(&ptr, (const void*)(uintptr_t)(argv_addr + (uint64_t)i * 8), 8);
        if (!ax_user_str(ptr, names[i], sizeof(names[i]))) {
            return AX_EINVAL;
        }
        argv[i] = names[i];
    }
    argv[argc] = nullptr;

    int pid = ax_proc_spawn(path, argv, argc);
    if (pid < 0) {
        return pid;
    }
    *ret = (uint64_t)pid;
    return 0;
}

static int sys_wait(uint64_t* args, uint64_t* ret)
{
    int code = ax_proc_wait(args[0]);
    if (code < 0) {
        return code;
    }
    *ret = (uint64_t)code;
    return 0;
}

static int sys_exitcode(uint64_t* args, uint64_t* ret)
{
    int code = ax_proc_exit_code(args[0]);
    if (code < 0) {
        return code;
    }
    *ret = (uint64_t)code;
    return 0;
}

static int sys_kill(uint64_t* args, uint64_t* ret)
{
    (void)ret;
    return ax_proc_kill(args[0]);
}

static int sys_yield(uint64_t* args, uint64_t* ret)
{
    (void)args;
    (void)ret;
    ax_thread_yield();
    return 0;
}

static int sys_shutdown(uint64_t* args, uint64_t* ret)
{
    (void)args;
    (void)ret;
    ax_power_shutdown();
    return 0;
}

static int sys_halt(uint64_t* args, uint64_t* ret)
{
    (void)args;
    (void)ret;
    ax_power_halt();
    return 0;
}

static int sys_suspend(uint64_t* args, uint64_t* ret)
{
    (void)args;
    (void)ret;
    ax_power_suspend();
    return 0;
}

static int sys_diskread(uint64_t* args, uint64_t* ret)
{
    uint64_t lba = args[0];
    void* buf = (void*)(uintptr_t)args[1];
    uint32_t cnt = (uint32_t)args[2];
    if (!buf || cnt == 0) return AX_EINVAL;
    int r = ax_disk_read(0, lba, cnt, buf);
    if (r < 0) return r;
    *ret = (uint64_t)(cnt * 512);
    return 0;
}

static int sys_diskwrite(uint64_t* args, uint64_t* ret)
{
    uint64_t lba = args[0];
    const void* buf = (const void*)(uintptr_t)args[1];
    uint32_t cnt = (uint32_t)args[2];
    if (!buf || cnt == 0) return AX_EINVAL;
    int r = ax_disk_write(0, lba, cnt, buf);
    if (r < 0) return r;
    *ret = (uint64_t)(cnt * 512);
    return 0;
}

static int sys_ipc_send(uint64_t* args, uint64_t* ret)
{
    int      port = (int)(int64_t)args[0];
    uint64_t buf  = args[1];
    uint64_t len  = args[2];

    if (len == 0) {
        *ret = 0;
        return 0;
    }
    if (!ax_user_ptr_ok(buf, len)) {
        return AX_EINVAL;
    }
    int n = ax_ipc_send(port, (const void*)(uintptr_t)buf, len);
    if (n < 0) {
        return n;
    }
    *ret = (uint64_t)n;
    return 0;
}

static int sys_ipc_recv(uint64_t* args, uint64_t* ret)
{
    int      port = (int)(int64_t)args[0];
    uint64_t buf  = args[1];
    uint64_t max  = args[2];

    if (max == 0) {
        *ret = 0;
        return 0;
    }
    if (!ax_user_ptr_ok(buf, max)) {
        return AX_EINVAL;
    }
    int n = ax_ipc_recv(port, (void*)(uintptr_t)buf, max);
    if (n < 0) {
        return n;
    }
    *ret = (uint64_t)n;
    return 0;
}

static int sys_modlist(uint64_t* args, uint64_t* ret)
{
    uint64_t out = args[0];
    uint64_t max = args[1];

    if (!ax_user_ptr_ok(out, max * sizeof(AxModInfo))) {
        return AX_EINVAL;
    }
    int n = ax_module_fill_info((AxModInfo*)(uintptr_t)out, (uint32_t)max);
    if (n < 0) {
        return n;
    }
    *ret = (uint64_t)n;
    return 0;
}

/* AX_SYS_THREADS: copia la tabla de hilos a user mode. */
static int sys_threads(uint64_t* args, uint64_t* ret)
{
    uint64_t out = args[0];
    uint64_t max = args[1];

    if (!out) {
        return AX_EINVAL;
    }
    if (max > AX_MAX_THREADS) {
        max = AX_MAX_THREADS;
    }
    if (max == 0) {
        *ret = 0;
        return 0;
    }
    if (!ax_user_ptr_ok(out, max * sizeof(AxThreadInfo))) {
        return AX_EINVAL;
    }

    int n = ax_thread_fill_info((AxThreadInfo*)(uintptr_t)out, (uint32_t)max);
    if (n < 0) {
        return n;
    }
    *ret = (uint64_t)n;
    return 0;
}

static int sys_modload(uint64_t* args, uint64_t* ret)
{
    char name[128];
    if (!ax_user_str(args[0], name, sizeof(name))) {
        return AX_EINVAL;
    }
    int r = ax_module_load(name);
    if (r < 0) {
        return r;
    }
    *ret = (uint64_t)r;
    return 0;
}

static int sys_modunload(uint64_t* args, uint64_t* ret)
{
    (void)ret;
    return ax_module_unload((uint32_t)args[0]);
}

static int sys_vmrun(uint64_t* args, uint64_t* ret)
{
    char path[256];
    if (!ax_user_str(args[0], path, sizeof(path))) {
        return AX_EINVAL;
    }
    int pid = ax_vm_run(path, nullptr, 0);
    if (pid < 0) {
        return pid;
    }
    *ret = (uint64_t)pid;
    return 0;
}

static int sys_vmstop(uint64_t* args, uint64_t* ret)
{
    (void)ret;
    return ax_vm_stop(args[0]);
}

static int sys_vmstatus(uint64_t* args, uint64_t* ret)
{
    uint64_t out = args[0];
    if (!ax_user_ptr_ok(out, sizeof(AxVmInfo))) {
        return AX_EINVAL;
    }
    int n = ax_vm_fill_info((AxVmInfo*)(uintptr_t)out, 1);
    if (n < 0) {
        return n;
    }
    *ret = (uint64_t)n;
    return 0;
}

static int sys_meminfo(uint64_t* args, uint64_t* ret)
{
    uint64_t out = args[0];
    if (!ax_user_ptr_ok(out, sizeof(AxMemInfo))) {
        return AX_EINVAL;
    }
    AxMemInfo* info = (AxMemInfo*)(uintptr_t)out;
    ax_memset(info, 0, sizeof(*info));
    if (ax_pmm_fill_info(info) != 0) {
        return AX_EIO;
    }
    info->low_mapped = ax_vmm_low_mapped_bytes();
    (void)ret;
    return 0;
}

static int sys_heapinfo(uint64_t* args, uint64_t* ret)
{
    uint64_t out = args[0];
    if (!ax_user_ptr_ok(out, sizeof(AxHeapInfo))) {
        return AX_EINVAL;
    }
    if (ax_heap_fill_info((AxHeapInfo*)(uintptr_t)out) != 0) {
        return AX_EIO;
    }
    (void)ret;
    return 0;
}

static int sys_sysinfo(uint64_t* args, uint64_t* ret)
{
    uint64_t out = args[0];
    if (!ax_user_ptr_ok(out, sizeof(AxSysInfo))) {
        return AX_EINVAL;
    }
    AxSysInfo* info = (AxSysInfo*)(uintptr_t)out;
    ax_memset(info, 0, sizeof(*info));
    info->uptime_ms    = ax_uptime_ms();
    info->cpu_count    = 1;
    info->thread_count = ax_thread_count();
    ax_strlcpy(info->kernel_version, AX_VERSION_STRING, sizeof(info->kernel_version));
    ax_strlcpy(info->kernel_name, AX_KERNEL_NAME, sizeof(info->kernel_name));
    info->total_phys = ax_pmm_total_bytes();
    info->free_phys  = ax_pmm_free_bytes();
    (void)ret;
    return 0;
}

static int sys_reboot(uint64_t* args, uint64_t* ret)
{
    (void)args;
    (void)ret;
    ax_reboot();
    return 0;
}

/* ------------------------------------------------------------------ */
/* Registro y despacho                                                 */
/* ------------------------------------------------------------------ */
int ax_syscall_register(uint64_t nr, ax_syscall_handler handler)
{
    if (!handler) {
        return AX_EINVAL;
    }

    if (nr < AX_SYSCALL_TABLE_SIZE) {
        if (g_handlers[nr]) {
            return AX_EBUSY;
        }
        g_handlers[nr] = handler;
        return 0;
    }
    if (nr >= AX_MODULE_SYSCALL_BASE &&
        nr < AX_MODULE_SYSCALL_BASE + AX_MODULE_SYSCALL_MAX) {
        uint64_t idx = nr - AX_MODULE_SYSCALL_BASE;
        if (g_module_handlers[idx]) {
            return AX_EBUSY;
        }
        g_module_handlers[idx] = handler;
        return 0;
    }
    return AX_EINVAL;
}

int ax_syscall_dispatch(uint64_t nr, uint64_t* args, uint64_t* ret)
{
    ax_syscall_handler handler = nullptr;

    if (nr < AX_SYSCALL_TABLE_SIZE) {
        handler = g_handlers[nr];
    } else if (nr >= AX_MODULE_SYSCALL_BASE &&
               nr < AX_MODULE_SYSCALL_BASE + AX_MODULE_SYSCALL_MAX) {
        handler = g_module_handlers[nr - AX_MODULE_SYSCALL_BASE];
    }

    if (!handler && g_vm_dispatch) {
        /* Las syscalls que el gestor de la VM no cubre pasan al anfitrion
           de la maquina virtual (natives de Axolyn). */
        return g_vm_dispatch(nr, args, ret);
    }

    if (!handler) {
        return AX_ENOSYS;
    }
    return handler(args, ret);
}

void ax_syscall_set_vm_dispatch(int (*fn)(uint64_t nr, uint64_t* args, uint64_t* ret))
{
    g_vm_dispatch = fn;
}

uint64_t ax_syscall_count(void)
{
    return g_call_count;
}

void ax_syscall_init(void)
{
    for (int i = 0; i < AX_SYSCALL_TABLE_SIZE; ++i) {
        g_handlers[i] = nullptr;
    }
    for (int i = 0; i < AX_MODULE_SYSCALL_MAX; ++i) {
        g_module_handlers[i] = nullptr;
    }
    g_call_count = 0;

    /* --- Syscalls del kernel ---------------------------------------- */
    ax_syscall_register(AX_SYS_EXIT,      sys_exit);
    ax_syscall_register(AX_SYS_WRITE,     sys_write);
    ax_syscall_register(AX_SYS_READ,      sys_read);
    ax_syscall_register(AX_SYS_OPEN,      sys_open);
    ax_syscall_register(AX_SYS_CLOSE,     sys_close);
    ax_syscall_register(AX_SYS_LSEEK,     sys_lseek);
    ax_syscall_register(AX_SYS_GETS,      sys_gets);
    ax_syscall_register(AX_SYS_SLEEP,     sys_sleep);
    ax_syscall_register(AX_SYS_TIME,      sys_time);
    ax_syscall_register(AX_SYS_GETPID,    sys_getpid);
    ax_syscall_register(AX_SYS_SPAWN,     sys_spawn);
    ax_syscall_register(AX_SYS_WAIT,      sys_wait);
    ax_syscall_register(AX_SYS_IPC_SEND,  sys_ipc_send);
    ax_syscall_register(AX_SYS_IPC_RECV,  sys_ipc_recv);
    ax_syscall_register(AX_SYS_MODLIST,   sys_modlist);
    ax_syscall_register(AX_SYS_THREADS,   sys_threads);
    ax_syscall_register(AX_SYS_MODLOAD,   sys_modload);
    ax_syscall_register(AX_SYS_MODUNLOAD, sys_modunload);
    ax_syscall_register(AX_SYS_VMRUN,     sys_vmrun);
    ax_syscall_register(AX_SYS_VMSTOP,    sys_vmstop);
    ax_syscall_register(AX_SYS_MEMINFO,   sys_meminfo);
    ax_syscall_register(AX_SYS_HEAPINFO,  sys_heapinfo);
    ax_syscall_register(AX_SYS_REBOOT,    sys_reboot);
    ax_syscall_register(AX_SYS_EXITCODE,  sys_exitcode);
    ax_syscall_register(AX_SYS_SYSINFO,   sys_sysinfo);
    ax_syscall_register(AX_SYS_KILL,      sys_kill);
    ax_syscall_register(AX_SYS_YIELD,     sys_yield);
    ax_syscall_register(AX_SYS_REBOOT,    sys_reboot);
    ax_syscall_register(AX_SYS_VMSTATUS,  sys_vmstatus);
    ax_syscall_register(AX_SYS_POWEROFF,  sys_shutdown);
    ax_syscall_register(AX_SYS_SUSPEND,   sys_suspend);
    ax_syscall_register(AX_SYS_HALT,      sys_halt);
    ax_syscall_register(AX_SYS_DISKREAD,  sys_diskread);
    ax_syscall_register(AX_SYS_DISKWRITE, sys_diskwrite);
}

/* ------------------------------------------------------------------ */
/* Punto de entrada (la invoca el trampolin de isr_stubs.S)           */
/* ------------------------------------------------------------------ */
extern "C" uint64_t ax_syscall_entry(uint64_t nr, uint64_t* args)
{
    uint64_t ret = 0;

    if (!args) {
        return (uint64_t)(int64_t)AX_EINVAL;
    }

    g_call_count++;

    int rc = ax_syscall_dispatch(nr, args, &ret);
    if (rc != 0) {
        /* Los errores se devuelven como -errno en RAX. */
        ret = (uint64_t)(int64_t)rc;
    }

    /* Se escribe tambien en el canal de retorno del trampolin, por
       compatibilidad con las dos convenciones. */
    args[AX_SYSCALL_RET_INDEX] = ret;
    return ret;
}
