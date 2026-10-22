/*
 * AxolynOS - Consola de user mode (escritura en los descriptores 1 y 2).
 *
 * Syscalls usadas (ver axos/abi.h):
 *
 *      AX_SYS_WRITE (1) rdi=fd rsi=buf rdx=len -> bytes escritos
 *      AX_SYS_READ  (2) rdi=fd rsi=buf rdx=len -> bytes leidos
 *
 * El descriptor 1 es la salida estandar (consola) y el 2 la salida de
 * error, igual que en un UNIX convencional.
 *
 * Convencion de retorno: >= 0 exito, < 0 error (-errno).
 */
#ifndef AXOS_CONSOLE_H
#define AXOS_CONSOLE_H

#include "axos/abi.h"
#include "axos/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AX_STDOUT 1
#define AX_STDERR 2

/* Escribe `len` bytes en el descriptor dado, reintentando si hace falta. */
long ax_console_write(int fd, const void* buf, unsigned long len);

/* Escribe una cadena terminada en '\0' (sin el terminador). */
long ax_console_puts(int fd, const char* text);

/* Escribe en la salida estandar (fd 1). */
void ax_console_print(const char* text);

/* Escribe en la salida estandar y anade un salto de linea. */
void ax_console_println(const char* text);

/* Imprime un entero con signo (sin salto de linea). */
void ax_console_print_int(long value);

/* Imprime un entero con signo y un salto de linea. */
void ax_console_println_int(long value);

/* Imprime un entero sin signo (sin salto de linea). */
void ax_console_print_uint(unsigned long value);

/* Imprime un entero sin signo y un salto de linea. */
void ax_console_println_uint(unsigned long value);

/* Imprime un entero en hexadecimal (0x..., sin salto de linea). */
void ax_console_print_hex(unsigned long value);

/* Imprime un entero en hexadecimal y un salto de linea. */
void ax_console_println_hex(unsigned long value);

/* Escribe un simple salto de linea. */
void ax_console_newline(void);

/* Lee una linea de la entrada estandar (fd 0) y la copia al buffer. */
long ax_console_read_line(char* buf, unsigned long max);

/* Nombre legible de un codigo de error (-errno). */
const char* ax_console_errno_name(int err);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AXOS_CONSOLE_H */
