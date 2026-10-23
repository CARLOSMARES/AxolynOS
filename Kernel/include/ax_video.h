/*
 * AxolynOS - Driver de video (VESA/VBE / framebuffer - esqueleto)
 */
#ifndef AX_VIDEO_H
#define AX_VIDEO_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    void*    framebuffer;
} AxVideoMode;

void ax_video_init(void);
int  ax_video_get_mode(AxVideoMode* mode);
int  ax_video_set_pixel(uint32_t x, uint32_t y, uint32_t color);
void ax_video_clear(uint32_t color);

#ifdef __cplusplus
}
#endif

#endif /* AX_VIDEO_H */
