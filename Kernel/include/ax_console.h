/*
 * AxolynOS - Consola de video (VGA texto) y salida por puerto serie.
 */
#ifndef AX_CONSOLE_H
#define AX_CONSOLE_H

#include "ax_types.h"

void ax_console_init(void);
void ax_console_clear(void);
void ax_console_set_cursor(int row, int col);
void ax_putc(char c);
void ax_puts(const char* s);
void ax_puthex(uint64_t value);
void ax_putdec(int64_t value);

void ax_serial_init(void);
void ax_serial_putc(char c);
void ax_serial_puts(const char* s);

/* Lectura del puerto serie (entrada estandar del kernel). */
char ax_console_getc(void);
bool ax_console_getc_ready(void);

/* Entrada combinada teclado PS/2 + serie.  trygetc() no bloquea. */
bool ax_console_trygetc(char* out);
char ax_console_read(void);

void ax_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void ax_vprintf(const char* fmt, va_list args);

/*
 * Control de la VGA para la pantalla de arranque.
 *
 * Mientras la VGA esta silenciada, ax_putc() sigue enviando todo al puerto
 * serie (los logs no se pierden) pero no escribe en la memoria de video.
 * Las funciones raw permiten dibujar la splash sin pasar por el parser ANSI.
 */
void ax_console_set_vga_muted(bool muted);
bool ax_console_vga_muted(void);
void ax_console_force_vga(void);
void ax_console_raw_clear(uint8_t attr);
void ax_console_raw_put(int row, int col, char c, uint8_t attr);
void ax_console_raw_text(int row, int col, const char* text, uint8_t attr);
void ax_console_raw_cursor(int row, int col);

#endif /* AX_CONSOLE_H */
