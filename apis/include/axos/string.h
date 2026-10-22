/*
 * AxolynOS - Utilidades de cadenas y memoria para user mode.
 *
 * La libreria se compila con -ffreestanding -nostdlib, por lo que no hay
 * libc disponible. Estas son las implementaciones propias que sustituyen
 * a memcpy/memset/memmove/memcmp/strlen/strcmp/strcpy. El compilador
 * puede generar llamadas a memcpy/memset al optimizar, por eso los
 * simbolos se definen con los nombres habituales.
 *
 * Convencion de retorno: 0 en exito, -1 si la ejecucion se interrumpe.
 */
#ifndef AXOS_STRING_H
#define AXOS_STRING_H

#include "axos/abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Nombres habituales (los puede generar el compilador) --------- */
void*       memcpy(void* dst, const void* src, unsigned long n);
void*       memmove(void* dst, const void* src, unsigned long n);
void*       memset(void* dst, int value, unsigned long n);
int         memcmp(const void* a, const void* b, unsigned long n);
unsigned long strlen(const char* s);
int         strcmp(const char* a, const char* b);
char*       strcpy(char* dst, const char* src);

/* ---- Versiones con prefijo ax_ (uso recomendado en codigo nuevo) -- */
void*       ax_memcpy(void* dst, const void* src, unsigned long n);
void*       ax_memset(void* dst, int value, unsigned long n);
unsigned long ax_strlen(const char* s);
int         ax_strcmp(const char* a, const char* b);
char*       ax_strcpy(char* dst, const char* src);

/*
 * Convierte un entero con signo en texto decimal.
 *   buffer : destino (debe caber el numero y el terminador).
 *   size   : capacidad del buffer.
 * Devuelve la longitud del texto generado (sin el terminador) o -1 si
 * no cabe.
 */
long ax_itoa(long value, char* buffer, unsigned long size);

/* Convierte un entero sin signo en texto hexadecimal. */
long ax_utoa_hex(unsigned long value, char* buffer, unsigned long size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AXOS_STRING_H */
