/*
 * AxolynOS - Gestor de modulos del kernel.
 *
 * Un modulo es un binario plano (.axmod) con la cabecera AxModHeader.
 * El kernel lo carga en una ranura de la ventana de modulos y le pasa un
 * AxModuleContext con los servicios que puede usar.
 *
 * Formato del fichero:
 *   [AxModHeader][codigo][datos]
 * Documentacion: manuales/05-modulos.md
 */
#ifndef AX_MODULE_H
#define AX_MODULE_H

#include "ax_types.h"
#include "ax_syscall.h"

#define AX_MOD_MAGIC "AXMOD001"
#define AX_MOD_MAGIC_LEN 8

#define AX_MOD_ABI_VERSION 1

typedef enum ax_module_state_e {
    AX_MOD_STATE_FREE = 0,
    AX_MOD_STATE_LOADED = 1,
    AX_MOD_STATE_FAILED = 2
} ax_module_state_t;

#pragma pack(push, 1)
typedef struct {
    char     magic[AX_MOD_MAGIC_LEN];
    uint32_t abi_version;
    uint32_t flags;
    uint64_t entry_offset;    /* offset del punto de entrada        */
    uint64_t init_offset;     /* offset de ax_module_init           */
    uint64_t code_size;       /* bytes de codigo                    */
    uint64_t data_size;       /* bytes de datos (iniciales)         */
    uint64_t bss_size;        /* bytes de .bss a cero               */
    uint32_t slot;            /* ranura asignada                    */
    uint32_t reserved;
    char     name[32];
    uint32_t checksum;        /* suma simple de los bytes de codigo */
} AxModHeader;

typedef struct AxModuleContext {
    uint32_t abi_version;
    const char* name;
    void*  (*alloc)(uint64_t size);
    void   (*free)(void* ptr);
    void   (*print)(const char* text);
    int    (*register_syscall)(uint64_t nr, ax_syscall_handler handler);
    void   (*ipc_subscribe)(int port);
    uint64_t (*time_ms)(void);
    int    (*publish)(const char* name, void* symbol);
    void*  (*lookup)(const char* name);
} AxModuleContext;

typedef int  (*ax_module_init_fn)(AxModuleContext* ctx);
typedef void (*ax_module_exit_fn)(void);

#pragma pack(pop)

void ax_module_init(void);
int  ax_module_load(const char* name);
int  ax_module_load_buffer(const char* name, const void* image, uint64_t size);
int  ax_module_unload(uint32_t index);
int  ax_module_publish(const char* name, void* symbol);
void* ax_module_lookup(const char* name);
uint32_t ax_module_count(void);
int  ax_module_fill_info(AxModInfo* out, uint32_t max);
uint32_t ax_module_state(uint32_t index);
const char* ax_module_name(uint32_t index);
uint64_t ax_module_base(uint32_t index);

/* Carga automatica de todos los modulos presentes en el initrd. */
void ax_module_autoload(void);

#endif /* AX_MODULE_H */
