/*
 * AxolynOS - Driver de video (esqueleto)
 */
#include "ax_video.h"
#include "ax_console.h"

static AxVideoMode g_video_mode;

void ax_video_init(void)
{
    g_video_mode.width = 80;
    g_video_mode.height = 25;
    g_video_mode.pitch = 160;
    g_video_mode.bpp = 16;
    g_video_mode.framebuffer = (void*)0xB8000;
    ax_printf("[video] modo de video inicializado\n");
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
    return -1;
}

void ax_video_clear(uint32_t color)
{
    (void)color;
}
