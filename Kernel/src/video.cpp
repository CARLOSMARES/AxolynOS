/*
 * AxolynOS - Driver de video
 */
#include "ax_video.h"
#include "ax_console.h"
#include "ax_pci.h"

static AxVideoMode g_video_mode;

void ax_video_init(void)
{
    g_video_mode.width = 80;
    g_video_mode.height = 25;
    g_video_mode.pitch = 160;
    g_video_mode.bpp = 16;
    g_video_mode.framebuffer = (void*)0xB8000;
    ax_printf("[video] modo de video inicializado (VGA texto)\n");
    /* Detección VGA/Bochs/VESA básica */
    for (uint32_t bus = 0; bus < 256; ++bus) {
        for (uint32_t dev = 0; dev < 32; ++dev) {
            for (uint32_t func = 0; func < 8; ++func) {
                uint32_t v = ax_pci_read32(bus, dev, func, 0x00);
                uint16_t vendor = (uint16_t)(v & 0xFFFF);
                uint16_t device = (uint16_t)(v >> 16);
                if (vendor == 0xFFFF) continue;
                uint32_t c = ax_pci_read32(bus, dev, func, 0x08);
                uint8_t class_code = (uint8_t)(c >> 24);
                if (class_code == 0x03) { /* Display controller */
                    uint8_t subclass = (uint8_t)(c >> 16);
                    ax_printf("[video] PCI %02x:%02x.%x vendor=%04x dev=%04x class=03/%02x\n", bus, dev, func, vendor, device, subclass);
                }
            }
        }
    }
}

int ax_video_get_mode(AxVideoMode* mode)
{
    if (!mode) return -1;
    *mode = g_video_mode;
    return 0;
}

int ax_video_set_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    (void)x; (void)y; (void)color;
    /* TODO: modo grafico framebuffer */
    return -1;
}

void ax_video_clear(uint32_t color)
{
    (void)color;
    /* limpiar consola texto */
    /* consola ya maneja esto */
}
