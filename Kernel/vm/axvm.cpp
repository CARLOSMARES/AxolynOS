/*
 * AxolynOS - Maquina virtual de bytecode "AXB" (Axolyn Bytecode).
 *
 * Este mismo fuente se compila en dos entornos:
 *   - dentro del kernel (freestanding: sin STL, sin excepciones, sin RTTI,
 *     sin libc; memoria propia mediante los callbacks del anfitrion)
 *   - en el host (g++ normal) para el runner de pruebas compiladores/axc/axrun
 *
 * Por eso no se usa nada de la biblioteca estandar: hay una arena propia con
 * bump pointer y lista de libres, y un formateador de textos propio.
 *
 * Modelo de memoria:
 *   - "pila" : celdas de 8 bytes (ax_val_t) para argumentos, locales y
 *              temporales. Las variables locales se direccionan con
 *              AX_OP_LOCAL_ADDR.
 *   - "heap" : arena con bump pointer para globales, cadenas constantes y
 *              los bloques de NEW / ARRAY_NEW / STRUCT_NEW.
 *
 * Un puntero valido es o una direccion del heap o una direccion de la pila;
 * cualquier otra direccion produce un error controlado (nunca un crash).
 *
 * Documentacion: manuales/10-bytecode-vm.md
 */
#include "axvm.h"
#include "axbytecode.h"

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ */
/* Constantes internas                                                */
/* ------------------------------------------------------------------ */
#define AX_VM_MAX_FRAMES        256u
#define AX_VM_MAX_NATIVE_ARGS   16u
#define AX_VM_ERR_LEN           256u
#define AX_VM_FALLBACK_SIZE     (256u * 1024u)
#define AX_VM_DEFAULT_STACK     (64u * 1024u)    /* celdas de 8 bytes  */
#define AX_VM_DEFAULT_HEAP      (1024u * 1024u)  /* bytes              */
#define AX_VM_HEAP_HDR          32u
#define AX_VM_HEAP_MAGIC        0xA81F00D5EED0A11Bull
#define AX_VM_STR_SCAN_MAX      (1024u * 1024u)  /* limite de lectura   */

/* ------------------------------------------------------------------ */
/* Tabla de namespaces nativos (debe coincidir con el enum AX_NT_*)   */
/* ------------------------------------------------------------------ */
const AxNativeNs ax_native_namespaces[] = {
    { "core",    AX_NT_CORE_PRINT,      5u },
    { "console", AX_NT_CONSOLE_WRITE,   3u },
    { "fs",      AX_NT_FS_OPEN,         6u },
    { "proc",    AX_NT_PROC_EXIT,       4u },
    { "mem",     AX_NT_MEM_INFO,        3u },
    { "time",    AX_NT_TIME_NOW_MS,     2u }
};
const uint32_t ax_native_ns_count = 6u;

/* ------------------------------------------------------------------ */
/* Utilidades de memoria propias (sin libc)                           */
/* ------------------------------------------------------------------ */
static void* axMemSet(void* dst, int value, uint64_t n) {
    uint8_t* d = (uint8_t*)dst;
    uint64_t i;
    for (i = 0; i < n; i++) d[i] = (uint8_t)value;
    return dst;
}

static void* axMemCpy(void* dst, const void* src, uint64_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    uint64_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

/* Longitud de una cadena, con limite de seguridad. */
static uint64_t axStrLenBounded(const char* s, uint64_t max) {
    uint64_t n = 0;
    if (!s) return 0;
    while (n < max && s[n] != '\0') n++;
    return n;
}

static uint64_t axStrLen(const char* s) {
    return axStrLenBounded(s, AX_VM_STR_SCAN_MAX);
}

/* ------------------------------------------------------------------ */
/* Formateador de textos propio (sustituye a snprintf)                */
/* ------------------------------------------------------------------ */
static void axFmtStr(char* out, uint64_t cap, uint64_t* pos, const char* s) {
    if (!s) s = "(null)";
    while (*s != '\0' && *pos + 1 < cap) out[(*pos)++] = *s++;
}

static void axFmtU64(char* out, uint64_t cap, uint64_t* pos, uint64_t v) {
    char tmp[24];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0 && n < 24) { tmp[n++] = (char)('0' + (int)(v % 10u)); v /= 10u; }
    while (n > 0 && *pos + 1 < cap) out[(*pos)++] = tmp[--n];
}

static void axFmtI64(char* out, uint64_t cap, uint64_t* pos, int64_t v) {
    uint64_t m;
    if (v < 0) {
        if (*pos + 1 < cap) out[(*pos)++] = '-';
        m = (uint64_t)(-(v + 1)) + 1u;
    } else {
        m = (uint64_t)v;
    }
    axFmtU64(out, cap, pos, m);
}

static void axFmtHex(char* out, uint64_t cap, uint64_t* pos, uint64_t v, int upper) {
    static const char digitsL[] = "0123456789abcdef";
    static const char digitsU[] = "0123456789ABCDEF";
    const char* digits = upper ? digitsU : digitsL;
    char tmp[24];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0 && n < 24) { tmp[n++] = digits[v & 0xFu]; v >>= 4; }
    while (n > 0 && *pos + 1 < cap) out[(*pos)++] = tmp[--n];
}

/* Formato minimo soportado: %s %c %d %i %u %x %X %p %% con modifiers l/ll/z */
static void axVFormat(char* out, uint64_t cap, const char* fmt, va_list ap) {
    uint64_t pos = 0;
    if (cap == 0) return;
    if (!fmt) { out[0] = '\0'; return; }
    while (*fmt != '\0' && pos + 1 < cap) {
        if (*fmt != '%') { out[pos++] = *fmt++; continue; }
        fmt++;
        if (*fmt == '\0') break;
        int lng = 0;
        while (*fmt == 'l' || *fmt == 'z' || *fmt == 'h') {
            if (*fmt == 'l') lng++;
            fmt++;
        }
        switch (*fmt) {
            case 's': {
                const char* s = va_arg(ap, const char*);
                axFmtStr(out, cap, &pos, s);
                break;
            }
            case 'c': {
                int c = va_arg(ap, int);
                if (pos + 1 < cap) out[pos++] = (char)c;
                break;
            }
            case 'd':
            case 'i': {
                int64_t v = (lng >= 2) ? va_arg(ap, long long) : (int64_t)va_arg(ap, int);
                axFmtI64(out, cap, &pos, v);
                break;
            }
            case 'u': {
                uint64_t v = (lng >= 2) ? va_arg(ap, unsigned long long) : (uint64_t)va_arg(ap, unsigned int);
                axFmtU64(out, cap, &pos, v);
                break;
            }
            case 'x':
            case 'X': {
                uint64_t v = (lng >= 2) ? va_arg(ap, unsigned long long) : (uint64_t)va_arg(ap, unsigned int);
                axFmtHex(out, cap, &pos, v, *fmt == 'X');
                break;
            }
            case 'p': {
                uint64_t v = (uint64_t)(uintptr_t)va_arg(ap, void*);
                axFmtHex(out, cap, &pos, v, 0);
                break;
            }
            case '%':
                if (pos + 1 < cap) out[pos++] = '%';
                break;
            default:
                if (pos + 1 < cap) out[pos++] = '%';
                if (pos + 1 < cap) out[pos++] = *fmt;
                break;
        }
        fmt++;
    }
    out[pos] = '\0';
}

static void axFormat(char* out, uint64_t cap, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    axVFormat(out, cap, fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------------ */
/* Estructuras internas                                               */
/* ------------------------------------------------------------------ */

/* Cabecera de cada bloque del heap de la VM. */
typedef struct AxHeapBlock {
    uint64_t size;    /* bytes utiles del bloque (sin cabecera)      */
    uint64_t magic;   /* AX_VM_HEAP_MAGIC si el bloque es valido      */
    uint32_t flags;   /* bit0: bloque liberable por AX_OP_FREE        */
    uint32_t pad;
    struct AxHeapBlock* next; /* siguiente bloque de la lista de libres */
} AxHeapBlock;

typedef struct {
    char*    name;
    uint32_t addr;
    uint32_t argc;
    uint32_t nlocals;
    uint32_t stackSize;
} AxVmFunc;

typedef struct {
    char*    name;
    uint32_t slot;
    uint32_t size;
    uint32_t flags;
} AxVmGlobal;

typedef struct {
    char*    name;
    uint32_t id;
} AxVmNative;

typedef struct {
    char*    ns;
    uint32_t nativeBase;
    uint32_t count;
} AxVmImport;

typedef struct {
    uint32_t funcIdx;    /* funcion en ejecucion                        */
    uint32_t retPc;      /* direccion de retorno en el codigo           */
    uint32_t localsBase; /* celda base de los locales de la funcion     */
    uint32_t prevSp;     /* pila a restaurar al retornar                */
} AxVmFrame;

struct AxVM {
    AxVMConfig cfg;

    /* --- pila de celdas --- */
    ax_val_t* stack;
    uint64_t  stackCap;   /* celdas          */
    uint64_t  stackBytes;
    uint64_t  sp;

    /* --- heap de programa --- */
    uint8_t*  heap;
    uint64_t  heapSize;
    uint64_t  heapUsed;
    AxHeapBlock* heapFreeList;

    /* --- imagen cargada --- */
    uint8_t*  code;
    uint64_t  codeSize;
    uint64_t  pc;
    ax_val_t* consts;
    uint32_t  constCount;
    uint32_t* strOff;
    uint32_t  strCount;
    uint8_t*  strData;
    uint32_t  strDataSize;
    int       strDataInHeap;   /* el blob vive en el heap de la VM */

    AxVmFunc*    funcs;    uint32_t funcCount;
    AxVmGlobal*  globals;  uint32_t globalCount;
    ax_val_t*    globalCells; uint32_t globalSlots;
    AxVmNative*  natives;  uint32_t nativeCount;
    AxVmImport*  imports;  uint32_t importCount;

    uint32_t entryFunc;
    uint32_t mainFunc;

    /* --- pila de llamadas --- */
    AxVmFrame* frames;
    uint32_t   frameCount;
    uint32_t   frameCap;

    /* --- estado --- */
    AxVMStatus status;
    int        exitCode;
    int        loaded;
    int        started;
    int        stopRequested;
    int        fatal;          /* error de lectura del codigo */
    uint64_t   instructions;
    uint64_t   sleepUntil;     /* instante de despertar (ms) */
    uint64_t   sleepRemaining; /* ms que faltan               */
    char       err[AX_VM_ERR_LEN];
};

/* ------------------------------------------------------------------ */
/* Reloj: lo proporciona el anfitrion (kernel o runner de host)       */
/* ------------------------------------------------------------------ */
#if defined(__GNUC__) || defined(__clang__)
extern "C" uint64_t axvm_host_millis(void) __attribute__((weak));
static uint64_t axvmNowMs(void) {
    if (axvm_host_millis != 0) return axvm_host_millis();
    return 0u;
}
#else
extern "C" uint64_t axvm_host_millis(void);
static uint64_t axvmNowMs(void) { return axvm_host_millis(); }
#endif

/* ------------------------------------------------------------------ */
/* Asignacion de memoria de la VM                                     */
/* ------------------------------------------------------------------ */
static uint8_t g_axFallback[AX_VM_FALLBACK_SIZE];
static uint64_t g_axFallbackUsed = 0;

static void* axVmRawAlloc(const AxVMConfig* cfg, uint64_t size) {
    if (cfg && cfg->alloc) return cfg->alloc(cfg->alloc_ctx, size);
    size = (size + 7u) & ~7ull;
    if (size == 0) size = 8u;
    if (g_axFallbackUsed + size > (uint64_t)AX_VM_FALLBACK_SIZE) return 0;
    void* p = &g_axFallback[g_axFallbackUsed];
    g_axFallbackUsed += size;
    return p;
}

static void axVmRawFree(const AxVMConfig* cfg, void* p) {
    if (!p) return;
    if (cfg && cfg->free) cfg->free(cfg->free_ctx, p);
    /* sin liberacion disponible: la arena de reserva no se recicla */
}

static char* axVmStrDup(const AxVMConfig* cfg, const char* s) {
    uint64_t n = axStrLen(s) + 1u;
    char* d = (char*)axVmRawAlloc(cfg, n);
    if (!d) return 0;
    axMemCpy(d, s, n);
    return d;
}

/* ------------------------------------------------------------------ */
/* Heap de programa (arena bump + lista de libres)                    */
/* ------------------------------------------------------------------ */
static void* axHeapAlloc(AxVM* vm, uint64_t size, int freeable) {
    AxHeapBlock* b;
    AxHeapBlock** pp;

    if (size == 0) size = 8u;
    size = (size + 7u) & ~7ull;

    /* Primero la lista de libres (primer encaje). */
    pp = &vm->heapFreeList;
    while (*pp != 0) {
        AxHeapBlock* cand = *pp;
        if (cand->size >= size) {
            *pp = cand->next;
            cand->next = 0;
            cand->magic = AX_VM_HEAP_MAGIC;
            cand->flags = freeable ? 1u : 0u;
            return (uint8_t*)cand + AX_VM_HEAP_HDR;
        }
        pp = &cand->next;
    }

    /* Si no cabe, bump pointer. */
    if (vm->heapUsed + AX_VM_HEAP_HDR + size > vm->heapSize) return 0;
    b = (AxHeapBlock*)(vm->heap + vm->heapUsed);
    b->size = size;
    b->magic = AX_VM_HEAP_MAGIC;
    b->flags = freeable ? 1u : 0u;
    b->next = 0;
    vm->heapUsed += AX_VM_HEAP_HDR + size;
    return (uint8_t*)b + AX_VM_HEAP_HDR;
}

static void axHeapFree(AxVM* vm, void* ptr) {
    uint8_t* p = (uint8_t*)ptr;
    AxHeapBlock* b;
    if (!ptr || !vm->heap) return;
    if (p < vm->heap + AX_VM_HEAP_HDR || p >= vm->heap + vm->heapUsed) return;
    b = (AxHeapBlock*)(p - AX_VM_HEAP_HDR);
    if (b->magic != AX_VM_HEAP_MAGIC) return;
    if (!(b->flags & 1u)) return;   /* bloques internos: no liberables */
    b->next = vm->heapFreeList;
    vm->heapFreeList = b;
}

/* Devuelve el tamano en bytes de un bloque del heap, o 0 si no lo es. */
static uint64_t axHeapBlockSize(AxVM* vm, const void* ptr) {
    const uint8_t* p = (const uint8_t*)ptr;
    const AxHeapBlock* b;
    if (!ptr || !vm->heap) return 0;
    if (p < vm->heap + AX_VM_HEAP_HDR || p >= vm->heap + vm->heapUsed) return 0;
    b = (const AxHeapBlock*)(p - AX_VM_HEAP_HDR);
    if (b->magic != AX_VM_HEAP_MAGIC) return 0;
    return b->size;
}

/* ------------------------------------------------------------------ */
/* API publica de reserva de memoria del programa                      */
/* ------------------------------------------------------------------ */
/*
 * El anfitrion (el kernel) puede reservar memoria en el heap del programa
 * para el nativo "mem.alloc".  Las direcciones devueltas son direcciones
 * validas dentro de la maquina virtual: el programa puede leerlas y
 * escribirlas con DEREF/STORE como cualquier otra celda del heap.
 */
void* axvm_alloc(AxVM* vm, uint64_t size) {
    if (!vm) return 0;
    return axHeapAlloc(vm, size, 1);
}

void axvm_free(AxVM* vm, void* ptr) {
    if (!vm) return;
    axHeapFree(vm, ptr);
}

/* ------------------------------------------------------------------ */
/* Acceso a memoria (heap o pila)                                     */
/* ------------------------------------------------------------------ */static int axAddrInHeap(const AxVM* vm, uint64_t addr) {
    return vm->heap && addr >= (uint64_t)(uintptr_t)vm->heap &&
           addr + 8u <= (uint64_t)(uintptr_t)vm->heap + vm->heapSize;
}

static int axAddrInStack(const AxVM* vm, uint64_t addr) {
    return vm->stack && addr >= (uint64_t)(uintptr_t)vm->stack &&
           addr + 8u <= (uint64_t)(uintptr_t)vm->stack + vm->stackBytes;
}

static int axMemRead(AxVM* vm, uint64_t addr, ax_val_t* out) {
    if (axAddrInHeap(vm, addr)) {
        axMemCpy(out, (const void*)(uintptr_t)addr, 8u);
        return 1;
    }
    if (axAddrInStack(vm, addr)) {
        uint64_t idx = (addr - (uint64_t)(uintptr_t)vm->stack) / 8u;
        *out = vm->stack[idx];
        return 1;
    }
    return 0;
}

static int axMemWrite(AxVM* vm, uint64_t addr, ax_val_t value) {
    if (axAddrInHeap(vm, addr)) {
        axMemCpy((void*)(uintptr_t)addr, &value, 8u);
        return 1;
    }
    if (axAddrInStack(vm, addr)) {
        uint64_t idx = (addr - (uint64_t)(uintptr_t)vm->stack) / 8u;
        vm->stack[idx] = value;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Errores                                                            */
/* ------------------------------------------------------------------ */
static void axVmError(AxVM* vm, const char* fmt, ...) {
    va_list ap;
    if (!vm) return;
    va_start(ap, fmt);
    axVFormat(vm->err, AX_VM_ERR_LEN, fmt, ap);
    va_end(ap);
    vm->status = AX_VM_ERROR;
    vm->exitCode = 1;
}

/* ------------------------------------------------------------------ */
/* Pila de valores                                                    */
/* ------------------------------------------------------------------ */
static int axPush(AxVM* vm, ax_val_t v) {
    if (vm->sp >= vm->stackCap) {
        axVmError(vm, "desbordamiento de la pila de la VM");
        return 0;
    }
    vm->stack[vm->sp++] = v;
    return 1;
}

static int axPop(AxVM* vm, ax_val_t* out) {
    if (vm->sp == 0) {
        axVmError(vm, "pila vacia");
        return 0;
    }
    *out = vm->stack[--vm->sp];
    return 1;
}

static int axPeek(AxVM* vm, ax_val_t* out) {
    if (vm->sp == 0) {
        axVmError(vm, "pila vacia");
        return 0;
    }
    *out = vm->stack[vm->sp - 1u];
    return 1;
}

/* ------------------------------------------------------------------ */
/* Creacion y destruccion                                             */
/* ------------------------------------------------------------------ */
int axvm_create(const AxVMConfig* cfg, AxVM** out) {
    AxVM* vm;
    uint64_t stackCells;
    uint64_t heapBytes;

    if (!out) return -1;
    *out = 0;
    if (!cfg) return -2;

    vm = (AxVM*)axVmRawAlloc(cfg, (uint64_t)sizeof(AxVM));
    if (!vm) return -3;
    axMemSet(vm, 0, (uint64_t)sizeof(AxVM));
    vm->cfg = *cfg;

    stackCells = cfg->stack_size ? cfg->stack_size : (uint64_t)AX_VM_DEFAULT_STACK;
    heapBytes  = cfg->heap_size  ? cfg->heap_size  : (uint64_t)AX_VM_DEFAULT_HEAP;

    vm->stackCap   = stackCells;
    vm->stackBytes = stackCells * 8u;
    vm->stack = (ax_val_t*)axVmRawAlloc(cfg, vm->stackBytes);
    if (!vm->stack) { axVmRawFree(cfg, vm); return -4; }
    axMemSet(vm->stack, 0, vm->stackBytes);

    vm->heapSize = heapBytes;
    vm->heap = (uint8_t*)axVmRawAlloc(cfg, heapBytes);
    if (!vm->heap) { axVmRawFree(cfg, vm->stack); axVmRawFree(cfg, vm); return -5; }
    axMemSet(vm->heap, 0, heapBytes);

    vm->frameCap = AX_VM_MAX_FRAMES;
    vm->frames = (AxVmFrame*)axVmRawAlloc(cfg, (uint64_t)sizeof(AxVmFrame) * vm->frameCap);
    if (!vm->frames) {
        axVmRawFree(cfg, vm->heap);
        axVmRawFree(cfg, vm->stack);
        axVmRawFree(cfg, vm);
        return -6;
    }
    axMemSet(vm->frames, 0, (uint64_t)sizeof(AxVmFrame) * vm->frameCap);

    vm->entryFunc = 0xFFFFFFFFu;
    vm->mainFunc  = 0xFFFFFFFFu;
    vm->status    = AX_VM_RUNNING;
    vm->exitCode  = 0;
    vm->err[0]    = '\0';

    *out = vm;
    return 0;
}

void axvm_destroy(AxVM* vm) {
    if (!vm) return;
    if (vm->funcs)    axVmRawFree(&vm->cfg, vm->funcs);
    if (vm->globals)  axVmRawFree(&vm->cfg, vm->globals);
    if (vm->natives)  axVmRawFree(&vm->cfg, vm->natives);
    if (vm->imports)  axVmRawFree(&vm->cfg, vm->imports);
    if (vm->code)     axVmRawFree(&vm->cfg, vm->code);
    if (vm->consts)   axVmRawFree(&vm->cfg, vm->consts);
    if (vm->strOff)   axVmRawFree(&vm->cfg, vm->strOff);
    if (vm->strData && !vm->strDataInHeap) axVmRawFree(&vm->cfg, vm->strData);
    if (vm->frames)   axVmRawFree(&vm->cfg, vm->frames);
    if (vm->heap)     axVmRawFree(&vm->cfg, vm->heap);
    if (vm->stack)    axVmRawFree(&vm->cfg, vm->stack);
    axVmRawFree(&vm->cfg, vm);
}

/* ------------------------------------------------------------------ */
/* Lectura de la imagen .axb                                          */
/* ------------------------------------------------------------------ */

/* Devuelve el nombre de un native registrado, o "?" si no se conoce. */
static const char* axVmNativeName(const AxVM* vm, uint32_t id) {
    uint32_t i;
    for (i = 0; i < vm->nativeCount; i++) {
        if (vm->natives[i].id == id) return vm->natives[i].name;
    }
    return "?";
}

static uint32_t axReadU32Mem(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t axReadU64Mem(const uint8_t* p) {
    return (uint64_t)axReadU32Mem(p) | ((uint64_t)axReadU32Mem(p + 4) << 32);
}

int axvm_load(AxVM* vm, const uint8_t* image, uint64_t size) {
    const AxbHeader* hdr;
    uint64_t off;
    uint64_t need;
    uint32_t i;

    if (!vm) return -1;
    if (!image || size < (uint64_t)sizeof(AxbHeader)) {
        axVmError(vm, "imagen AXB demasiado corta o nula");
        return -1;
    }

    hdr = (const AxbHeader*)(const void*)image;

    {
        int magicOk = 1;
        for (i = 0; i < (uint32_t)AXB_MAGIC_LEN; i++) {
            if (hdr->magic[i] != AXB_MAGIC[i]) { magicOk = 0; break; }
        }
        if (!magicOk) {
            axVmError(vm, "firma AXB invalida (se esperaba '%s')", AXB_MAGIC);
            return -2;
        }
    }
    if (hdr->version != AXB_VERSION) {
        axVmError(vm, "version AXB %u no soportada (esperada %u)",
                  (unsigned)hdr->version, (unsigned)AXB_VERSION);
        return -3;
    }

    /* Tamano minimo que debe tener la imagen. */
    need = (uint64_t)sizeof(AxbHeader);
    need += (uint64_t)hdr->code_size;
    need += (uint64_t)hdr->const_count * 8u;
    need += (uint64_t)hdr->str_count * 4u;
    need += (uint64_t)hdr->str_data_size;
    need += (uint64_t)hdr->func_count * (uint64_t)sizeof(AxbFunction);
    need += (uint64_t)hdr->global_count * (uint64_t)sizeof(AxbGlobal);
    need += (uint64_t)hdr->native_count * (uint64_t)sizeof(AxbNative);
    need += (uint64_t)hdr->import_count * (uint64_t)sizeof(AxbImport);
    if (size < need) {
        axVmError(vm, "imagen AXB truncada (%llu bytes, se necesitan %llu)",
                  (unsigned long long)size, (unsigned long long)need);
        return -4;
    }

    off = (uint64_t)sizeof(AxbHeader);

    /* --- codigo --- */
    vm->codeSize = hdr->code_size;
    vm->code = (uint8_t*)axVmRawAlloc(&vm->cfg, vm->codeSize ? vm->codeSize : 1u);
    if (!vm->code) { axVmError(vm, "sin memoria para el codigo"); return -5; }
    axMemCpy(vm->code, image + off, vm->codeSize);
    off += vm->codeSize;

    /* --- constantes --- */
    vm->constCount = hdr->const_count;
    if (vm->constCount) {
        vm->consts = (ax_val_t*)axVmRawAlloc(&vm->cfg, (uint64_t)vm->constCount * 8u);
        if (!vm->consts) { axVmError(vm, "sin memoria para las constantes"); return -6; }
        for (i = 0; i < vm->constCount; i++) {
            vm->consts[i] = axReadU64Mem(image + off);
            off += 8u;
        }
    }

    /* --- tabla de cadenas --- */
    vm->strCount = hdr->str_count;
    if (vm->strCount) {
        vm->strOff = (uint32_t*)axVmRawAlloc(&vm->cfg, (uint64_t)vm->strCount * 4u);
        if (!vm->strOff) { axVmError(vm, "sin memoria para la tabla de cadenas"); return -7; }
        for (i = 0; i < vm->strCount; i++) {
            vm->strOff[i] = axReadU32Mem(image + off);
            off += 4u;
        }
    }
    vm->strDataSize = hdr->str_data_size;
    vm->strData = (uint8_t*)axVmRawAlloc(&vm->cfg, vm->strDataSize ? vm->strDataSize : 1u);
    if (!vm->strData) { axVmError(vm, "sin memoria para las cadenas"); return -8; }
    axMemCpy(vm->strData, image + off, vm->strDataSize);
    off += vm->strDataSize;

    /* --- funciones --- */
    vm->funcCount = hdr->func_count;
    if (vm->funcCount) {
        vm->funcs = (AxVmFunc*)axVmRawAlloc(&vm->cfg, (uint64_t)vm->funcCount * sizeof(AxVmFunc));
        if (!vm->funcs) { axVmError(vm, "sin memoria para las funciones"); return -9; }
        axMemSet(vm->funcs, 0, (uint64_t)vm->funcCount * sizeof(AxVmFunc));
        for (i = 0; i < vm->funcCount; i++) {
            const uint8_t* p = image + off;
            off += sizeof(AxbFunction);
            vm->funcs[i].addr       = axReadU32Mem(p + 4);
            vm->funcs[i].argc       = axReadU32Mem(p + 8);
            vm->funcs[i].nlocals    = axReadU32Mem(p + 12);
            vm->funcs[i].stackSize  = axReadU32Mem(p + 16);
            {
                uint32_t nameOff = axReadU32Mem(p);
                const char* nm = "";
                if (nameOff < vm->strDataSize) nm = (const char*)(vm->strData + nameOff);
                vm->funcs[i].name = axVmStrDup(&vm->cfg, nm);
            }
            if (vm->funcs[i].addr >= vm->codeSize) {
                axVmError(vm, "la funcion '%s' apunta fuera del codigo",
                          vm->funcs[i].name ? vm->funcs[i].name : "?");
                return -10;
            }
            if (vm->funcs[i].nlocals < vm->funcs[i].argc) {
                axVmError(vm, "la funcion '%s' declara menos locales que parametros",
                          vm->funcs[i].name ? vm->funcs[i].name : "?");
                return -10;
            }
        }
    }

    /* --- globales --- */
    vm->globalCount = hdr->global_count;
    vm->globalSlots = hdr->global_slots;
    if (vm->globalCount) {
        vm->globals = (AxVmGlobal*)axVmRawAlloc(&vm->cfg, (uint64_t)vm->globalCount * sizeof(AxVmGlobal));
        if (!vm->globals) { axVmError(vm, "sin memoria para los globales"); return -11; }
        axMemSet(vm->globals, 0, (uint64_t)vm->globalCount * sizeof(AxVmGlobal));
        for (i = 0; i < vm->globalCount; i++) {
            const uint8_t* p = image + off;
            off += sizeof(AxbGlobal);
            vm->globals[i].slot  = axReadU32Mem(p + 4);
            vm->globals[i].size  = axReadU32Mem(p + 8);
            vm->globals[i].flags = axReadU32Mem(p + 12);
            {
                uint32_t nameOff = axReadU32Mem(p);
                const char* nm = "";
                if (nameOff < vm->strDataSize) nm = (const char*)(vm->strData + nameOff);
                vm->globals[i].name = axVmStrDup(&vm->cfg, nm);
            }
            if (vm->globals[i].slot >= vm->globalSlots) {
                axVmError(vm, "el global '%s' usa una celda invalida",
                          vm->globals[i].name ? vm->globals[i].name : "?");
                return -12;
            }
        }
    }

    /* --- celdas de globales --- */
    if (vm->globalSlots) {
        void* mem = axHeapAlloc(vm, (uint64_t)vm->globalSlots * 8u, 0);
        if (!mem) { axVmError(vm, "sin memoria para las celdas globales"); return -13; }
        vm->globalCells = (ax_val_t*)mem;
        axMemSet(vm->globalCells, 0, (uint64_t)vm->globalSlots * 8u);
    }

    /* --- natives --- */
    vm->nativeCount = hdr->native_count;
    if (vm->nativeCount) {
        vm->natives = (AxVmNative*)axVmRawAlloc(&vm->cfg, (uint64_t)vm->nativeCount * sizeof(AxVmNative));
        if (!vm->natives) { axVmError(vm, "sin memoria para los natives"); return -14; }
        axMemSet(vm->natives, 0, (uint64_t)vm->nativeCount * sizeof(AxVmNative));
        for (i = 0; i < vm->nativeCount; i++) {
            const uint8_t* p = image + off;
            off += sizeof(AxbNative);
            vm->natives[i].id = axReadU32Mem(p + 4);
            {
                uint32_t nameOff = axReadU32Mem(p);
                const char* nm = "";
                if (nameOff < vm->strDataSize) nm = (const char*)(vm->strData + nameOff);
                vm->natives[i].name = axVmStrDup(&vm->cfg, nm);
            }
        }
    }

    /* --- imports --- */
    vm->importCount = hdr->import_count;
    if (vm->importCount) {
        vm->imports = (AxVmImport*)axVmRawAlloc(&vm->cfg, (uint64_t)vm->importCount * sizeof(AxbImport));
        if (!vm->imports) { axVmError(vm, "sin memoria para los imports"); return -15; }
        axMemSet(vm->imports, 0, (uint64_t)vm->importCount * sizeof(AxbImport));
        for (i = 0; i < vm->importCount; i++) {
            const uint8_t* p = image + off;
            off += sizeof(AxbImport);
            vm->imports[i].nativeBase = axReadU32Mem(p + 4);
            vm->imports[i].count      = axReadU32Mem(p + 8);
            {
                uint32_t nsOff = axReadU32Mem(p);
                const char* nm = "";
                if (nsOff < vm->strDataSize) nm = (const char*)(vm->strData + nsOff);
                vm->imports[i].ns = axVmStrDup(&vm->cfg, nm);
            }
        }
    }

    /* --- funciones de entrada --- */
    vm->entryFunc = hdr->entry_func;
    vm->mainFunc  = hdr->main_func;
    if (vm->funcCount == 0) {
        axVmError(vm, "la imagen no contiene ninguna funcion");
        return -16;
    }
    if (vm->entryFunc >= vm->funcCount) {
        axVmError(vm, "entry_func %u fuera de rango", (unsigned)vm->entryFunc);
        return -17;
    }
    if (vm->mainFunc != 0xFFFFFFFFu && vm->mainFunc >= vm->funcCount) {
        axVmError(vm, "main_func %u fuera de rango", (unsigned)vm->mainFunc);
        return -18;
    }

    /* --- copia de las cadenas constantes al heap --- */
    if (vm->strCount && vm->strDataSize) {
        uint8_t* blob = (uint8_t*)axHeapAlloc(vm, vm->strDataSize, 0);
        if (!blob) { axVmError(vm, "sin memoria para las cadenas constantes"); return -19; }
        axMemCpy(blob, vm->strData, vm->strDataSize);
        axMemSet(vm->strData, 0, vm->strDataSize);
        axVmRawFree(&vm->cfg, vm->strData);
        vm->strData = blob;
        vm->strDataInHeap = 1;
    }

    vm->loaded = 1;
    vm->status = AX_VM_RUNNING;
    vm->started = 0;
    vm->err[0] = '\0';
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lectura de operandos del flujo de instrucciones                    */
/* ------------------------------------------------------------------ */
static uint32_t axCodeU32(AxVM* vm) {
    uint32_t v;
    if (vm->pc + 4u > vm->codeSize) { vm->fatal = 1; return 0; }
    v = axReadU32Mem(vm->code + vm->pc);
    vm->pc += 4u;
    return v;
}

static uint64_t axCodeU64(AxVM* vm) {
    uint64_t v;
    if (vm->pc + 8u > vm->codeSize) { vm->fatal = 1; return 0; }
    v = axReadU64Mem(vm->code + vm->pc);
    vm->pc += 8u;
    return v;
}

/* Devuelve la cadena apuntada por un valor de la pila. */
static const char* axValStr(ax_val_t v) {
    return (const char*)(uintptr_t)v;
}

static uint64_t axValStrLen(ax_val_t v) {
    if (!v) return 0;
    return axStrLenBounded(axValStr(v), AX_VM_STR_SCAN_MAX);
}

/* ------------------------------------------------------------------ */
/* Llamada a una funcion bytecode                                     */
/* ------------------------------------------------------------------ */
static int axVmCall(AxVM* vm, uint32_t funcIdx, uint32_t argc) {
    AxVmFunc* f;
    uint32_t argsBase;
    uint32_t localsBase;
    uint32_t slots;
    uint32_t i;

    if (funcIdx >= vm->funcCount) {
        axVmError(vm, "indice de funcion invalido: %u", (unsigned)funcIdx);
        return 0;
    }
    f = &vm->funcs[funcIdx];
    if (argc != f->argc) {
        axVmError(vm, "'%s' espera %u argumento(s) y recibe %u",
                  f->name ? f->name : "?", (unsigned)f->argc, (unsigned)argc);
        return 0;
    }
    if (vm->frameCount >= vm->frameCap) {
        axVmError(vm, "desbordamiento de la pila de llamadas");
        return 0;
    }
    /* 'nlocals' ya incluye los parametros: el marco son 'nlocals' celdas. */
    slots = f->nlocals;
    if (slots < f->argc) {
        axVmError(vm, "'%s' declara %u local(es) pero recibe %u argumento(s)",
                  f->name ? f->name : "?", (unsigned)slots, (unsigned)f->argc);
        return 0;
    }
    if (vm->sp + slots > vm->stackCap) {
        axVmError(vm, "desbordamiento de la pila de la VM");
        return 0;
    }
    if (vm->sp < argc) {
        axVmError(vm, "pila insuficiente para la llamada");
        return 0;
    }

    argsBase  = (uint32_t)(vm->sp - argc);   /* donde empiezan los argumentos */
    localsBase = (uint32_t)vm->sp;            /* los locales van justo encima  */

    /* Copia de los argumentos a las celdas de locals (solapan hacia arriba). */
    for (i = argc; i > 0; i--) {
        vm->stack[localsBase + i - 1u] = vm->stack[argsBase + i - 1u];
    }
    for (i = f->argc; i < slots; i++) {
        vm->stack[localsBase + i] = 0;
    }

    vm->frames[vm->frameCount].funcIdx    = funcIdx;
    vm->frames[vm->frameCount].retPc      = (uint32_t)vm->pc;
    vm->frames[vm->frameCount].localsBase = localsBase;
    vm->frames[vm->frameCount].prevSp     = argsBase;
    vm->frameCount++;

    vm->sp = (uint64_t)localsBase + slots;
    vm->pc = f->addr;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Despacho de natives                                                */
/* ------------------------------------------------------------------ */
static int axVmCallNative(AxVM* vm, uint32_t id, uint32_t argc) {
    ax_val_t args[AX_VM_MAX_NATIVE_ARGS];
    ax_val_t ret = 0;
    uint32_t i;
    int rc;

    if (argc > AX_VM_MAX_NATIVE_ARGS) {
        axVmError(vm, "demasiados argumentos para el nativo %u", (unsigned)id);
        return 0;
    }
    if (id == 0 || id >= AX_NT_MAX) {
        axVmError(vm, "nativo desconocido: %u", (unsigned)id);
        return 0;
    }
    /* Los argumentos se apilan en orden: el ultimo esta en la cima. */
    for (i = argc; i > 0; i--) {
        if (!axPop(vm, &args[i - 1u])) return 0;
    }

    if (!vm->cfg.native) {
        axVmError(vm, "el anfitrion no implementa natives ('%s' id %u)",
                  axVmNativeName(vm, id), (unsigned)id);
        return 0;
    }
    rc = vm->cfg.native(vm->cfg.native_ctx, id, args, argc, &ret);
    if (rc != 0) {
        axVmError(vm, "el nativo '%s' (id %u) fallo con el codigo %d",
                  axVmNativeName(vm, id), (unsigned)id, rc);
        return 0;
    }
    return axPush(vm, ret);
}

/* ------------------------------------------------------------------ */
/* Interprete                                                         */
/* ------------------------------------------------------------------ */
AxVMStatus axvm_run(AxVM* vm, int* exit_code) {
    if (!vm) {
        if (exit_code) *exit_code = 1;
        return AX_VM_ERROR;
    }
    if (vm->status == AX_VM_ERROR) {
        if (exit_code) *exit_code = vm->exitCode;
        return AX_VM_ERROR;
    }
    if (vm->status == AX_VM_HALTED) {
        if (exit_code) *exit_code = vm->exitCode;
        return AX_VM_HALTED;
    }
    if (!vm->loaded) {
        axVmError(vm, "no hay ninguna imagen cargada en la VM");
        if (exit_code) *exit_code = 1;
        return AX_VM_ERROR;
    }

    /* ¿Seguimos durmiendo? */
    if (vm->status == AX_VM_SLEEP) {
        uint64_t now = axvmNowMs();
        if (now < vm->sleepUntil) {
            vm->sleepRemaining = vm->sleepUntil - now;
            if (exit_code) *exit_code = 0;
            return AX_VM_SLEEP;
        }
        vm->sleepRemaining = 0;
        vm->status = AX_VM_RUNNING;
    }

    if (vm->stopRequested) {
        vm->stopRequested = 0;
        vm->status = AX_VM_RUNNING;
        if (exit_code) *exit_code = 0;
        return AX_VM_RUNNING;
    }

    /* Primer arranque: se prepara la llamada a la funcion de entrada. */
    if (!vm->started) {
        AxVmFunc* f;
        uint32_t slots;
        uint32_t i;
        vm->started = 1;
        f = &vm->funcs[vm->entryFunc];
        slots = f->nlocals;
        if (vm->sp + slots > vm->stackCap) {
            axVmError(vm, "desbordamiento de la pila de la VM");
            if (exit_code) *exit_code = 1;
            return AX_VM_ERROR;
        }
        for (i = 0; i < slots; i++) vm->stack[i] = 0;
        vm->frames[0].funcIdx    = vm->entryFunc;
        vm->frames[0].retPc      = 0;
        vm->frames[0].localsBase = 0;
        vm->frames[0].prevSp     = 0;
        vm->frameCount = 1;
        vm->sp = slots;
        vm->pc = f->addr;
    }

    for (;;) {
        uint8_t op;

        if (vm->stopRequested) {
            vm->stopRequested = 0;
            vm->status = AX_VM_RUNNING;
            if (exit_code) *exit_code = 0;
            return AX_VM_RUNNING;
        }
        if (vm->pc >= vm->codeSize) {
            axVmError(vm, "el flujo de ejecucion salio del codigo");
            if (exit_code) *exit_code = 1;
            return AX_VM_ERROR;
        }
        op = vm->code[vm->pc++];
        vm->instructions++;
        vm->fatal = 0;

        switch (op) {
            case AX_OP_NOP:
                break;

            case AX_OP_PUSH_I64: {
                ax_val_t v = axCodeU64(vm);
                if (vm->fatal) goto fatal;
                if (!axPush(vm, v)) goto fail;
                break;
            }
            case AX_OP_PUSH_STR: {
                uint32_t idx = axCodeU32(vm);
                ax_val_t v;
                if (vm->fatal) goto fatal;
                if (idx >= vm->strCount) {
                    axVmError(vm, "indice de cadena invalido: %u", (unsigned)idx);
                    goto fail;
                }
                v = (ax_val_t)(uintptr_t)(vm->strData + vm->strOff[idx]);
                if (!axPush(vm, v)) goto fail;
                break;
            }
            case AX_OP_POP: {
                ax_val_t v;
                if (!axPop(vm, &v)) goto fail;
                break;
            }
            case AX_OP_DUP: {
                ax_val_t v;
                if (!axPeek(vm, &v)) goto fail;
                if (!axPush(vm, v)) goto fail;
                break;
            }
            case AX_OP_DUP2: {
                ax_val_t a, b;
                if (vm->sp < 2) { axVmError(vm, "pila insuficiente para DUP2"); goto fail; }
                b = vm->stack[vm->sp - 1u];
                a = vm->stack[vm->sp - 2u];
                if (!axPush(vm, a)) goto fail;
                if (!axPush(vm, b)) goto fail;
                break;
            }
            case AX_OP_SWAP: {
                ax_val_t t;
                if (vm->sp < 2) { axVmError(vm, "pila insuficiente para SWAP"); goto fail; }
                t = vm->stack[vm->sp - 1u];
                vm->stack[vm->sp - 1u] = vm->stack[vm->sp - 2u];
                vm->stack[vm->sp - 2u] = t;
                break;
            }

            case AX_OP_LOAD_LOCAL: {
                uint32_t idx = axCodeU32(vm);
                if (vm->fatal) goto fatal;
                if (vm->frameCount == 0) { axVmError(vm, "LOAD_LOCAL fuera de una funcion"); goto fail; }
                if (idx >= vm->funcs[vm->frames[vm->frameCount - 1u].funcIdx].nlocals) {
                    axVmError(vm, "local %u fuera de rango", (unsigned)idx);
                    goto fail;
                }
                if (!axPush(vm, vm->stack[vm->frames[vm->frameCount - 1u].localsBase + idx])) goto fail;
                break;
            }
            case AX_OP_STORE_LOCAL: {
                uint32_t idx = axCodeU32(vm);
                ax_val_t v;
                if (vm->fatal) goto fatal;
                if (vm->frameCount == 0) { axVmError(vm, "STORE_LOCAL fuera de una funcion"); goto fail; }
                if (!axPop(vm, &v)) goto fail;
                if (idx >= vm->funcs[vm->frames[vm->frameCount - 1u].funcIdx].nlocals) {
                    axVmError(vm, "local %u fuera de rango", (unsigned)idx);
                    goto fail;
                }
                vm->stack[vm->frames[vm->frameCount - 1u].localsBase + idx] = v;
                break;
            }
            case AX_OP_LOAD_GLOBAL: {
                uint32_t idx = axCodeU32(vm);
                if (vm->fatal) goto fatal;
                if (idx >= vm->globalSlots) {
                    axVmError(vm, "global %u fuera de rango", (unsigned)idx);
                    goto fail;
                }
                if (!axPush(vm, vm->globalCells[idx])) goto fail;
                break;
            }
            case AX_OP_STORE_GLOBAL: {
                uint32_t idx = axCodeU32(vm);
                ax_val_t v;
                if (vm->fatal) goto fatal;
                if (!axPop(vm, &v)) goto fail;
                if (idx >= vm->globalSlots) {
                    axVmError(vm, "global %u fuera de rango", (unsigned)idx);
                    goto fail;
                }
                vm->globalCells[idx] = v;
                break;
            }
            case AX_OP_LOCAL_ADDR: {
                uint32_t idx = axCodeU32(vm);
                ax_val_t addr;
                if (vm->fatal) goto fatal;
                if (vm->frameCount == 0) { axVmError(vm, "LOCAL_ADDR fuera de una funcion"); goto fail; }
                if (idx >= vm->funcs[vm->frames[vm->frameCount - 1u].funcIdx].nlocals) {
                    axVmError(vm, "local %u fuera de rango", (unsigned)idx);
                    goto fail;
                }
                addr = (ax_val_t)(uintptr_t)&vm->stack[vm->frames[vm->frameCount - 1u].localsBase + idx];
                if (!axPush(vm, addr)) goto fail;
                break;
            }
            case AX_OP_GLOBAL_ADDR: {
                uint32_t idx = axCodeU32(vm);
                ax_val_t addr;
                if (vm->fatal) goto fatal;
                if (idx >= vm->globalSlots) {
                    axVmError(vm, "global %u fuera de rango", (unsigned)idx);
                    goto fail;
                }
                addr = (ax_val_t)(uintptr_t)&vm->globalCells[idx];
                if (!axPush(vm, addr)) goto fail;
                break;
            }

            case AX_OP_JMP: {
                uint32_t target = axCodeU32(vm);
                if (vm->fatal) goto fatal;
                if (target >= vm->codeSize) { axVmError(vm, "salto a %u fuera del codigo", (unsigned)target); goto fail; }
                vm->pc = target;
                break;
            }
            case AX_OP_JZ: {
                uint32_t target = axCodeU32(vm);
                ax_val_t v;
                if (vm->fatal) goto fatal;
                if (!axPop(vm, &v)) goto fail;
                if (v == 0) {
                    if (target >= vm->codeSize) { axVmError(vm, "salto a %u fuera del codigo", (unsigned)target); goto fail; }
                    vm->pc = target;
                }
                break;
            }
            case AX_OP_JNZ: {
                uint32_t target = axCodeU32(vm);
                ax_val_t v;
                if (vm->fatal) goto fatal;
                if (!axPop(vm, &v)) goto fail;
                if (v != 0) {
                    if (target >= vm->codeSize) { axVmError(vm, "salto a %u fuera del codigo", (unsigned)target); goto fail; }
                    vm->pc = target;
                }
                break;
            }
            case AX_OP_JLT: {
                uint32_t target = axCodeU32(vm);
                ax_val_t b, a;
                if (vm->fatal) goto fatal;
                if (!axPop(vm, &b)) goto fail;
                if (!axPop(vm, &a)) goto fail;
                if ((int64_t)a < (int64_t)b) {
                    if (target >= vm->codeSize) { axVmError(vm, "salto a %u fuera del codigo", (unsigned)target); goto fail; }
                    vm->pc = target;
                }
                break;
            }
            case AX_OP_JGE: {
                uint32_t target = axCodeU32(vm);
                ax_val_t b, a;
                if (vm->fatal) goto fatal;
                if (!axPop(vm, &b)) goto fail;
                if (!axPop(vm, &a)) goto fail;
                if ((int64_t)a >= (int64_t)b) {
                    if (target >= vm->codeSize) { axVmError(vm, "salto a %u fuera del codigo", (unsigned)target); goto fail; }
                    vm->pc = target;
                }
                break;
            }
            case AX_OP_CALL: {
                uint32_t fidx = axCodeU32(vm);
                uint32_t argc = axCodeU32(vm);
                if (vm->fatal) goto fatal;
                if (!axVmCall(vm, fidx, argc)) goto fail;
                break;
            }
            case AX_OP_CALL_NATIVE: {
                uint32_t id = axCodeU32(vm);
                uint32_t argc = axCodeU32(vm);
                if (vm->fatal) goto fatal;
                if (!axVmCallNative(vm, id, argc)) goto fail;
                break;
            }
            case AX_OP_RET: {
                ax_val_t v = 0;
                if (!axPop(vm, &v)) goto fail;
                if (vm->frameCount <= 1u) {
                    vm->frameCount = 0;
                    vm->sp = 0;
                    vm->status = AX_VM_HALTED;
                    vm->exitCode = (int)(int64_t)v;
                    if (exit_code) *exit_code = vm->exitCode;
                    return AX_VM_HALTED;
                }
                {
                    AxVmFrame fr = vm->frames[--vm->frameCount];
                    vm->sp = fr.prevSp;
                    vm->pc = fr.retPc;
                }
                if (!axPush(vm, v)) goto fail;
                break;
            }

            /* --- aritmetica --- */
            case AX_OP_ADD: case AX_OP_SUB: case AX_OP_MUL:
            case AX_OP_DIV: case AX_OP_MOD: {
                ax_val_t b, a;
                int64_t r = 0;
                if (!axPop(vm, &b)) goto fail;
                if (!axPop(vm, &a)) goto fail;
                switch (op) {
                    case AX_OP_ADD: r = (int64_t)a + (int64_t)b; break;
                    case AX_OP_SUB: r = (int64_t)a - (int64_t)b; break;
                    case AX_OP_MUL: r = (int64_t)a * (int64_t)b; break;
                    case AX_OP_DIV:
                        if ((int64_t)b == 0) { axVmError(vm, "division por cero"); goto fail; }
                        if ((int64_t)a == (-9223372036854775807LL - 1) && (int64_t)b == -1) {
                            axVmError(vm, "desbordamiento en la division"); goto fail;
                        }
                        r = (int64_t)a / (int64_t)b;
                        break;
                    case AX_OP_MOD:
                        if ((int64_t)b == 0) { axVmError(vm, "modulo por cero"); goto fail; }
                        if ((int64_t)a == (-9223372036854775807LL - 1) && (int64_t)b == -1) {
                            r = 0;
                        } else {
                            r = (int64_t)a % (int64_t)b;
                        }
                        break;
                    default: break;
                }
                if (!axPush(vm, (ax_val_t)r)) goto fail;
                break;
            }
            case AX_OP_NEG: {
                ax_val_t a;
                if (!axPop(vm, &a)) goto fail;
                if (!axPush(vm, (ax_val_t)(-(int64_t)a))) goto fail;
                break;
            }
            case AX_OP_NOT: {
                ax_val_t a;
                if (!axPop(vm, &a)) goto fail;
                if (!axPush(vm, a ? 0u : 1u)) goto fail;
                break;
            }
            case AX_OP_BITNOT: {
                ax_val_t a;
                if (!axPop(vm, &a)) goto fail;
                if (!axPush(vm, ~a)) goto fail;
                break;
            }
            case AX_OP_AND: case AX_OP_OR: case AX_OP_XOR:
            case AX_OP_SHL: case AX_OP_SHR: case AX_OP_USHR: {
                ax_val_t b, a, r = 0;
                if (!axPop(vm, &b)) goto fail;
                if (!axPop(vm, &a)) goto fail;
                switch (op) {
                    case AX_OP_AND:  r = a & b; break;
                    case AX_OP_OR:   r = a | b; break;
                    case AX_OP_XOR:  r = a ^ b; break;
                    case AX_OP_SHL:  r = a << (b & 63u); break;
                    case AX_OP_SHR:  r = (ax_val_t)((int64_t)a >> (b & 63u)); break;
                    case AX_OP_USHR: r = a >> (b & 63u); break;
                    default: break;
                }
                if (!axPush(vm, r)) goto fail;
                break;
            }

            /* --- comparacion --- */
            case AX_OP_EQ: case AX_OP_NE: case AX_OP_LT:
            case AX_OP_LE: case AX_OP_GT: case AX_OP_GE: {
                ax_val_t b, a;
                int res = 0;
                if (!axPop(vm, &b)) goto fail;
                if (!axPop(vm, &a)) goto fail;
                switch (op) {
                    case AX_OP_EQ: res = (a == b); break;
                    case AX_OP_NE: res = (a != b); break;
                    case AX_OP_LT: res = ((int64_t)a <  (int64_t)b); break;
                    case AX_OP_LE: res = ((int64_t)a <= (int64_t)b); break;
                    case AX_OP_GT: res = ((int64_t)a >  (int64_t)b); break;
                    case AX_OP_GE: res = ((int64_t)a >= (int64_t)b); break;
                    default: break;
                }
                if (!axPush(vm, res ? 1u : 0u)) goto fail;
                break;
            }

            /* --- memoria --- */
            case AX_OP_DEREF: {
                ax_val_t p, v;
                if (!axPop(vm, &p)) goto fail;
                if (!axMemRead(vm, p, &v)) {
                    axVmError(vm, "puntero invalido: 0x%llx", (unsigned long long)p);
                    goto fail;
                }
                if (!axPush(vm, v)) goto fail;
                break;
            }
            case AX_OP_STORE: {
                ax_val_t v, p;
                if (!axPop(vm, &v)) goto fail;
                if (!axPop(vm, &p)) goto fail;
                if (!axMemWrite(vm, p, v)) {
                    axVmError(vm, "puntero invalido: 0x%llx", (unsigned long long)p);
                    goto fail;
                }
                break;
            }
            case AX_OP_NEW: {
                uint64_t bytes = axCodeU64(vm);
                void* mem;
                if (vm->fatal) goto fatal;
                mem = axHeapAlloc(vm, bytes, 1);
                if (!mem) { axVmError(vm, "heap agotado (%llu bytes)", (unsigned long long)bytes); goto fail; }
                if (!axPush(vm, (ax_val_t)(uintptr_t)mem)) goto fail;
                break;
            }
            case AX_OP_FREE: {
                ax_val_t p;
                if (!axPop(vm, &p)) goto fail;
                axHeapFree(vm, (void*)(uintptr_t)p);
                break;
            }
            case AX_OP_ARRAY_NEW: {
                uint32_t count = axCodeU32(vm);
                void* mem;
                if (vm->fatal) goto fatal;
                mem = axHeapAlloc(vm, (uint64_t)count * 8u, 1);
                if (!mem) { axVmError(vm, "heap agotado (array de %u celdas)", (unsigned)count); goto fail; }
                axMemSet(mem, 0, (uint64_t)count * 8u);
                if (!axPush(vm, (ax_val_t)(uintptr_t)mem)) goto fail;
                break;
            }
            case AX_OP_ARRAY_GET: {
                ax_val_t idx, base, v;
                if (!axPop(vm, &idx)) goto fail;
                if (!axPop(vm, &base)) goto fail;
                if (!axMemRead(vm, base + (idx * 8u), &v)) {
                    axVmError(vm, "indice de array fuera de rango (%llu)",
                              (unsigned long long)idx);
                    goto fail;
                }
                if (!axPush(vm, v)) goto fail;
                break;
            }
            case AX_OP_ARRAY_SET: {
                ax_val_t v, idx, base;
                if (!axPop(vm, &v)) goto fail;
                if (!axPop(vm, &idx)) goto fail;
                if (!axPop(vm, &base)) goto fail;
                if (!axMemWrite(vm, base + (idx * 8u), v)) {
                    axVmError(vm, "indice de array fuera de rango (%llu)",
                              (unsigned long long)idx);
                    goto fail;
                }
                break;
            }
            case AX_OP_ARRAY_LEN: {
                ax_val_t base;
                uint64_t size;
                if (!axPop(vm, &base)) goto fail;
                size = axHeapBlockSize(vm, (void*)(uintptr_t)base);
                if (!size) { axVmError(vm, "puntero de array invalido"); goto fail; }
                if (!axPush(vm, (ax_val_t)(size / 8u))) goto fail;
                break;
            }
            case AX_OP_STRUCT_NEW: {
                uint64_t bytes = axCodeU64(vm);
                void* mem;
                if (vm->fatal) goto fatal;
                mem = axHeapAlloc(vm, bytes ? bytes : 8u, 1);
                if (!mem) { axVmError(vm, "heap agotado (%llu bytes)", (unsigned long long)bytes); goto fail; }
                axMemSet(mem, 0, bytes ? bytes : 8u);
                if (!axPush(vm, (ax_val_t)(uintptr_t)mem)) goto fail;
                break;
            }
            case AX_OP_STRUCT_GET: {
                uint32_t off = axCodeU32(vm);
                ax_val_t p, v;
                if (vm->fatal) goto fatal;
                if (!axPop(vm, &p)) goto fail;
                if (!axMemRead(vm, p + off, &v)) {
                    axVmError(vm, "acceso a un struct invalido (offset %u)", (unsigned)off);
                    goto fail;
                }
                if (!axPush(vm, v)) goto fail;
                break;
            }
            case AX_OP_STRUCT_SET: {
                uint32_t off = axCodeU32(vm);
                ax_val_t v, p;
                if (vm->fatal) goto fatal;
                if (!axPop(vm, &v)) goto fail;
                if (!axPop(vm, &p)) goto fail;
                if (!axMemWrite(vm, p + off, v)) {
                    axVmError(vm, "acceso a un struct invalido (offset %u)", (unsigned)off);
                    goto fail;
                }
                break;
            }

            /* --- cadenas --- */
            case AX_OP_STR_LEN: {
                ax_val_t s;
                if (!axPop(vm, &s)) goto fail;
                if (!axPush(vm, (ax_val_t)axValStrLen(s))) goto fail;
                break;
            }
            case AX_OP_STR_CONCAT: {
                ax_val_t b, a;
                uint64_t la, lb;
                char* dst;
                if (!axPop(vm, &b)) goto fail;
                if (!axPop(vm, &a)) goto fail;
                la = axValStrLen(a);
                lb = axValStrLen(b);
                dst = (char*)axHeapAlloc(vm, la + lb + 1u, 1);
                if (!dst) { axVmError(vm, "heap agotado (concatenacion)"); goto fail; }
                if (la) axMemCpy(dst, axValStr(a), la);
                if (lb) axMemCpy(dst + la, axValStr(b), lb);
                dst[la + lb] = '\0';
                if (!axPush(vm, (ax_val_t)(uintptr_t)dst)) goto fail;
                break;
            }
            case AX_OP_STR_EQ: {
                ax_val_t b, a;
                uint64_t la, lb, i;
                int eq = 1;
                if (!axPop(vm, &b)) goto fail;
                if (!axPop(vm, &a)) goto fail;
                la = axValStrLen(a);
                lb = axValStrLen(b);
                if (la != lb) { eq = 0; }
                else {
                    for (i = 0; i < la; i++) {
                        if (axValStr(a)[i] != axValStr(b)[i]) { eq = 0; break; }
                    }
                }
                if (!axPush(vm, eq ? 1u : 0u)) goto fail;
                break;
            }
            case AX_OP_STR_CMP: {
                ax_val_t b, a;
                uint64_t la, lb, n, i;
                int res = 0;
                if (!axPop(vm, &b)) goto fail;
                if (!axPop(vm, &a)) goto fail;
                la = axValStrLen(a);
                lb = axValStrLen(b);
                n = (la < lb) ? la : lb;
                for (i = 0; i < n; i++) {
                    unsigned char ca = (unsigned char)axValStr(a)[i];
                    unsigned char cb = (unsigned char)axValStr(b)[i];
                    if (ca != cb) { res = (ca < cb) ? -1 : 1; break; }
                }
                if (res == 0) {
                    if (la < lb) res = -1;
                    else if (la > lb) res = 1;
                }
                if (!axPush(vm, (ax_val_t)(int64_t)res)) goto fail;
                break;
            }
            case AX_OP_STR_CHAR_AT: {
                ax_val_t idx, s;
                uint64_t len;
                if (!axPop(vm, &idx)) goto fail;
                if (!axPop(vm, &s)) goto fail;
                len = axValStrLen(s);
                if (idx >= len) {
                    if (!axPush(vm, 0u)) goto fail;
                } else {
                    if (!axPush(vm, (ax_val_t)(unsigned char)axValStr(s)[idx])) goto fail;
                }
                break;
            }

            /* --- salida --- */
            case AX_OP_PRINT:
            case AX_OP_PRINTLN: {
                ax_val_t s;
                const char* txt;
                uint64_t len;
                if (!axPop(vm, &s)) goto fail;
                txt = axValStr(s);
                len = axValStrLen(s);
                if (vm->cfg.print) {
                    vm->cfg.print(vm->cfg.print_ctx, txt, len);
                    if (op == AX_OP_PRINTLN) vm->cfg.print(vm->cfg.print_ctx, "\n", 1u);
                }
                break;
            }
            case AX_OP_PRINT_I64: {
                ax_val_t v;
                char buf[32];
                if (!axPop(vm, &v)) goto fail;
                axFormat(buf, sizeof(buf), "%lld", (long long)(int64_t)v);
                if (vm->cfg.print) vm->cfg.print(vm->cfg.print_ctx, buf, axStrLen(buf));
                break;
            }

            /* --- sistema --- */
            case AX_OP_YIELD:
                vm->status = AX_VM_YIELD;
                if (exit_code) *exit_code = 0;
                return AX_VM_YIELD;
            case AX_OP_SLEEP: {
                uint32_t ms = axCodeU32(vm);
                if (vm->fatal) goto fatal;
                vm->sleepUntil = axvmNowMs() + (uint64_t)ms;
                vm->sleepRemaining = (uint64_t)ms;
                vm->status = AX_VM_SLEEP;
                if (exit_code) *exit_code = 0;
                return AX_VM_SLEEP;
            }
            case AX_OP_HALT: {
                ax_val_t v = 0;
                if (!axPop(vm, &v)) goto fail;
                vm->status = AX_VM_HALTED;
                vm->exitCode = (int)(int64_t)v;
                if (exit_code) *exit_code = vm->exitCode;
                return AX_VM_HALTED;
            }
            case AX_OP_ASSERT: {
                uint32_t line = axCodeU32(vm);
                ax_val_t v;
                if (vm->fatal) goto fatal;
                if (!axPop(vm, &v)) goto fail;
                if (v == 0) {
                    axVmError(vm, "asercion fallida en la linea %u", (unsigned)line);
                    goto fail;
                }
                break;
            }

            default:
                axVmError(vm, "opcode invalido: 0x%02x", (unsigned)op);
                goto fail;
        }
        continue;

    fatal:
        axVmError(vm, "flujo de instrucciones truncado");
        goto fail;
    fail:
        if (exit_code) *exit_code = vm->exitCode ? vm->exitCode : 1;
        return AX_VM_ERROR;
    }
}

/* ------------------------------------------------------------------ */
/* API auxiliar                                                        */
/* ------------------------------------------------------------------ */
void axvm_stop(AxVM* vm) {
    if (vm) vm->stopRequested = 1;
}

const char* axvm_last_error(const AxVM* vm) {
    if (!vm) return "VM nula";
    if (vm->err[0] == '\0') return "";
    return vm->err;
}

uint64_t axvm_instructions(const AxVM* vm) {
    return vm ? vm->instructions : 0u;
}

uint64_t axvm_sleep_remaining(const AxVM* vm) {
    return vm ? vm->sleepRemaining : 0u;
}

uint64_t axvm_heap_base(const AxVM* vm) {
    return vm ? (uint64_t)(uintptr_t)vm->heap : 0u;
}

uint64_t axvm_heap_size(const AxVM* vm) {
    return vm ? vm->heapSize : 0u;
}
