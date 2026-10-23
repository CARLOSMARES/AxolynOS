/*
 * AxolynOS - Abstracción de disco (IDE/AHCI básico)
 */
#ifndef AX_DISK_H
#define AX_DISK_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AX_DISK_MAX_DRIVES 4

typedef struct {
    char     name[16];
    uint64_t sectors;      /* total de sectores (512 bytes) */
    uint32_t sector_size;
    bool     present;
    bool     is_ata;
    bool     is_atapi;
} AxDiskInfo;

void ax_disk_init(void);
int  ax_disk_count(void);
int  ax_disk_get_info(int drive, AxDiskInfo* info);
int  ax_disk_read(int drive, uint64_t lba, uint32_t count, void* buf);
int  ax_disk_write(int drive, uint64_t lba, uint32_t count, const void* buf);

#ifdef __cplusplus
}
#endif

#endif /* AX_DISK_H */
