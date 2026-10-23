/*
 * AxolynOS - Sistema de ficheros en disco AxFS (desde cero)
 */
#ifndef AX_FS_DISK_H
#define AX_FS_DISK_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AXFS_DISK_MAGIC "AXFSD0001"
#define AXFS_DISK_VERSION 1

void ax_fs_disk_init(void);
int  ax_fs_disk_format(int drive);
int  ax_fs_disk_mount(int drive);
int  ax_fs_disk_mkdir(const char* path);
int  ax_fs_disk_create(const char* path);
int  ax_fs_disk_write(const char* path, const void* data, uint64_t size);
int  ax_fs_disk_read(const char* path, void* buf, uint64_t size, uint64_t* out);
int  ax_fs_disk_unlink(const char* path);
int  ax_fs_disk_list(const char* path, char* out, uint64_t max);

#ifdef __cplusplus
}
#endif

#endif /* AX_FS_DISK_H */
