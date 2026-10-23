/*
 * AxolynOS - Driver AHCI básico (esqueleto funcional para SSD/M.2 SATA)
 */
#ifndef AX_AHCI_H
#define AX_AHCI_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void ax_ahci_init(void);
int  ax_ahci_read_lba(uint64_t lba, uint32_t count, void* buf);
int  ax_ahci_write_lba(uint64_t lba, uint32_t count, const void* buf);

#ifdef __cplusplus
}
#endif

#endif /* AX_AHCI_H */
