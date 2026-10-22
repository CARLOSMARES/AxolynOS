/*
 * AxolynOS - Consola de video (VGA texto) y salida por puerto serie.
 */
#ifndef AX_CONSOLE_H
#define AX_CONSOLE_H

#include "ax_types.h"

void ax_console_init(void);
void ax_console_clear(void);
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

void ax_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void ax_vprintf(const char* fmt, va_list args);

#endif /* AX_CONSOLE_H */
