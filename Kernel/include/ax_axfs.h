/*
 * AxolynOS - Sistema de ficheros en memoria (AXFS) usado por el initrd.
 *
 * El initrd lo genera la herramienta de host scripts/tools/mkinitramfs.cpp
 * y lo carga el bootloader (stage2) en AX_INITRD_PHYS.
 */
#ifndef AX_AXFS_H
#define AX_AXFS_H

#include "ax_types.h"

#define AXFS_MAGIC     "AXFS0001"
#define AXFS_MAGIC_LEN 8
#define AXFS_VERSION   1

/* Longitud maxima del nombre de una entrada (campo name de AxfsEntry). */
#define AXFS_NAME_MAX  64

#define AXFS_TYPE_FILE 1
#define AXFS_TYPE_DIR  2

#pragma pack(push, 1)
typedef struct {
    char     magic[AXFS_MAGIC_LEN];
    uint32_t version;
    uint32_t count;
    uint64_t index_off;
    uint64_t data_off;
    uint64_t total_size;
    uint32_t flags;
    uint32_t reserved;
} AxfsHeader;

typedef struct {
    char     name[64];
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t size;
    uint32_t parent;
    uint32_t reserved;
} AxfsEntry;
#pragma pack(pop)

void ax_vfs_init(void);
const void* ax_vfs_read(const char* path, uint64_t* size_out);
int  ax_vfs_exists(const char* path);
int  ax_vfs_list(char* out, uint64_t max);   /* lista separada por \n */
uint32_t ax_vfs_count(void);
const char* ax_vfs_name(uint32_t index);

/* Entrada completa del initrd (nombre, tipo y tamano).  Devuelve nullptr si
   el indice esta fuera de rango.  La usa el instalador para copiar todo el
   arbol al disco y la shell para listar sin volumen montado. */
const AxfsEntry* ax_vfs_entry(uint32_t index);

#endif /* AX_AXFS_H */
