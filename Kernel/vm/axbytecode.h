/*
 * AxolynOS - Formato de bytecode "AXB" (Axolyn Bytecode).
 *
 * Contrato entre el compilador (compiladores/axc) y la maquina virtual
 * (Kernel/vm/axvm.cpp). Todo el formato es little-endian.
 *
 * Documentacion: manuales/10-bytecode-vm.md
 */
#ifndef AX_BYTECODE_H
#define AX_BYTECODE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AXB_MAGIC        "AXBYTCD1"
#define AXB_MAGIC_LEN    8
#define AXB_VERSION      1u

/* ------------------------------------------------------------------ */
/* Opcodes                                                            */
/* ------------------------------------------------------------------ */
enum AxOpcode {
    AX_OP_NOP          = 0x00,

    /* --- pila ------------------------------------------------------ */
    AX_OP_PUSH_I64     = 0x01, /* i64 immediato                       */
    AX_OP_PUSH_STR     = 0x02, /* u32 indice en la tabla de strings   */
    AX_OP_POP         = 0x03,
    AX_OP_DUP         = 0x04,
    AX_OP_DUP2        = 0x05,
    AX_OP_SWAP        = 0x06,

    /* --- variables -------------------------------------------------- */
    AX_OP_LOAD_LOCAL  = 0x07, /* u32 indice de local (frame)          */
    AX_OP_STORE_LOCAL = 0x08, /* u32 indice de local                  */
    AX_OP_LOAD_GLOBAL = 0x09, /* u32 indice de global                 */
    AX_OP_STORE_GLOBAL= 0x0A, /* u32 indice de global                 */
    AX_OP_LOCAL_ADDR  = 0x0B, /* u32 -> direccion de la celda local   */
    AX_OP_GLOBAL_ADDR = 0x0C, /* u32 -> direccion de la celda global  */

    /* --- control de flujo ------------------------------------------- */
    AX_OP_JMP         = 0x0D, /* u32 offset absoluto dentro del code  */
    AX_OP_JZ          = 0x0E, /* u32 offset absoluto                  */
    AX_OP_JNZ         = 0x0F, /* u32 offset absoluto                  */
    AX_OP_JLT         = 0x10, /* u32 (bloque if/while)                */
    AX_OP_JGE         = 0x11, /* u32                                  */
    AX_OP_CALL        = 0x12, /* u32 funcIndex, u32 argc              */
    AX_OP_CALL_NATIVE = 0x13, /* u32 nativeId, u32 argc               */
    AX_OP_RET         = 0x14,

    /* --- aritmetica ------------------------------------------------- */
    AX_OP_ADD         = 0x15,
    AX_OP_SUB         = 0x16,
    AX_OP_MUL         = 0x17,
    AX_OP_DIV         = 0x18,
    AX_OP_MOD         = 0x19,
    AX_OP_NEG         = 0x1A,
    AX_OP_NOT         = 0x1B,   /* !logico                              */
    AX_OP_BITNOT      = 0x1C,   /* ~                                   */
    AX_OP_AND         = 0x1D,   /* && corto circuito via jumps          */
    AX_OP_OR          = 0x1E,
    AX_OP_XOR         = 0x1F,
    AX_OP_SHL         = 0x20,
    AX_OP_SHR         = 0x21,
    AX_OP_USHR        = 0x22,

    /* --- comparacion (dejan 0 o 1 en la pila) ------------------------ */
    AX_OP_EQ          = 0x23,
    AX_OP_NE          = 0x24,
    AX_OP_LT          = 0x25,
    AX_OP_LE          = 0x26,
    AX_OP_GT          = 0x27,
    AX_OP_GE          = 0x28,

    /* --- memoria ---------------------------------------------------- */
    AX_OP_DEREF       = 0x29,  /* pops puntero, pushes celda de 8 bytes */
    AX_OP_STORE       = 0x2A,  /* pops valor, pops puntero, escribe     */
    AX_OP_NEW         = 0x2B,  /* u64 bytes -> puntero (heap de la VM)  */
    AX_OP_FREE        = 0x2C,
    AX_OP_ARRAY_NEW   = 0x2D,  /* u32 cantidad de celdas (8 bytes)      */
    AX_OP_ARRAY_GET   = 0x2E,  /* pops indice, pops base -> valor      */
    AX_OP_ARRAY_SET   = 0x2F,  /* pops valor, indice, base             */
    AX_OP_ARRAY_LEN   = 0x30,
    AX_OP_STRUCT_NEW  = 0x31,  /* u64 bytes                            */
    AX_OP_STRUCT_GET  = 0x32,  /* u32 offset del campo                 */
    AX_OP_STRUCT_SET  = 0x33,  /* u32 offset del campo                 */

    /* --- strings ---------------------------------------------------- */
    AX_OP_STR_LEN     = 0x34,
    AX_OP_STR_CONCAT  = 0x35,
    AX_OP_STR_EQ      = 0x36,
    AX_OP_STR_CMP     = 0x37,
    AX_OP_STR_CHAR_AT = 0x38,

    /* --- salida ----------------------------------------------------- */
    AX_OP_PRINT       = 0x39,  /* pops puntero a string                */
    AX_OP_PRINTLN     = 0x3A,
    AX_OP_PRINT_I64   = 0x3B,

    /* --- sistema ---------------------------------------------------- */
    AX_OP_YIELD       = 0x3C,
    AX_OP_SLEEP       = 0x3D,  /* u32 milisegundos                     */
    AX_OP_HALT        = 0x3E,  /* termina la VM con el valor de la pila */
    AX_OP_ASSERT      = 0x3F,  /* u32 linea: aborta si la celda es 0    */

    AX_OP_MAX
};

/* ------------------------------------------------------------------ */
/* Cabecera del fichero .axb                                           */
/* ------------------------------------------------------------------ */
#pragma pack(push, 1)
typedef struct {
    char     magic[AXB_MAGIC_LEN]; /* "AXBYTCD1"                   */
    uint32_t version;              /* AXB_VERSION                  */
    uint32_t flags;                /* reservado (0)                */
    uint32_t code_size;            /* bytes de codigo              */
    uint32_t const_count;          /* cantidad de constantes i64   */
    uint32_t str_count;            /* entradas de la tabla strings */
    uint32_t str_data_size;        /* bytes del blob de strings    */
    uint32_t func_count;           /* funciones declaradas         */
    uint32_t global_count;         /* variables globales           */
    uint32_t native_count;         /* natives conocidas             */
    uint32_t import_count;         /* namespaces importados        */
    uint32_t entry_func;           /* indice de la funcion de entry*/
    uint32_t main_func;            /* indice de main() o 0xFFFFFFFF*/
    uint32_t global_slots;         /* celdas de 8 bytes para globals*/
    uint32_t reserved;
} AxbHeader;

typedef struct {
    uint32_t name_off;   /* offset en str_data del nombre            */
    uint32_t addr;       /* offset en code donde arranca la funcion  */
    uint32_t argc;       /* numero de parametros                     */
    uint32_t nlocals;    /* numero de variables locales              */
    uint32_t stack_size; /* celdas reservadas en el frame             */
} AxbFunction;

typedef struct {
    uint32_t name_off;
    uint32_t slot;       /* indice de celda global                   */
    uint32_t size;
    uint32_t flags;
} AxbGlobal;

typedef struct {
    uint32_t name_off;
    uint32_t id;         /* id de la tabla nativa                    */
} AxbNative;

typedef struct {
    uint32_t ns_off;     /* offset en str_data del namespace         */
    uint32_t native_base;/* primer native_id del namespace           */
    uint32_t count;      /* cuantos natives tiene                   */
} AxbImport;
#pragma pack(pop)

/* Namespace por defecto de la tabla nativa (ver axvm.hpp AX_NATIVE_*) */
#define AX_IMPORT_NS_CORE     "core"
#define AX_IMPORT_NS_CONSOLE  "console"
#define AX_IMPORT_NS_FS       "fs"
#define AX_IMPORT_NS_PROC     "proc"
#define AX_IMPORT_NS_MEM      "mem"
#define AX_IMPORT_NS_TIME     "time"

#ifdef __cplusplus
}
#endif

#endif /* AX_BYTECODE_H */
