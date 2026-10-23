/*
 * AxolynOS - Enumeración PCI básica
 */
#ifndef AX_PCI_H
#define AX_PCI_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AX_PCI_MAX_DEVICES 256

typedef struct {
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  func;
    uint16_t vendor;
    uint16_t device;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    uint8_t  header_type;
    uint32_t bar[6];
    uint8_t  irq;
    bool     present;
} AxPciDevice;

uint32_t ax_pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
uint16_t ax_pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
uint8_t  ax_pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
void     ax_pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val);
void     ax_pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint16_t val);
void     ax_pci_write8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint8_t val);

void         ax_pci_init(void);
int          ax_pci_count(void);
AxPciDevice* ax_pci_get(int idx);

#ifdef __cplusplus
}
#endif

#endif /* AX_PCI_H */
