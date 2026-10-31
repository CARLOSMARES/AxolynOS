/*
 * AxolynOS - Pantalla de arranque y barra de progreso.
 *
 * Mientras la splash esta activa la consola VGA esta silenciada: los logs
 * siguen saliendo por el puerto serie.  El dibujo se hace escribiendo
 * directamente celdas de la VGA para no interferir con el parser ANSI.
 */
#ifndef AX_SPLASH_H
#define AX_SPLASH_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void ax_splash_init(void);
void ax_splash_progress(int percent, const char* text);
void ax_splash_end(void);
bool ax_splash_active(void);
void ax_splash_dismiss(void);
void ax_splash_abort(void);

#ifdef __cplusplus
}
#endif

#endif /* AX_SPLASH_H */
