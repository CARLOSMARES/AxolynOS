/*
 * AxolynOS - Gestion de procesos en user mode.
 *
 * Un "proceso" de AxolynOS es un hilo de user mode: el planificador (que es
 * un planificador de hilos) guarda la lista completa y el gestor de procesos
 * solo anade la capa de aplicacion: carga de la imagen ELF, ranura de user
 * mode, descriptores de fichero y tabla de procesos.
 *
 * MEMORIA DE UNA APLICACION
 * -------------------------
 * Cada aplicacion ocupa una ranura de 16 MiB de la region de user mode:
 *
 *   base = AX_USER_VIRT_BASE + ranura*AX_USER_SLOT_SIZE
 *
 * El mapa de identidad hace que esa direccion virtual sea tambien la
 * fisica, asi que no hay que reservar frames: la region de user mode se
 * reserva en el PMM para que el kernel no la use por su cuenta.
 *
 *   [base              ] imagen de la aplicacion (ELF)
 *   [base+15 MiB       ] cima de la pila de user mode (1 MiB)
 *
 * Documentacion: manuales/08-crear-aplicaciones.md
 */

#include "ax_types.h"
#include "ax_kernel.h"
#include "ax_axfs.h"
#include "ax_elf.h"
#include "ax_sched.h"
#include "ax_memory.h"
#include "ax_console.h"

/* ------------------------------------------------------------------ */
/* Ranuras de user mode                                                */
/* ------------------------------------------------------------------ */
typedef struct {
    bool     used;
    uint32_t slot;
    uint64_t base;
    uint64_t entry;
    uint64_t stack_top;
    char     path[128];
} ax_proc_slot;

static ax_proc_slot g_slots[AX_USER_MAX_SLOTS];

/* ------------------------------------------------------------------ */
/* Descriptores de fichero                                             */
/* ------------------------------------------------------------------ */
#define AX_PROC_MAX_FD 16

typedef struct {
    bool         used;
    uint64_t     flags;
    uint64_t     pos;
    const uint8_t* data;    /* contenido del fichero (AXFS, solo lectura) */
    uint64_t     size;
} ax_proc_fd;

static ax_proc_fd g_fds[AX_PROC_MAX_FD];

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

/* ------------------------------------------------------------------ */
/* Ranuras                                                             */
/* ------------------------------------------------------------------ */
static int ax_slot_alloc(void)
{
    for (uint32_t i = 0; i < AX_USER_MAX_SLOTS; ++i) {
        if (!g_slots[i].used) {
            g_slots[i].used = true;
            g_slots[i].slot = i;
            g_slots[i].base = AX_USER_VIRT_BASE + (uint64_t)i * AX_USER_SLOT_SIZE;
            g_slots[i].entry = 0;
            g_slots[i].stack_top = g_slots[i].base + AX_USER_STACK_OFFSET;
            g_slots[i].path[0] = '\0';
            return (int)i;
        }
    }
    return -1;
}

static void ax_slot_release(int index)
{
    if (index < 0 || index >= (int)AX_USER_MAX_SLOTS) {
        return;
    }
    g_slots[index].used = false;
    g_slots[index].entry = 0;
    g_slots[index].path[0] = '\0';
}

/* ------------------------------------------------------------------ */
/* Pila de user mode: argumentos del proceso                           */
/* ------------------------------------------------------------------ */
/*
 * Prepara la pila de user mode con el bloque de argumentos que espera
 * _start (apis/src/start.S):
 *
 *      [RSP+0]  argc
 *      [RSP+8]  argv[0] ... argv[argc-1], NULL
 *
 * Las cadenas se copian por encima del bloque (direcciones mas altas) para
 * que la pila, que crece hacia abajo, no las pise.
 *
 * Devuelve el valor que hay que poner en RSP.
 */
static uint64_t ax_proc_setup_stack(uint64_t stack_top, const char* path,
                                    const char* const* argv, int argc,
                                    uint64_t* rsp_out)
{
    uint64_t strings_size = 0;
    int      count = argc > 0 ? argc : 0;

    if (count > 64) {
        count = 64;
    }

    /* El propio path es argv[0] si el llamante no lo paso. */
    const char* names[65];
    if (count == 0) {
        names[0] = path;
        count = 1;
    } else {
        for (int i = 0; i < count; ++i) {
            names[i] = argv[i] ? argv[i] : "";
        }
    }

    for (int i = 0; i < count; ++i) {
        strings_size += ax_strlen(names[i]) + 1;
    }
    strings_size = AX_ALIGN_UP(strings_size, 16);

    /* Bloque de argumentos: 1 palabra de argc + (count+1) punteros. */
    uint64_t block = 8 + 8ull * (uint64_t)(count + 1);
    block = AX_ALIGN_UP(block, 16);

    uint64_t strings_base = stack_top - strings_size;
    uint64_t rsp = strings_base - block;

    if (rsp < AX_USER_VIRT_BASE) {
        return 0;
    }

    /* Cadenas. */
    uint64_t pos = strings_base;
    for (int i = 0; i < count; ++i) {
        uint64_t len = ax_strlen(names[i]) + 1;
        ax_memcpy((void*)(uintptr_t)pos, names[i], len);
        pos += len;
    }

    /* Bloque: argc, punteros y NULL final. */
    uint64_t* words = (uint64_t*)(uintptr_t)rsp;
    words[0] = (uint64_t)count;
    for (int i = 0; i < count; ++i) {
        words[1 + i] = strings_base + (uint64_t)i;   /* se corrige abajo */
    }
    words[1 + count] = 0;

    /* Se recalculan las direcciones de cada cadena. */
    pos = strings_base;
    for (int i = 0; i < count; ++i) {
        words[1 + i] = pos;
        pos += ax_strlen(names[i]) + 1;
    }

    *rsp_out = rsp;
    return rsp;
}

/* ------------------------------------------------------------------ */
/* API: procesos                                                       */
/* ------------------------------------------------------------------ */
int ax_proc_spawn(const char* path, const char* const* argv, int argc)
{
    if (!path || !path[0]) {
        return -AX_EINVAL;
    }

    uint64_t size = 0;
    const void* image = ax_vfs_read(path, &size);
    if (!image || size == 0) {
        return -AX_ENOENT;
    }

    int slot = ax_slot_alloc();
    if (slot < 0) {
        return -AX_ENOSPC;
    }

    uint64_t entry = 0;
    int rc = ax_elf_load_user(image, size, g_slots[slot].base, &entry);
    if (rc != 0) {
        ax_slot_release(slot);
        return rc;
    }

    uint64_t rsp = 0;
    if (ax_proc_setup_stack(g_slots[slot].stack_top, path, argv, argc, &rsp) == 0) {
        ax_slot_release(slot);
        return -AX_ENOMEM;
    }

    char name[AX_THREAD_NAME_LEN];
    ax_strlcpy(name, path, sizeof(name));

    AxThread* th = ax_thread_create_user(name, entry, rsp);
    if (!th) {
        ax_slot_release(slot);
        return -AX_ENOMEM;
    }

    g_slots[slot].entry = entry;
    ax_strlcpy(g_slots[slot].path, path, sizeof(g_slots[slot].path));

    ax_printf("[proc] '%s' launched (pid %u, ranura %u)\n",
              path, (unsigned)th->id, (unsigned)slot);

    return (int)th->id;
}

int ax_proc_wait(uint64_t pid)
{
    for (;;) {
        AxThread* t = ax_thread_by_id(pid);
        if (!t) {
            return -AX_EINVAL;
        }
        if (t->state == AX_THREAD_ZOMBIE) {
            return t->exit_code;
        }
        /* El proceso sigue vivo: se cede el CPU y se vuelve a mirar. */
        ax_thread_sleep(5);
    }
}

int ax_proc_exit_code(uint64_t pid)
{
    AxThread* t = ax_thread_by_id(pid);
    if (!t) {
        return -AX_EINVAL;
    }
    if (t->state != AX_THREAD_ZOMBIE) {
        return -AX_EAGAIN;
    }
    return t->exit_code;
}

int ax_proc_kill(uint64_t pid)
{
    AxThread* t = ax_thread_by_id(pid);
    if (!t) {
        return -AX_EINVAL;
    }
    if (t->state == AX_THREAD_ZOMBIE) {
        return 0;
    }
    t->exit_code = -AX_EINTR;
    ax_thread_wakeup(t);
    /* El hilo muere la proxima vez que se planifique: se marca como
       terminado y se le quita de la cola de listos. */
    t->state = AX_THREAD_ZOMBIE;
    return 0;
}

uint64_t ax_proc_current_pid(void)
{
    AxThread* t = ax_current_thread();
    return t ? t->id : 0;
}

int ax_proc_fill_info(AxThreadInfo* out, uint32_t max)
{
    return ax_thread_fill_info(out, max);
}

/* ------------------------------------------------------------------ */
/* API: descriptores de fichero                                        */
/* ------------------------------------------------------------------ */
int ax_proc_open(const char* path, uint64_t flags)
{
    if (!path || !path[0]) {
        return -AX_EINVAL;
    }

    uint64_t size = 0;
    const void* data = ax_vfs_read(path, &size);
    if (!data) {
        return -AX_ENOENT;
    }

    /* El AXFS es de solo lectura. */
    if (flags & (AX_O_WRONLY | AX_O_RDWR | AX_O_CREAT | AX_O_TRUNC | AX_O_APPEND)) {
        return -AX_EACCES;
    }

    for (int i = 3; i < AX_PROC_MAX_FD; ++i) {
        if (!g_fds[i].used) {
            g_fds[i].used  = true;
            g_fds[i].flags = flags;
            g_fds[i].pos   = 0;
            g_fds[i].data  = (const uint8_t*)data;
            g_fds[i].size  = size;
            return i;
        }
    }
    return -AX_ENOSPC;
}

int ax_proc_read(int fd, void* buf, uint64_t len)
{
    if (fd < 0 || fd >= AX_PROC_MAX_FD) {
        return -AX_EINVAL;
    }
    if (!buf && len > 0) {
        return -AX_EINVAL;
    }

    if (fd == 0) {
        /* Entrada estandar: se lee del puerto serie. */
        uint8_t* out = (uint8_t*)buf;
        uint64_t n = 0;
        while (n < len) {
            out[n++] = (uint8_t)ax_console_getc();
        }
        return (int)n;
    }

    if (fd == 1 || fd == 2) {
        return -AX_EINVAL;
    }

    ax_proc_fd* f = &g_fds[fd];
    if (!f->used) {
        return -AX_EINVAL;
    }
    if (f->pos >= f->size) {
        return 0;
    }

    uint64_t avail = f->size - f->pos;
    uint64_t n = (len < avail) ? len : avail;
    ax_memcpy(buf, f->data + f->pos, n);
    f->pos += n;
    return (int)n;
}

int ax_proc_write(int fd, const void* buf, uint64_t len)
{
    if (fd < 0 || fd >= AX_PROC_MAX_FD) {
        return -AX_EINVAL;
    }
    if (!buf && len > 0) {
        return -AX_EINVAL;
    }

    if (fd == 1 || fd == 2) {
        const char* s = (const char*)buf;
        for (uint64_t i = 0; i < len; ++i) {
            ax_putc(s[i]);
        }
        return (int)len;
    }

    if (fd == 0) {
        return -AX_EINVAL;
    }

    ax_proc_fd* f = &g_fds[fd];
    if (!f->used) {
        return -AX_EINVAL;
    }
    return -AX_EACCES;   /* el AXFS es de solo lectura */
}

int ax_proc_close(int fd)
{
    if (fd < 3 || fd >= AX_PROC_MAX_FD) {
        return -AX_EINVAL;
    }
    if (!g_fds[fd].used) {
        return -AX_EINVAL;
    }
    ax_memset(&g_fds[fd], 0, sizeof(g_fds[fd]));
    return 0;
}

int ax_proc_lseek(int fd, int64_t offset, int whence)
{
    if (fd < 3 || fd >= AX_PROC_MAX_FD) {
        return -AX_EINVAL;
    }
    ax_proc_fd* f = &g_fds[fd];
    if (!f->used) {
        return -AX_EINVAL;
    }

    int64_t base = 0;
    switch (whence) {
        case AX_SEEK_SET: base = 0; break;
        case AX_SEEK_CUR: base = (int64_t)f->pos; break;
        case AX_SEEK_END: base = (int64_t)f->size; break;
        default: return -AX_EINVAL;
    }

    int64_t np = base + offset;
    if (np < 0) {
        return -AX_EINVAL;
    }
    f->pos = (uint64_t)np;
    return (int)np;
}

/* ------------------------------------------------------------------ */
/* API: lectura de una linea                                           */
/* ------------------------------------------------------------------ */
int ax_proc_gets(char* buf, uint64_t max)
{
    if (!buf || max < 2) {
        return -AX_EINVAL;
    }

    uint64_t n = 0;
    for (;;) {
        char c = ax_console_getc();
        if (c == '\r' || c == '\n') {
            break;
        }
        if (c == 8 || c == 127) {       /* backspace */
            if (n > 0) {
                n--;
            }
            continue;
        }
        if (n + 1 < max) {
            buf[n++] = c;
        }
    }
    buf[n] = '\0';
    return (int)n;
}
