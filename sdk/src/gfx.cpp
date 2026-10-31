/*
 * AxolynOS SDK - manejo de graficos basico
 */
#include "../include/axos/sdk.h"

/* Inicializar modo grafico (framebuffer) */
int sdk_gfx_init(int width, int height)
{
    sdk_print("GFX: Inicializando modo %dx%d\n", width, height);
    return 0;
}

/* Pintar un pixel */
void sdk_gfx_pixel(int x, int y, int r, int g, int b)
{
    sdk_print("GFX: Pixel (%d,%d) color RGB(%d,%d,%d)\n", x, y, r, g, b);
}

/* Dibujar una linea */
void sdk_gfx_line(int x1, int y1, int x2, int y2, int r, int g, int b)
{
    sdk_print("GFX: Linea (%d,%d)->(%d,%d) color RGB(%d,%d,%d)\n", 
              x1, y1, x2, y2, r, g, b);
}

/* Dibujar un rectangulo */
void sdk_gfx_rect(int x, int y, int w, int h, int r, int g, int b)
{
    sdk_print("GFX: Rectangulo (%d,%d) %dx%d color RGB(%d,%d,%d)\n", 
              x, y, w, h, r, g, b);
}

/* Limpiar pantalla */
void sdk_gfx_clear(void)
{
    sdk_print("GFX: Limpiando pantalla\n");
}