/*
 * AxolynOS - Anfitrion de la maquina virtual de bytecode (Axolyn).
 *
 * Una aplicacion escrita en Axolyn se compila a un fichero .axb (bytecode)
 * y se ejecuta dentro de una VM que vive en el kernel.  Cada programa corre
 * en su propio hilo del kernel, de modo que puede ceder el CPU, dormir o
 * dejar que otras aplicaciones sigan.
 *
 * El anfitrion proporciona:
 *   - la memoria de la VM (heap del kernel)
 *   - la salida por consola
 *   - los natives que el programa puede llamar (core, console, fs, proc,
 *     mem y time)
 *
 * Documentacion: manuales/10-bytecode-vm.md
 */

#include "ax_types.h"
#include "ax_kernel.h"
#include "ax_axfs.h"
#include "ax_console.h"
#include "ax_sched.h"
#include "ax_memory.h"

#include "axvm.h"
#include "axbytecode.h"

/* ------------------------------------------------------------------ */
/* Reloj para la VM (enlace debil declarado en axvm.cpp)               */
/* ------------------------------------------------------------------ */
extern "C" uint64_t axvm_host_millis(void)
{
    return ax_uptime_ms();
}

/* ------------------------------------------------------------------ */
/* Estado de las VMs                                                   */
/* ------------------------------------------------------------------ */
#define AX_VM_MAX_INSTANCES 8
#define AX_VM_PATH_LEN      128

typedef struct {
    bool     used;
    AxVM*    vm;
    uint32_t pid;
    bool     stop_requested;
    char     name[AX_THREAD_NAME_LEN];
    char     path[AX_VM_PATH_LEN];
    int      exit_code;
    bool     finished;
} ax_vm_instance;

static ax_vm_instance g_instances[AX_VM_MAX_INSTANCES];

/* Buffers rotatorios para los natives que devuelven cadenas nuevas. */
#define AX_VM_STRBUF_COUNT 8
#define AX_VM_STRBUF_SIZE  256
static char g_strbufs[AX_VM_STRBUF_COUNT][AX_VM_STRBUF_SIZE];
static uint32_t g_strbuf_next = 0;

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */
static void ax_memset(void* dst, int value, uint64_t n)
{
    uint8_t* d = (uint8_t*)dst;
    while (n--) {
        *d++ = (uint8_t)value;
    }
}

static uint64_t ax_strlen(const char* s)
{
    uint64_t n = 0;
    if (!s) {
        return 0;
    }
    while (s[n]) {
        n++;
    }
    return n;
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

static char* ax_strbuf_next(void)
{
    char* b = g_strbufs[g_strbuf_next];
    g_strbuf_next = (g_strbuf_next + 1) % AX_VM_STRBUF_COUNT;
    ax_memset(b, 0, AX_VM_STRBUF_SIZE);
    return b;
}

static void ax_print_str(const char* s)
{
    if (!s) {
        return;
    }
    for (uint64_t i = 0; s[i]; ++i) {
        ax_putc(s[i]);
    }
}

static void ax_print_i64(int64_t v)
{
    ax_putdec(v);
}

/* Convierte un entero en una cadena dentro de un buffer rotatorio. */
static char* ax_i64_to_str(int64_t v)
{
    char* buf = ax_strbuf_next();
    char   tmp[24];
    int    n = 0;

    if (v < 0) {
        /* Cuidadoso con INT64_MIN: se trabaja en unsigned. */
        uint64_t u = (uint64_t)(-(v + 1)) + 1ull;
        do {
            tmp[n++] = (char)('0' + (int)(u % 10ull));
            u /= 10ull;
        } while (u && n < 20);
        tmp[n++] = '-';
    } else {
        uint64_t u = (uint64_t)v;
        do {
            tmp[n++] = (char)('0' + (int)(u % 10ull));
            u /= 10ull;
        } while (u && n < 20);
    }

    int pos = 0;
    while (n > 0 && pos < AX_VM_STRBUF_SIZE - 1) {
        buf[pos++] = tmp[--n];
    }
    buf[pos] = '\0';
    return buf;
}

/* ------------------------------------------------------------------ */
/* Servicios de la VM                                                  */
/* ------------------------------------------------------------------ */
static void* vm_alloc(void* ctx, uint64_t size)
{
    (void)ctx;
    if (size == 0) {
        size = 1;
    }
    return ax_malloc(size);
}

static void vm_free(void* ctx, void* ptr)
{
    (void)ctx;
    ax_free(ptr);
}

static void vm_print(void* ctx, const char* s, uint64_t len)
{
    (void)ctx;
    if (!s || len == 0) {
        return;
    }
    for (uint64_t i = 0; i < len; ++i) {
        ax_putc(s[i]);
    }
}

static void vm_status(void* ctx, int status)
{
    (void)ctx;
    (void)status;
}

/* ------------------------------------------------------------------ */
/* Natives                                                             */
/* ------------------------------------------------------------------ */
static const char* vm_str(ax_val_t v)
{
    return (const char*)(uintptr_t)v;
}

static int vm_native(void* ctx, uint32_t id, ax_val_t* args, uint32_t argc, ax_val_t* ret)
{
    (void)ctx;
    if (ret) {
        *ret = 0;
    }

    switch (id) {
        /* --- core ---------------------------------------------------- */
        case AX_NT_CORE_PRINT: {
            if (argc < 1) {
                return 0;
            }
            ax_print_str(vm_str(args[0]));
            return 0;
        }
        case AX_NT_CORE_PRINTLN: {
            if (argc < 1) {
                return 0;
            }
            ax_print_str(vm_str(args[0]));
            ax_putc('\n');
            return 0;
        }
        case AX_NT_CORE_PRINT_INT: {
            if (argc < 1) {
                return 0;
            }
            ax_print_i64((int64_t)args[0]);
            return 0;
        }
        case AX_NT_CORE_STRLEN: {
            if (argc < 1) {
                return 0;
            }
            if (ret) {
                *ret = (ax_val_t)ax_strlen(vm_str(args[0]));
            }
            return 0;
        }
        case AX_NT_CORE_INT_TO_STR: {
            if (argc < 1) {
                return 0;
            }
            if (ret) {
                *ret = (ax_val_t)(uintptr_t)ax_i64_to_str((int64_t)args[0]);
            }
            return 0;
        }

        /* --- console ------------------------------------------------- */
        case AX_NT_CONSOLE_WRITE: {
            if (argc < 1) {
                return 0;
            }
            ax_print_str(vm_str(args[0]));
            return 0;
        }
        case AX_NT_CONSOLE_READLN: {
            char* buf = ax_strbuf_next();
            ax_proc_gets(buf, AX_VM_STRBUF_SIZE);
            if (ret) {
                *ret = (ax_val_t)(uintptr_t)buf;
            }
            return 0;
        }
        case AX_NT_CONSOLE_CLEAR: {
            ax_console_clear();
            return 0;
        }

        /* --- fs ------------------------------------------------------ */
        case AX_NT_FS_OPEN: {
            if (argc < 1) {
                return 0;
            }
            int fd = ax_proc_open(vm_str(args[0]), AX_O_RDONLY);
            if (ret) {
                *ret = (ax_val_t)(int64_t)fd;
            }
            return 0;
        }
        case AX_NT_FS_READ: {
            if (argc < 3) {
                return 0;
            }
            int fd = (int)(int64_t)args[0];
            void* buf = (void*)(uintptr_t)args[1];
            uint64_t len = (uint64_t)args[2];
            int n = ax_proc_read(fd, buf, len);
            if (ret) {
                *ret = (ax_val_t)(int64_t)n;
            }
            return 0;
        }
        case AX_NT_FS_WRITE: {
            if (argc < 3) {
                return 0;
            }
            int fd = (int)(int64_t)args[0];
            const void* buf = (const void*)(uintptr_t)args[1];
            uint64_t len = (uint64_t)args[2];
            int n = ax_proc_write(fd, buf, len);
            if (ret) {
                *ret = (ax_val_t)(int64_t)n;
            }
            return 0;
        }
        case AX_NT_FS_CLOSE: {
            if (argc < 1) {
                return 0;
            }
            int r = ax_proc_close((int)(int64_t)args[0]);
            if (ret) {
                *ret = (ax_val_t)(int64_t)r;
            }
            return 0;
        }
        case AX_NT_FS_LIST: {
            char* buf = ax_strbuf_next();
            ax_vfs_list(buf, AX_VM_STRBUF_SIZE);
            if (ret) {
                *ret = (ax_val_t)(uintptr_t)buf;
            }
            return 0;
        }
        case AX_NT_FS_STAT: {
            if (argc < 1) {
                return 0;
            }
            uint64_t size = 0;
            const void* data = ax_vfs_read(vm_str(args[0]), &size);
            if (ret) {
                *ret = (ax_val_t)(data ? size : 0);
            }
            return 0;
        }

        /* --- proc ---------------------------------------------------- */
        case AX_NT_PROC_EXIT: {
            int code = (argc >= 1) ? (int)(int64_t)args[0] : 0;
            ax_vm_instance* inst = (ax_vm_instance*)ctx;
            if (inst) {
                inst->exit_code = code;
                inst->stop_requested = true;
                if (inst->vm) {
                    axvm_stop(inst->vm);
                }
            }
            if (ret) {
                *ret = 0;
            }
            return 0;
        }
        case AX_NT_PROC_PID: {
            if (ret) {
                *ret = (ax_val_t)ax_proc_current_pid();
            }
            return 0;
        }
        case AX_NT_PROC_SPAWN: {
            if (argc < 1) {
                return 0;
            }
            int pid = ax_proc_spawn(vm_str(args[0]), nullptr, 0);
            if (ret) {
                *ret = (ax_val_t)(int64_t)pid;
            }
            return 0;
        }
        case AX_NT_PROC_GETS: {
            if (argc < 1) {
                return 0;
            }
            char* buf = (char*)(uintptr_t)args[0];
            uint64_t max = (argc >= 2) ? (uint64_t)args[1] : 256u;
            int n = ax_proc_gets(buf, max);
            if (ret) {
                *ret = (ax_val_t)(int64_t)n;
            }
            return 0;
        }

        /* --- mem ----------------------------------------------------- */
        case AX_NT_MEM_INFO: {
            if (ret) {
                *ret = (ax_val_t)ax_heap_total();
            }
            return 0;
        }
        case AX_NT_MEM_ALLOC: {
            if (argc < 1) {
                return 0;
            }
            ax_vm_instance* inst = (ax_vm_instance*)ctx;
            void* p = inst && inst->vm ? axvm_alloc(inst->vm, (uint64_t)args[0]) : nullptr;
            if (ret) {
                *ret = (ax_val_t)(uintptr_t)p;
            }
            return 0;
        }
        case AX_NT_MEM_FREE: {
            if (argc < 1) {
                return 0;
            }
            ax_vm_instance* inst = (ax_vm_instance*)ctx;
            if (inst && inst->vm) {
                axvm_free(inst->vm, (void*)(uintptr_t)args[0]);
            }
            return 0;
        }

        /* --- time ---------------------------------------------------- */
        case AX_NT_TIME_NOW_MS: {
            if (ret) {
                *ret = (ax_val_t)ax_uptime_ms();
            }
            return 0;
        }
        case AX_NT_TIME_SLEEP: {
            uint64_t ms = (argc >= 1) ? (uint64_t)args[0] : 0ull;
            if (ms) {
                ax_thread_sleep(ms);
            }
            return 0;
        }

        default:
            return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Hilo de ejecucion de una VM                                         */
/* ------------------------------------------------------------------ */
static void vm_thread(void* arg)
{
    ax_vm_instance* inst = (ax_vm_instance*)arg;
    if (!inst || !inst->vm) {
        return;
    }

    AxVM* vm = inst->vm;
    int   code = 0;

    for (;;) {
        AxVMStatus status = axvm_run(vm, &code);

        if (status == AX_VM_HALTED) {
            break;
        }
        if (status == AX_VM_ERROR) {
            ax_printf("[vm] '%s': %s\n", inst->name, axvm_last_error(vm));
            code = 1;
            break;
        }
        if (status == AX_VM_SLEEP) {
            uint64_t remaining = axvm_sleep_remaining(vm);
            if (remaining > 50ull) {
                remaining = 50ull;
            }
            if (remaining) {
                ax_thread_sleep(remaining);
            }
            continue;
        }
        if (status == AX_VM_YIELD) {
            ax_thread_yield();
            continue;
        }
        if (status == AX_VM_RUNNING) {
            /* La VM se detuvo a proposito: solo se continua si nadie la ha
               pedido detener. */
            if (inst->stop_requested) {
                break;
            }
            ax_thread_yield();
            continue;
        }
        break;
    }

    inst->exit_code = code;
    inst->finished  = true;
    ax_printf("[vm] '%s' finished (code %d)\n", inst->name, code);
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */
int ax_vm_run(const char* path, const char* const* argv, int argc)
{
    (void)argv;
    (void)argc;

    if (!path || !path[0]) {
        return AX_EINVAL;
    }

    uint64_t size = 0;
    const void* image = ax_vfs_read(path, &size);
    if (!image || size == 0) {
        return AX_ENOENT;
    }

    int index = -1;
    for (int i = 0; i < AX_VM_MAX_INSTANCES; ++i) {
        if (!g_instances[i].used) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        return AX_ENOSPC;
    }

    ax_vm_instance* inst = &g_instances[index];
    ax_memset(inst, 0, sizeof(*inst));
    inst->used = true;
    ax_strlcpy(inst->path, path, sizeof(inst->path));
    ax_strlcpy(inst->name, path, sizeof(inst->name));

    AxVMConfig cfg;
    ax_memset(&cfg, 0, sizeof(cfg));
    cfg.alloc       = vm_alloc;
    cfg.alloc_ctx   = nullptr;
    cfg.free        = vm_free;
    cfg.free_ctx    = nullptr;
    cfg.print       = vm_print;
    cfg.print_ctx   = nullptr;
    cfg.native      = vm_native;
    cfg.native_ctx  = inst;
    cfg.status      = vm_status;
    cfg.status_ctx  = nullptr;
    cfg.stack_size  = 64u * 1024u;      /* celdas de 8 bytes */
    cfg.heap_size   = 1024u * 1024u;     /* 1 MiB */

    AxVM* vm = nullptr;
    if (axvm_create(&cfg, &vm) != 0 || !vm) {
        inst->used = false;
        return AX_ENOMEM;
    }

    if (axvm_load(vm, (const uint8_t*)image, size) != 0) {
        ax_printf("[vm] '%s': %s\n", path, axvm_last_error(vm));
        axvm_destroy(vm);
        inst->used = false;
        return AX_EINVAL;
    }

    inst->vm = vm;

    AxThread* th = ax_thread_create_kernel(inst->name, vm_thread, inst);
    if (!th) {
        axvm_destroy(vm);
        inst->used = false;
        inst->vm = nullptr;
        return AX_ENOMEM;
    }
    inst->pid = (uint32_t)th->id;

    ax_printf("[vm] '%s' started (pid %u)\n", inst->name, (unsigned)inst->pid);
    return (int)inst->pid;
}

int ax_vm_stop(uint64_t pid)
{
    for (int i = 0; i < AX_VM_MAX_INSTANCES; ++i) {
        ax_vm_instance* inst = &g_instances[i];
        if (!inst->used || inst->pid != pid) {
            continue;
        }
        if (inst->vm) {
            axvm_stop(inst->vm);
        }
        inst->stop_requested = true;
        return 0;
    }
    return AX_EINVAL;
}

int ax_vm_fill_info(AxVmInfo* out, uint32_t max)
{
    if (!out) {
        return 0;
    }
    uint32_t n = 0;
    for (int i = 0; i < AX_VM_MAX_INSTANCES && n < max; ++i) {
        ax_vm_instance* inst = &g_instances[i];
        if (!inst->used) {
            continue;
        }
        out[n].pid   = inst->pid;
        out[n].state = inst->finished ? 0u : 1u;
        out[n].instructions = inst->vm ? (uint32_t)axvm_instructions(inst->vm) : 0u;
        ax_strlcpy(out[n].name, inst->name, sizeof(out[n].name));
        out[n].uptime_ms = ax_uptime_ms();
        n++;
    }
    return (int)n;
}
