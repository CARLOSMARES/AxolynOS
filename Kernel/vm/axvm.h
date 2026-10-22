/*
 * AxolynOS - Interfaz publica de la maquina virtual de bytecode (AXB).
 *
 * El mismo fuente (Kernel/vm/axvm.cpp) se compila:
 *   - dentro del kernel (freestanding, sin STL ni libc)
 *   - en el host, para el runner de pruebas compiladores/axc/axrun
 *
 * Por eso aqui solo se usan tipos primitivos y punteros void*.
 *
 * Documentacion: manuales/10-bytecode-vm.md
 */
#ifndef AX_VM_H
#define AX_VM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t ax_val_t;

typedef struct AxVM AxVM;

/* ------------------------------------------------------------------ */
/* Callbacks que provee el anfitrion                                    */
/* ------------------------------------------------------------------ */
typedef void* (*axvm_alloc_fn)(void* ctx, uint64_t size);
typedef void  (*axvm_free_fn)(void* ctx, void* ptr);
typedef void  (*axvm_print_fn)(void* ctx, const char* s, uint64_t len);
typedef void  (*axvm_status_fn)(void* ctx, int status);

/* Llamada nativa: recibe los argumentos, escribe el retorno en *ret y
 * devuelve 0 si todo va bien o un codigo de error AxVM. */
typedef int   (*axvm_native_fn)(void* ctx, uint32_t id, ax_val_t* args,
                                uint32_t argc, ax_val_t* ret);

typedef struct {
    axvm_alloc_fn   alloc;      void* alloc_ctx;
    axvm_free_fn    free;       void* free_ctx;
    axvm_print_fn   print;      void* print_ctx;
    axvm_native_fn  native;     void* native_ctx;
    axvm_status_fn  status;     void* status_ctx;
    uint64_t        stack_size; /* celdas de la pila de la VM  */
    uint64_t        heap_size;  /* bytes del heap de la VM      */
} AxVMConfig;

/* ------------------------------------------------------------------ */
/* Estados de ejecucion                                                */
/* ------------------------------------------------------------------ */
typedef enum {
    AX_VM_RUNNING = 0,  /* sigue ejecutando (solo tras axvm_resume)   */
    AX_VM_HALTED  = 1,  /* el programa termino con HALT/exit            */
    AX_VM_YIELD   = 2,  /* cedio el CPU voluntarymente                 */
    AX_VM_SLEEP   = 3,  /* esta durmiendo (llamar de nuevo luego)      */
    AX_VM_ERROR   = 4   /* error fatal, ver axvm_last_error()          */
} AxVMStatus;

/* ------------------------------------------------------------------ */
/* Tabla de funciones nativas (ids compartidos con el compilador)     */
/* ------------------------------------------------------------------ */
enum {
    AX_NT_CORE_PRINT      = 1,
    AX_NT_CORE_PRINTLN    = 2,
    AX_NT_CORE_PRINT_INT  = 3,
    AX_NT_CORE_STRLEN     = 4,
    AX_NT_CORE_INT_TO_STR = 5,

    AX_NT_CONSOLE_WRITE   = 6,
    AX_NT_CONSOLE_READLN  = 7,
    AX_NT_CONSOLE_CLEAR   = 8,

    AX_NT_FS_OPEN         = 9,
    AX_NT_FS_READ         = 10,
    AX_NT_FS_WRITE        = 11,
    AX_NT_FS_CLOSE        = 12,
    AX_NT_FS_LIST         = 13,
    AX_NT_FS_STAT         = 14,

    AX_NT_PROC_EXIT       = 15,
    AX_NT_PROC_PID        = 16,
    AX_NT_PROC_SPAWN      = 17,
    AX_NT_PROC_GETS       = 18,

    AX_NT_MEM_INFO        = 19,
    AX_NT_MEM_ALLOC       = 20,
    AX_NT_MEM_FREE        = 21,

    AX_NT_TIME_NOW_MS     = 22,
    AX_NT_TIME_SLEEP      = 23,

    AX_NT_MAX             = 24
};

typedef struct {
    const char* ns;        /* nombre del namespace            */
    uint32_t    first_id;  /* primer id nativo del namespace  */
    uint32_t    count;     /* numero de funciones            */
} AxNativeNs;

/* Tabla de namespaces; el orden define los ids (ver enum anterior). */
extern const AxNativeNs ax_native_namespaces[];
extern const uint32_t    ax_native_ns_count;

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

/* Crea la VM. Devuelve 0 en exito o un codigo de error negativo. */
int  axvm_create(const AxVMConfig* cfg, AxVM** out);
void axvm_destroy(AxVM* vm);

/* Carga una imagen .axb en memoria. Devuelve 0 o -errno. */
int  axvm_load(AxVM* vm, const uint8_t* image, uint64_t size);

/* Ejecuta hasta HALT / YIELD / SLEEP / error. */
AxVMStatus axvm_run(AxVM* vm, int* exit_code);

/* Marca la VM para que axvm_run termine en la proxima instruccion. */
void axvm_stop(AxVM* vm);

/* Ultimo error (cadena estatica, valida hasta la proxima llamada). */
const char* axvm_last_error(const AxVM* vm);

/* Numero de instrucciones ejecutadas (util para depurar). */
uint64_t axvm_instructions(const AxVM* vm);

/* Milisegundos que faltan para despertar de un AX_VM_SLEEP. */
uint64_t axvm_sleep_remaining(const AxVM* vm);

/* La VM expone su heap para el kernel (modulos) */
uint64_t axvm_heap_base(const AxVM* vm);
uint64_t axvm_heap_size(const AxVM* vm);

/*
 * Reserva y libera memoria en el heap del programa.  Lo usa el anfitrion
 * para implementar el nativo "mem.alloc" / "mem.free": las direcciones
 * devueltas son validas dentro de la maquina virtual.
 */
void* axvm_alloc(AxVM* vm, uint64_t size);
void  axvm_free(AxVM* vm, void* ptr);

#ifdef __cplusplus
}
#endif

#endif /* AX_VM_H */
