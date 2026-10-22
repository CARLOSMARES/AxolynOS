/*
 * AxolynOS - Consola de user mode (implementacion).
 *
 * Usa AX_SYS_WRITE sobre los descriptores 1 (salida) y 2 (error).
 */
#include "axos/console.h"
#include "axos/string.h"

/* ------------------------------------------------------------------ */
/* Escritura cruda                                                    */
/* ------------------------------------------------------------------ */

long ax_console_write(int fd, const void* buf, unsigned long len)
{
    const char*        p = (const char*)buf;
    unsigned long      done = 0;
    long               last = 0;

    if (buf == nullptr) {
        return AX_EINVAL;
    }

    /* Se reintenta mientras el kernel acepte bytes: una escritura puede
     * ser parcial si el buffer del kernel esta lleno. */
    while (done < len) {
        long n = ax_syscall3(AX_SYS_WRITE, fd, (long)(p + done), (long)(len - done));
        if (n < 0) {
            return (done > 0) ? (long)done : n;
        }
        if (n == 0) {
            break; /* el kernel no avanza: se sale para no bucear */
        }
        last = n;
        done += (unsigned long)n;
    }
    return (last < 0) ? last : (long)done;
}

long ax_console_puts(int fd, const char* text)
{
    if (text == nullptr) {
        return AX_EINVAL;
    }
    return ax_console_write(fd, text, ax_strlen(text));
}

/* ------------------------------------------------------------------ */
/* Salida estandar                                                    */
/* ------------------------------------------------------------------ */

void ax_console_print(const char* text)
{
    if (text != nullptr) {
        ax_console_write(AX_STDOUT, text, ax_strlen(text));
    }
}

void ax_console_println(const char* text)
{
    ax_console_print(text);
    ax_console_write(AX_STDOUT, "\n", 1UL);
}

void ax_console_print_int(long value)
{
    char buffer[24];

    if (ax_itoa(value, buffer, sizeof(buffer)) >= 0) {
        ax_console_write(AX_STDOUT, buffer, ax_strlen(buffer));
    }
}

void ax_console_println_int(long value)
{
    ax_console_print_int(value);
    ax_console_newline();
}

void ax_console_print_uint(unsigned long value)
{
    char buffer[24];

    if (ax_itoa((long)value, buffer, sizeof(buffer)) >= 0) {
        ax_console_write(AX_STDOUT, buffer, ax_strlen(buffer));
    }
}

void ax_console_println_uint(unsigned long value)
{
    ax_console_print_uint(value);
    ax_console_newline();
}

void ax_console_print_hex(unsigned long value)
{
    char buffer[20];

    if (ax_utoa_hex(value, buffer, sizeof(buffer)) >= 0) {
        ax_console_write(AX_STDOUT, buffer, ax_strlen(buffer));
    }
}

void ax_console_println_hex(unsigned long value)
{
    ax_console_print_hex(value);
    ax_console_newline();
}

void ax_console_newline(void)
{
    ax_console_write(AX_STDOUT, "\n", 1UL);
}

/* ------------------------------------------------------------------ */
/* Entrada estandar                                                   */
/* ------------------------------------------------------------------ */

long ax_console_read_line(char* buf, unsigned long max)
{
    long n;

    if (buf == nullptr || max == 0UL) {
        return AX_EINVAL;
    }

    /* AX_SYS_GETS ya devuelve la linea terminada en '\0'. */
    n = ax_syscall2(AX_SYS_GETS, (long)buf, (long)max);
    return n;
}

/* ------------------------------------------------------------------ */
/* Nombres de los codigos de error                                    */
/* ------------------------------------------------------------------ */

const char* ax_console_errno_name(int err)
{
    if (err < 0) {
        err = -err;
    }

    /* Se usa una cadena de if/else (y no un switch) porque un switch
     * generaria una tabla de saltos con direcciones absolutas, que el
     * enlazador no puede convertir al enlazar como PIE. */
    if (err == AX_OK)     return "OK";
    if (err == AX_ENOENT) return "ENOENT";
    if (err == AX_EIO)    return "EIO";
    if (err == AX_EINVAL) return "EINVAL";
    if (err == AX_ENOSPC) return "ENOSPC";
    if (err == AX_EACCES) return "EACCES";
    if (err == AX_EEXIST) return "EEXIST";
    if (err == AX_ENOMEM) return "ENOMEM";
    if (err == AX_EBUSY)  return "EBUSY";
    if (err == AX_EINTR)  return "EINTR";
    if (err == AX_EAGAIN) return "EAGAIN";
    if (err == AX_ENOSYS) return "ENOSYS";
    if (err == AX_ERANGE) return "ERANGE";
    return "EUNKNOWN";
}
