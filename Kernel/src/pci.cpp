/*
 * AxolynOS - Enumeración PCI
 */
#include "ax_pci.h"
#include "ax_console.h"
#include "ax_io.h"

AxPciDevice g_pci_devices[AX_PCI_MAX_DEVICES];
int g_pci_count = 0;

uint32_t ax_pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t addr = (uint32_t)0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (off & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

uint16_t ax_pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t v = ax_pci_read32(bus, dev, func, off & 0xFC);
    return (uint16_t)((v >> ((off & 2) * 8)) & 0xFFFF);
}

uint8_t ax_pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t v = ax_pci_read32(bus, dev, func, off & 0xFC);
    return (uint8_t)((v >> ((off & 3) * 8)) & 0xFF);
}

void ax_pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val)
{
    uint32_t addr = (uint32_t)0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (off & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, val);
}

void ax_pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint16_t val)
{
    uint32_t v = ax_pci_read32(bus, dev, func, off & 0xFC);
    uint32_t shift = (off & 2) * 8;
    v = (v & ~(0xFFFF << shift)) | ((uint32_t)val << shift);
    ax_pci_write32(bus, dev, func, off & 0xFC, v);
}

void ax_pci_write8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint8_t val)
{
    uint32_t v = ax_pci_read32(bus, dev, func, off & 0xFC);
    uint32_t shift = (off & 3) * 8;
    v = (v & ~(0xFF << shift)) | ((uint32_t)val << shift);
    ax_pci_write32(bus, dev, func, off & 0xFC, v);
}

void ax_pci_init(void)
{
    g_pci_count = 0;
    for (int bus = 0; bus < 256 && g_pci_count < AX_PCI_MAX_DEVICES; ++bus) {
        for (int dev = 0; dev < 32 && g_pci_count < AX_PCI_MAX_DEVICES; ++dev) {
            for (int func = 0; func < 8 && g_pci_count < AX_PCI_MAX_DEVICES; ++func) {
                uint16_t vendor = ax_pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)func, 0x00);
                if (vendor == 0xFFFF) {
                    if (func == 0) break;
                    continue;
                }
                AxPciDevice* d = &g_pci_devices[g_pci_count++];
                d->bus = (uint8_t)bus;
                d->dev = (uint8_t)dev;
                d->func = (uint8_t)func;
                d->vendor = vendor;
                d->device = ax_pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)func, 0x02);
                d->class_code = ax_pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, 0x0B);
                d->subclass = ax_pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, 0x0A);
                d->prog_if = ax_pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, 0x09);
                d->revision = ax_pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, 0x08);
                d->header_type = ax_pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, 0x0E);
                d->irq = ax_pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, 0x3C);
                for (int i = 0; i < 6; ++i) {
                    d->bar[i] = ax_pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)func, 0x10 + i * 4);
                }
                d->present = true;
                if (func == 0 && !(d->header_type & 0x80)) break;
            }
        }
    }
    ax_printf("[pci] %d dispositivos encontrados\n", g_pci_count);
}

int ax_pci_count(void) { return g_pci_count; }
AxPciDevice* ax_pci_get(int idx) { if (idx < 0 || idx >= g_pci_count) return nullptr; return &g_pci_devices[idx]; }
