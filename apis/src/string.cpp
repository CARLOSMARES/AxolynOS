/*
 * AxolynOS - Utilidades de cadenas y memoria (implementacion).
 *
 * La libreria se compila sin libc (-ffreestanding -nostdlib), asi que
 * aquí se implementan a mano las funciones que el compilador puede
 * generar por su cuenta (memcpy, memset, memmove, memcmp) y las que
 * usan las aplicaciones (strlen, strcmp, strcpy, itoa).
 *
 * Nota: el pragma evita que el optimizador transforme estos bucles en
 * llamadas a si mismos (recursion infinita).
 */
#pragma GCC optimize("no-tree-loop-distribute-patterns")

#include "axos/string.h"
#include "axos/syscall.h"

/* ------------------------------------------------------------------ */
/* Nombres habituales                                                 */
/* ------------------------------------------------------------------ */

void* memcpy(void* dst, const void* src, unsigned long n)
{
    unsigned char*       d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;

    for (unsigned long i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dst;
}

void* memmove(void* dst, const void* src, unsigned long n)
{
    unsigned char*       d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;

    if (d == s || n == 0) {
        return dst;
    }
    if (d < s) {
        for (unsigned long i = 0; i < n; ++i) {
            d[i] = s[i];
        }
    } else {
        for (unsigned long i = n; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }
    return dst;
}

void* memset(void* dst, int value, unsigned long n)
{
    unsigned char* d = (unsigned char*)dst;

    for (unsigned long i = 0; i < n; ++i) {
        d[i] = (unsigned char)value;
    }
    return dst;
}

int memcmp(const void* a, const void* b, unsigned long n)
{
    const unsigned char* pa = (const unsigned char*)a;
    const unsigned char* pb = (const unsigned char*)b;

    for (unsigned long i = 0; i < n; ++i) {
        if (pa[i] != pb[i]) {
            return (int)pa[i] - (int)pb[i];
        }
    }
    return 0;
}

unsigned long strlen(const char* s)
{
    unsigned long n = 0;

    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

int strcmp(const char* a, const char* b)
{
    unsigned long i = 0;

    while (a[i] != '\0' && a[i] == b[i]) {
        ++i;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

char* strcpy(char* dst, const char* src)
{
    unsigned long i = 0;

    while (src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
    return dst;
}

/* ------------------------------------------------------------------ */
/* Versiones con prefijo ax_                                          */
/* ------------------------------------------------------------------ */

void* ax_memcpy(void* dst, const void* src, unsigned long n)
{
    return memcpy(dst, src, n);
}

void* ax_memset(void* dst, int value, unsigned long n)
{
    return memset(dst, value, n);
}

unsigned long ax_strlen(const char* s)
{
    return strlen(s);
}

int ax_strcmp(const char* a, const char* b)
{
    return strcmp(a, b);
}

char* ax_strcpy(char* dst, const char* src)
{
    return strcpy(dst, src);
}

/* ------------------------------------------------------------------ */
/* Conversion de numeros a texto                                      */
/* ------------------------------------------------------------------ */

long ax_itoa(long value, char* buffer, unsigned long size)
{
    char   tmp[24];
    unsigned long n = 0;
    unsigned long pos = 0;
    unsigned long digits = 0;
    bool negative = false;

    if (buffer == nullptr || size == 0) {
        return -1;
    }

    if (value < 0) {
        negative = true;
        /* -value puede desbordar para LONG_MIN: se trabaja en unsigned. */
        unsigned long magnitude = (unsigned long)(-(value + 1)) + 1UL;
        do {
            tmp[n++] = (char)('0' + (int)(magnitude % 10UL));
            magnitude /= 10UL;
        } while (magnitude != 0UL);
    } else {
        unsigned long magnitude = (unsigned long)value;
        do {
            tmp[n++] = (char)('0' + (int)(magnitude % 10UL));
            magnitude /= 10UL;
        } while (magnitude != 0UL);
    }

    digits = n;
    if (negative) {
        ++digits; /* signo */
    }
    if (digits + 1UL > size) {
        buffer[0] = '\0';
        return -1;
    }

    if (negative) {
        buffer[pos++] = '-';
    }
    while (n > 0) {
        buffer[pos++] = tmp[--n];
    }
    buffer[pos] = '\0';
    return (long)pos;
}

long ax_utoa_hex(unsigned long value, char* buffer, unsigned long size)
{
    static const char digits[] = "0123456789abcdef";
    char          tmp[16];
    unsigned long n = 0;
    unsigned long pos = 0;

    if (buffer == nullptr || size < 3UL) {
        return -1;
    }

    do {
        tmp[n++] = digits[value & 0xFUL];
        value >>= 4;
    } while (value != 0UL);

    if (n + 3UL > size) {
        buffer[0] = '\0';
        return -1;
    }

    buffer[pos++] = '0';
    buffer[pos++] = 'x';
    while (n > 0) {
        buffer[pos++] = tmp[--n];
    }
    buffer[pos] = '\0';
    return (long)pos;
}

/* ------------------------------------------------------------------ */
/* Soporte del protector de pila                                      */
/* ------------------------------------------------------------------ */

/*
 * Si el toolchain activa el protector de pila (-fstack-protector-strong)
 * el compilador genera llamadas a __stack_chk_fail. En un entorno sin
 * libc hay que proporcionarla: se aborta el proceso con el codigo 127.
 */
AX_NORETURN void __stack_chk_fail(void)
{
    ax_syscall_exit(127);
}
