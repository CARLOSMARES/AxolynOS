/*
 * AxolynOS - ABI estable entre el kernel (ring 0) y las aplicaciones
 * (ring 3). Este header lo comparten:
 *
 *   - Kernel/src/syscall.cpp        (implementacion)
 *   - apis/src/            (biblioteca de usuario)
 *   - apps/native/         (aplicaciones nativas)
 *
 * REGLA DE ORO: si cambias algo aqui, cambia el numero de syscall en
 * kernel y la libreria a la vez (usa AX_SYS_* en ambos lados).
 */
#ifndef AXOS_ABI_H
#define AXOS_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char      ax_u8;
typedef unsigned short     ax_u16;
typedef unsigned int       ax_u32;
typedef unsigned long long ax_u64;
typedef signed char        ax_i8;
typedef short              ax_i16;
typedef int                ax_i32;
typedef long long          ax_i64;
typedef unsigned long      ax_size_t;

/* ------------------------------------------------------------------ */
/* Numeros de syscall (argumento 1 = RAX en el ABI de x86-64)          */
/* ------------------------------------------------------------------ */
enum {
    AX_SYS_EXIT       = 0,  /* rdi=codigo            -> nunca retorna     */
    AX_SYS_WRITE      = 1,  /* rdi=fd rsi=buf rdx=len -> bytes escritos  */
    AX_SYS_READ       = 2,  /* rdi=fd rsi=buf rdx=len -> bytes leidos    */
    AX_SYS_OPEN       = 3,  /* rdi=path rsi=flags    -> fd | -errno      */
    AX_SYS_CLOSE      = 4,  /* rdi=fd                -> 0 | -errno        */
    AX_SYS_LSEEK      = 5,  /* rdi=fd rsi=off rdx=whence -> offset        */
    AX_SYS_GETS       = 6,  /* rdi=buf rsi=max       -> longitud leida    */
    AX_SYS_SLEEP      = 7,  /* rdi=ms                                   */
    AX_SYS_TIME       = 8,  /* rdi=0 -> uptime ms                        */
    AX_SYS_GETPID     = 9,  /* -> pid                                    */
    AX_SYS_SPAWN      = 10, /* rdi=path rsi=argv rdx=argc -> pid         */
    AX_SYS_WAIT       = 11, /* rdi=pid -> exit code                      */
    AX_SYS_IPC_SEND   = 12, /* rdi=port rsi=buf rdx=len                  */
    AX_SYS_IPC_RECV   = 13, /* rdi=port rsi=buf rdx=max -> bytes         */
    AX_SYS_MODLIST    = 14, /* rdi=AxModInfo* rsi=max -> n               */
    AX_SYS_MODLOAD    = 15, /* rdi=name -> 0 | -errno                    */
    AX_SYS_MODUNLOAD  = 16, /* rdi=index                                */
    AX_SYS_VMRUN      = 17, /* rdi=path rsi=argv -> pid de la VM          */
    AX_SYS_VMSTOP     = 18, /* rdi=pid                                  */
    AX_SYS_MEMINFO    = 19, /* rdi=AxMemInfo*                           */
    AX_SYS_HEAPINFO   = 20, /* rdi=AxHeapInfo*                          */
    AX_SYS_REBOOT     = 21, /* reinicia el sistema                       */
    AX_SYS_EXITCODE   = 22, /* rdi=pid -> exit code                      */
    AX_SYS_SYSINFO    = 23, /* rdi=AxSysInfo*                           */
    AX_SYS_KILL       = 24, /* rdi=pid                                  */
    AX_SYS_YIELD      = 25, /* cede el CPU al planificador               */
    AX_SYS_THREADS    = 26, /* rdi=AxThreadInfo* rsi=max -> n           */
    AX_SYS_VMSTATUS   = 27, /* rdi=AxVmInfo*                            */
    AX_SYS_POWEROFF   = 28, /* apaga el sistema/maquina                 */
    AX_SYS_SUSPEND    = 29, /* suspende el sistema (ACPI S3)            */
    AX_SYS_HALT       = 30, /* detiene el sistema                       */
    AX_SYS_DISKREAD   = 31, /* rdi=lba rsi=buf rdx=count                */
    AX_SYS_DISKWRITE  = 32, /* rdi=lba rsi=buf rdx=count                */
    AX_SYS_SIGNAL     = 33, /* rdi=senal rsi=accion (DFL/IGN/manejador) */
    AX_SYS_SIGSEND    = 34, /* rdi=pid rsi=senal                       */
    AX_SYS_SIGPENDING = 35, /* rdi=senal (0=mascara) -> 1/0 | mascara   */
    AX_SYS_MAX        = 36
};

/* Senales (coinciden con Kernel/include/ax_signal.h). */
#ifndef AX_SIGINT
#define AX_SIGINT   2
#define AX_SIGQUIT  3
#define AX_SIGKILL  9
#define AX_SIGTERM  15
#define AX_SIGSTOP  19
#endif

#ifndef AX_SIG_DFL
#define AX_SIG_DFL  0ull
#define AX_SIG_IGN  1ull
#endif

/* Codigos de error (negativos) */
enum {
    AX_OK            = 0,
    AX_ENOENT        = -1,
    AX_EIO           = -2,
    AX_EINVAL        = -3,
    AX_ENOSPC        = -4,
    AX_EACCES        = -5,
    AX_EEXIST        = -6,
    AX_ENOMEM        = -7,
    AX_EBUSY         = -8,
    AX_EINTR         = -9,
    AX_EAGAIN        = -10,
    AX_ENOSYS        = -11,
    AX_ERANGE        = -12
};

/* Banderas para AX_SYS_OPEN */
enum {
    AX_O_RDONLY      = 0x0000,
    AX_O_WRONLY      = 0x0001,
    AX_O_RDWR        = 0x0002,
    AX_O_CREAT       = 0x0100,
    AX_O_TRUNC       = 0x0200,
    AX_O_APPEND      = 0x0400
};

/* whence para AX_SYS_LSEEK */
enum {
    AX_SEEK_SET      = 0,
    AX_SEEK_CUR      = 1,
    AX_SEEK_END      = 2
};

/* ------------------------------------------------------------------ */
/* Estructuras compartidas                                             */
/* ------------------------------------------------------------------ */
#define AX_NAME_LEN 32

typedef struct {
    ax_u64 total_phys;      /* bytes de RAM detectados            */
    ax_u64 free_phys;       /* bytes libres en el PMM             */
    ax_u64 total_pages;
    ax_u64 free_pages;
    ax_u64 reserved_pages;
    ax_u64 kernel_virt;
    ax_u64 heap_used;
    ax_u64 heap_total;
    ax_u64 low_mapped;
} AxMemInfo;

typedef struct {
    ax_u64 used;            /* bytes usados por el heap del kernel */
    ax_u64 total;           /* bytes totales del heap             */
    ax_u64 blocks;          /* bloques activos                     */
    ax_u64 free_blocks;
} AxHeapInfo;

typedef struct {
    ax_u32 index;                       /* indice del modulo         */
    ax_u32 state;                       /* 0 libre, 1 cargado        */
    char    name[AX_NAME_LEN];
    ax_u64  vaddr;                      /* direccion virtual         */
    ax_u64  size;                       /* tamano en bytes           */
    ax_u32  flags;
    ax_u32  reserved;
} AxModInfo;

typedef struct {
    ax_u64 uptime_ms;
    ax_u64 unix_ms;
    ax_u32 cpu_count;
    ax_u32 thread_count;
    char    kernel_version[16];
    char    kernel_name[24];
    ax_u64 total_phys;
    ax_u64 free_phys;
} AxSysInfo;

typedef struct {
    ax_u64 uptime_ms;
    ax_u32 pid;
    ax_u32 state;         /* 0 = detenido, 1 = ejecutando           */
    ax_u32 instructions;
    char    name[AX_NAME_LEN];
} AxVmInfo;

typedef struct {
    ax_u32 pid;
    ax_u32 state;         /* 0 free 1 ready 2 running 3 blocked 4 zombie */
    char    name[AX_NAME_LEN];
} AxThreadInfo;

typedef struct {
    ax_u64 magic;
    ax_u64 kernel_phys;
    ax_u64 kernel_virt;
    ax_u64 initrd_phys;
    ax_u64 initrd_size;
    ax_u64 mem_size;
    ax_u64 e820_entries;
    ax_u64 e820_ptr;
    ax_u64 cmdline;
} AxBootInfo;

#define AX_BOOTINFO_MAGIC 0x4158424F4F54494Eul /* "AXBOOTIN" */

#ifdef __cplusplus
}
#endif

#endif /* AXOS_ABI_H */
