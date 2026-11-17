/*
 * AxolynOS - Configuracion de aplicaciones (.axc) para user mode.
 *
 * Syscalls usadas (ver axos/abi.h):
 *
 *      AX_SYS_AXC_LIST     (63) rdi=indice rsi=AxcInfo*
 *      AX_SYS_AXC_LOAD     (64) rdi=ruta -> indice | -errno
 *      AX_SYS_AXC_CONN     (65) rdi=indice rsi=conexion rdx=AxcConnection*
 *      AX_SYS_AXC_FIND     (66) rdi=nombre -> indice | -errno
 *      AX_SYS_AXC_CONNECTION (67) rdi=nombre rsi=tipo rdx=buf r10=max
 */
#ifndef AXOS_AXC_H
#define AXOS_AXC_H

#include "axos/abi.h"
#include "axos/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Informacion de una aplicacion cargada. */
long ax_axc_list(unsigned int index, AxcInfo* out);

/* Carga un fichero .axc.  Devuelve el indice o -errno. */
long ax_axc_load(const char* path);

/* Conexion "i-esima" de una aplicacion. */
long ax_axc_conn(unsigned int index, unsigned int conn_index,
                 AxcConnection* out);

/* Busca una aplicacion por nombre.  Devuelve el indice o -errno. */
long ax_axc_find(const char* name);

/* Busca una conexion concreta ("run", "open", ...) de una aplicacion. */
long ax_axc_connection(const char* name, const char* key,
                       char* out, unsigned long max);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace axos {

/* Version C++ de AxcInfo. */
struct AppConfig {
    char   name[AX_AXC_NAME_LEN];
    char   version[AX_AXC_VER_LEN];
    char   publisher[AX_AXC_PUB_LEN];
    char   exec[AX_AXC_EXEC_LEN];
    char   icon[AX_AXC_ICON_LEN];
    char   description[AX_AXC_DESC_LEN];
    ax_u32 conn_count;
    long   error;
};

/* Version C++ de AxcConnection. */
struct AppConnection {
    char key[AX_AXC_KEY_LEN];
    char value[AX_AXC_CONN_LEN];
    long error;
};

inline AppConfig axc_list(unsigned int index) noexcept
{
    AppConfig  a{};
    AxcInfo    raw{};
    long       r = ax_axc_list(index, &raw);
    a.error = r;
    if (r >= 0) {
        for (int i = 0; i < AX_AXC_NAME_LEN; ++i)   a.name[i] = raw.name[i];
        for (int i = 0; i < AX_AXC_VER_LEN; ++i)    a.version[i] = raw.version[i];
        for (int i = 0; i < AX_AXC_PUB_LEN; ++i)    a.publisher[i] = raw.publisher[i];
        for (int i = 0; i < AX_AXC_EXEC_LEN; ++i)   a.exec[i] = raw.exec[i];
        for (int i = 0; i < AX_AXC_ICON_LEN; ++i)   a.icon[i] = raw.icon[i];
        for (int i = 0; i < AX_AXC_DESC_LEN; ++i)   a.description[i] = raw.description[i];
        a.conn_count = raw.conn_count;
    }
    return a;
}

inline long axc_load(const char* path) noexcept
{
    return ax_axc_load(path);
}

inline AppConnection axc_conn(unsigned int index, unsigned int conn_index) noexcept
{
    AppConnection c{};
    AxcConnection raw{};
    long r = ax_axc_conn(index, conn_index, &raw);
    c.error = r;
    if (r >= 0) {
        for (int i = 0; i < AX_AXC_KEY_LEN; ++i)   c.key[i] = raw.key[i];
        for (int i = 0; i < AX_AXC_CONN_LEN; ++i) c.value[i] = raw.value[i];
    }
    return c;
}

inline long axc_find(const char* name) noexcept
{
    return ax_axc_find(name);
}

inline long axc_connection(const char* name, const char* key,
                           char* out, unsigned long max) noexcept
{
    return ax_axc_connection(name, key, out, max);
}

} /* namespace axos */

#endif /* __cplusplus */

#endif /* AXOS_AXC_H */
