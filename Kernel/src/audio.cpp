/*
 * AxolynOS - Driver de audio
 */
#include "ax_audio.h"
#include "ax_console.h"
#include "ax_pci.h"

void ax_audio_init(void)
{
    ax_printf("[audio] inicializando driver de audio...\n");
    /* Busqueda básica de dispositivos de audio */
    for (uint32_t bus = 0; bus < 256; ++bus) {
        for (uint32_t dev = 0; dev < 32; ++dev) {
            for (uint32_t func = 0; func < 8; ++func) {
                uint32_t v = ax_pci_read32(bus, dev, func, 0x00);
                uint16_t vendor = (uint16_t)(v & 0xFFFF);
                uint16_t device = (uint16_t)(v >> 16);
                if (vendor == 0xFFFF) continue;
                uint32_t c = ax_pci_read32(bus, dev, func, 0x08);
                uint8_t class_code = (uint8_t)(c >> 24);
                uint8_t subclass = (uint8_t)(c >> 16);
                if (class_code == 0x04) { /* Multimedia */
                    ax_printf("[audio] PCI %02x:%02x.%x vendor=%04x dev=%04x class=04/%02x\n", bus, dev, func, vendor, device, subclass);
                }
            }
        }
    }
    ax_printf("[audio] driver listo (AC97/HDA: deteccion basica)\n");
}

int ax_audio_play(const void* data, uint64_t size)
{
    (void)data; (void)size;
    /* Implementacion minima: sin hardware real aún */
    return 0;
}

int ax_audio_set_volume(uint8_t vol)
{
    (void)vol;
    return 0;
}
