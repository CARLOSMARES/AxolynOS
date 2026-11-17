/*
 * AxolynOS - Archivos de sistema protegidos (.axsys).
 *
 * Un .axsys es un contenedor con metadatos y contenido binario que solo el
 * kernel puede leer.  Es el analogo de los ficheros .sys de Windows: se
 * usan para distribuir modulos, firmwares, controladores o datos de sistema
 * con informacion de version, fabricante y firma.
 *
 * Formato del fichero (todo empaquetado y en little-endian):
 *
 *   [AxSysFileHeader][AxSysFileMeta * meta_count][firma 32 bytes][contenido]
 *
 * La firma es el SHA-256 del contenido.  El kernel valida la cabecera, el
 * CRC del contenido y la firma antes de dar por bueno el fichero.
 *
 * Las aplicaciones de user mode pueden listar los ficheros cargados y leer
 * sus metadatos y su firma, pero nunca el contenido: ese queda en memoria
 * del kernel.
 *
 * Documentacion: manuales/15-axsys.md
 */
#ifndef AX_SYSFILE_H
#define AX_SYSFILE_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AX_SYSFILE_MAGIC       "AXSYS001"
#define AX_SYSFILE_MAGIC_LEN   8
#define AX_SYSFILE_VERSION     1

/* Banderas de la cabecera. */
#define AX_SYSFILE_FLAG_READONLY (1u << 0)  /* solo lectura            */
#define AX_SYSFILE_FLAG_KERNEL   (1u << 1)  /* contenido solo kernel  */
#define AX_SYSFILE_FLAG_SIGNED   (1u << 2)  /* firma SHA-256 valida   */

/* Tipo de contenido. */
#define AX_SYSFILE_TYPE_RAW     1
#define AX_SYSFILE_TYPE_ELF     2
#define AX_SYSFILE_TYPE_DATA    3
#define AX_SYSFILE_TYPE_CONFIG  4

#define AX_SYSFILE_MAX          16
#define AX_SYSFILE_MAX_META     8
#define AX_SYSFILE_META_KEY     16
#define AX_SYSFILE_META_VALUE   64
#define AX_SYSFILE_SIGN_SIZE    32

#if defined(__GNUC__)
#  define AX_SYSFILE_PACKED __attribute__((packed))
#else
#  define AX_SYSFILE_PACKED
#endif

/* Cabecera: 208 bytes. */
typedef struct AX_SYSFILE_PACKED {
    char     magic[AX_SYSFILE_MAGIC_LEN];
    uint16_t version;
    uint16_t flags;
    uint32_t meta_count;
    uint32_t content_type;
    uint64_t content_size;
    uint64_t content_crc32;   /* CRC-32 del contenido                     */
    uint64_t header_crc32;    /* CRC-32 de la cabecera con este campo a 0 */
    char     name[32];
    char     product[32];
    char     manufacturer[32];
    char     description[64];
    uint32_t reserved;
} AxSysFileHeader;

/* Entrada de metadatos: 80 bytes. */
typedef struct AX_SYSFILE_PACKED {
    char key[AX_SYSFILE_META_KEY];
    char value[AX_SYSFILE_META_VALUE];
} AxSysFileMeta;

/* Inicializa la tabla de ficheros cargados. */
void ax_sysfile_init(void);

/*
 * Carga un .axsys desde el VFS (initrd) o desde AxFS-D.  Valida la cabecera,
 * el CRC y la firma.  Devuelve el indice (>= 0) o -errno.
 */
int  ax_sysfile_load(const char* path);

/* Descarga un fichero por indice. */
int  ax_sysfile_unload(uint32_t index);

/* Numero de ficheros cargados. */
int  ax_sysfile_count(void);

/* Informacion publica de un fichero (metadatos, no el contenido). */
int  ax_sysfile_info(uint32_t index, AxSysFileInfo* out);

/* Lee un metadato por clave (sensible a mayusculas como Windows). */
int  ax_sysfile_meta(uint32_t index, const char* key, char* out, uint64_t max);

/* Lee un metadato por indice (para enumerar las entradas). */
int  ax_sysfile_meta_at(uint32_t index, uint32_t meta_index,
                        char* key, uint64_t key_max,
                        char* value, uint64_t value_max);

/* Lista los ficheros cargados en formato texto (lineas "nombre  producto"). */
int  ax_sysfile_list(char* out, uint64_t max);

/* Carga automaticamente los .axsys del initrd en /system. */
void ax_sysfile_autoload(void);

#ifdef __cplusplus
}
#endif

#endif /* AX_SYSFILE_H */
