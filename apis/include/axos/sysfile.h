/*
 * AxolynOS - Archivos de sistema protegidos (.axsys) para aplicaciones.
 *
 * Syscalls usadas (ver axos/abi.h):
 *
 *      AX_SYS_SYSFILE_LIST (55) rdi=indice rsi=AxSysFileInfo*
 *      AX_SYS_SYSFILE_LOAD (56) rdi=ruta -> indice | -errno
 *      AX_SYS_SYSFILE_META (57) rdi=indice rsi=clave rdx=buf r10=max
 *
 * El contenido de un .axsys solo es legible por el kernel: las aplicaciones
 * pueden consultar los metadatos y la firma, pero nunca los bytes.
 */
#ifndef AXOS_SYSFILE_H
#define AXOS_SYSFILE_H

#include "axos/abi.h"
#include "axos/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Informacion publica de un archivo cargado. */
long ax_sysfile_list(unsigned int index, AxSysFileInfo* out);

/* Carga un .axsys (initrd o AxFS-D).  Devuelve el indice o -errno. */
long ax_sysfile_load(const char* path);

/* Lee un metadato por clave. */
long ax_sysfile_meta(unsigned int index, const char* key,
                     char* out, unsigned long max);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace axos {

struct SysFileInfo {
    ax_u32 index;
    ax_u32 flags;
    ax_u32 content_type;
    ax_u64 content_size;
    ax_u64 content_crc32;
    ax_u32 meta_count;
    ax_u32 signature_ok;
    char   name[AX_SYSFILE_NAME_LEN];
    char   product[AX_SYSFILE_PRODUCT_LEN];
    char   manufacturer[AX_SYSFILE_MANUF_LEN];
    char   description[AX_SYSFILE_DESC_LEN];
    long   error;
};

inline SysFileInfo sysfile_list(unsigned int index) noexcept
{
    SysFileInfo   s{};
    AxSysFileInfo raw{};
    long          r = ax_sysfile_list(index, &raw);
    s.error = r;
    if (r >= 0) {
        s.index        = raw.index;
        s.flags        = raw.flags;
        s.content_type = raw.content_type;
        s.content_size = raw.content_size;
        s.content_crc32 = raw.content_crc32;
        s.meta_count   = raw.meta_count;
        s.signature_ok = raw.signature_ok;
        for (int i = 0; i < AX_SYSFILE_NAME_LEN; ++i) {
            s.name[i] = raw.name[i];
        }
        for (int i = 0; i < AX_SYSFILE_PRODUCT_LEN; ++i) {
            s.product[i] = raw.product[i];
        }
        for (int i = 0; i < AX_SYSFILE_MANUF_LEN; ++i) {
            s.manufacturer[i] = raw.manufacturer[i];
        }
        for (int i = 0; i < AX_SYSFILE_DESC_LEN; ++i) {
            s.description[i] = raw.description[i];
        }
    }
    return s;
}

inline long sysfile_load(const char* path) noexcept
{
    return ax_sysfile_load(path);
}

inline long sysfile_meta(unsigned int index, const char* key,
                         char* out, unsigned long max) noexcept
{
    return ax_sysfile_meta(index, key, out, max);
}

} /* namespace axos */

#endif /* __cplusplus */

#endif /* AXOS_SYSFILE_H */
