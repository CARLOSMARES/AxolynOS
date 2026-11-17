/*
 * AxolynOS - Ficheros INI para aplicaciones de user mode.
 *
 * Syscalls usadas (ver axos/abi.h):
 *
 *      AX_SYS_INI_PARSE (58) rdi=ruta -> entradas | -errno
 *      AX_SYS_INI_GET   (59) rdi=seccion rsi=clave rdx=buf r10=max
 *      AX_SYS_INI_SET   (60) rdi=seccion rsi=clave rdx=valor
 *      AX_SYS_INI_ENUM  (61) rdi=indice rsi=AxIniEntry*
 *      AX_SYS_INI_SAVE  (62) rdi=ruta
 *
 * Las claves y secciones no distinguen mayusculas, igual que en Windows.
 */
#ifndef AXOS_INI_H
#define AXOS_INI_H

#include "axos/abi.h"
#include "axos/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Carga y analiza un fichero INI.  Devuelve el numero de entradas o -errno. */
long ax_ini_parse(const char* path);

/* Obtiene el valor de una clave. */
long ax_ini_get(const char* section, const char* key,
                char* out, unsigned long max);

/* Crea o actualiza una clave en la tabla en memoria. */
long ax_ini_set(const char* section, const char* key, const char* value);

/* Enumeracion de entradas. */
long ax_ini_enum(unsigned int index, AxIniEntry* out);

/* Guarda la tabla en un fichero INI (AxFS-D). */
long ax_ini_save(const char* path);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace axos {

struct IniEntry {
    char section[AX_INI_KEY_LEN];
    char key[AX_INI_KEY_LEN];
    char value[AX_INI_VALUE_LEN];
    long error;
};

inline long ini_parse(const char* path) noexcept
{
    return ax_ini_parse(path);
}

inline long ini_get(const char* section, const char* key,
                    char* out, unsigned long max) noexcept
{
    return ax_ini_get(section, key, out, max);
}

inline long ini_set(const char* section, const char* key,
                    const char* value) noexcept
{
    return ax_ini_set(section, key, value);
}

inline IniEntry ini_enum(unsigned int index) noexcept
{
    IniEntry   e{};
    AxIniEntry raw{};
    long       r = ax_ini_enum(index, &raw);
    e.error = r;
    if (r >= 0) {
        for (int i = 0; i < AX_INI_KEY_LEN; ++i) {
            e.section[i] = raw.section[i];
            e.key[i]     = raw.key[i];
        }
        for (int i = 0; i < AX_INI_VALUE_LEN; ++i) {
            e.value[i] = raw.value[i];
        }
    }
    return e;
}

inline long ini_save(const char* path) noexcept
{
    return ax_ini_save(path);
}

} /* namespace axos */

#endif /* __cplusplus */

#endif /* AXOS_INI_H */
