/*
 * AxolynOS - Registro del sistema (estilo Windows).
 *
 * El registro es un servicio del kernel con claves planas de tipo
 * cadena/entero.  Se persiste en AxFS-D (/system/registry.dat) y se expone
 * al shell con `reg`/`regedit` y a user mode con las syscalls AX_SYS_REG_*.
 *
 * La implementacion es propia y freestanding: no usa heap ni libc.
 */
#ifndef AX_REGISTRY_H
#define AX_REGISTRY_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AX_REG_MAX_ENTRIES  128
#define AX_REG_PATH_MAX     96
#define AX_REG_VALUE_MAX    256

#define AX_REG_TYPE_STRING  1u
#define AX_REG_TYPE_INT     2u

#define AX_REG_HKLM         "HKLM"
#define AX_REG_HKCU         "HKCU"

typedef struct {
    char     path[AX_REG_PATH_MAX];
    char     value[AX_REG_VALUE_MAX];
    uint32_t type;
    uint32_t value_len;
} AxRegEntry;

void ax_registry_init(void);
int  ax_registry_get(const char* path, char* out, uint64_t max, uint32_t* type);
int  ax_registry_set(const char* path, const char* value, uint32_t type);
int  ax_registry_set_int(const char* path, int64_t value);
int  ax_registry_del(const char* path);
int  ax_registry_enum(uint32_t index, AxRegEntry* out);
int  ax_registry_count(void);
int  ax_registry_save(void);
bool ax_registry_exists(const char* path);
const char* ax_registry_type_name(uint32_t type);

#ifdef __cplusplus
}
#endif

#endif /* AX_REGISTRY_H */
