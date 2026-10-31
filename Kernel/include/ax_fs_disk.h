/*
 * AxolynOS - AxFS en disco (formateador, montaje y ficheros).
 *
 * AxFS-D es un sistema de ficheros sencillo pensado para el instalador:
 *
 *   - Superbloque de 512 bytes en AXFS_DISK_LBA.
 *   - Tabla de 128 entradas de 64 bytes (8 KiB).
 *   - Bitmap de bloques de 4 KiB (8 sectores, hasta 32768 bloques).
 *   - Area de datos con bloques de 4 KiB.
 *
 * Cada fichero ocupa una extension contigua de bloques.  Es suficiente para
 * un disco de instalacion y evita la complejidad de un arbol de nodos.
 * Los nombres son rutas relativas sin '/' inicial ("docs/nota.txt").
 */
#ifndef AX_FS_DISK_H
#define AX_FS_DISK_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AXFS_DISK_MAGIC        "AXFSD002"
#define AXFS_DISK_MAGIC_LEN    8
#define AXFS_DISK_VERSION      2

/* Primer LBA del area AxFS.  Los primeros 4 MiB quedan para el cargador, el
   kernel y el initrd. */
#define AXFS_DISK_LBA          8192ull
#define AXFS_DISK_BLOCK_SIZE   4096u
#define AXFS_DISK_BLOCK_SECT   (AXFS_DISK_BLOCK_SIZE / 512u)
#define AXFS_DISK_MAX_ENTRIES  128
#define AXFS_DISK_NAME_MAX     40
#define AXFS_DISK_BITMAP_SECT  8u
#define AXFS_DISK_MAX_BLOCKS   (AXFS_DISK_BITMAP_SECT * 512u * 8u)

#define AXFS_DISK_TYPE_FREE   0
#define AXFS_DISK_TYPE_FILE   1
#define AXFS_DISK_TYPE_DIR    2

#pragma pack(push, 1)
typedef struct {
    char     magic[AXFS_DISK_MAGIC_LEN];
    uint32_t version;
    uint32_t block_size;
    uint64_t total_sectors;
    uint64_t block_count;      /* bloques de datos del volumen */
    uint64_t data_lba;
    uint64_t data_blocks;
    uint64_t free_blocks;
    uint64_t entry_lba;
    uint64_t bitmap_lba;
    uint32_t entry_count;
    uint32_t entry_max;
    uint32_t bitmap_sectors;
    uint32_t flags;
    uint32_t checksum;
    uint32_t reserved;
} AxfsDiskSuper;

typedef struct {
    char     name[AXFS_DISK_NAME_MAX];
    uint32_t type;             /* AXFS_DISK_TYPE_* */
    uint32_t blocks;           /* bloques reservados */
    uint64_t first_block;      /* indice de bloque (no LBA) */
    uint64_t size;             /* bytes utiles */
} AxfsDiskEntry;
#pragma pack(pop)

void ax_fs_disk_init(void);
int  ax_fs_disk_format(int drive);
int  ax_fs_disk_mount(int drive);
int  ax_fs_disk_unmount(void);
bool ax_fs_disk_mounted(void);
int  ax_fs_disk_drive(void);

int  ax_fs_disk_mkdir(const char* path);
int  ax_fs_disk_create(const char* path);
int  ax_fs_disk_write(const char* path, const void* data, uint64_t size);
int  ax_fs_disk_read(const char* path, void* buf, uint64_t size, uint64_t* out);
/* Lee un fragmento sin cargar el fichero completo (cat, tail, grep, ...). */
int  ax_fs_disk_read_at(const char* path, uint64_t offset, void* buf,
                        uint64_t size, uint64_t* out);
int  ax_fs_disk_unlink(const char* path);
int  ax_fs_disk_rmdir(const char* path);
int  ax_fs_disk_rename(const char* old_path, const char* new_path);
int  ax_fs_disk_stat(const char* path, uint32_t* type, uint64_t* size);
int  ax_fs_disk_list(const char* path, char* out, uint64_t max);
int  ax_fs_disk_listdir(const char* path, char* out, uint64_t max);

/*
 * Enumera las entradas directas de un directorio sin formatearlas.
 * "index" empieza en 0 y se incrementa hasta que la funcion devuelve
 * AX_ENOENT.  El nombre devuelto es el nombre corto (sin ruta).
 */
typedef struct {
    char     name[AXFS_DISK_NAME_MAX];
    uint32_t type;             /* AXFS_DISK_TYPE_* */
    uint64_t size;
} AxfsDiskDirEntry;

int  ax_fs_disk_readdir(const char* path, uint32_t index, AxfsDiskDirEntry* out);

/* Totales del volumen montado (bytes de datos y libres). */
uint64_t ax_fs_disk_total_bytes(void);
uint64_t ax_fs_disk_free_bytes(void);
uint32_t ax_fs_disk_entry_count(void);

#ifdef __cplusplus
}
#endif

#endif /* AX_FS_DISK_H */
