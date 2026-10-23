/*
 * AxolynOS - Acceso a puertos de E/S (x86, ring 0).
 *
 * Envoltorios minimos sobre las instrucciones in/out.  Se usan  desde los
 * drivers (PIC, PCI, disco, energia...) para no repetir el ensamblador en
 * cada fichero.
 *
 * Nota: estas funciones SOLO son validas en modo kernel.  Una aplicacion en
 * ring 3 no puede ejecutar in/out (el IOCL del TSS no le da permiso) y debe
 * pedir la operacion por syscall.
 */
#ifndef AX_IO_H
#define AX_IO_H

#include "ax_types.h"

/* --- Escritura ---------------------------------------------------- */

static inline void ax_outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void ax_outw(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void ax_outl(uint16_t port, uint32_t value)
{
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

/* --- Lectura ------------------------------------------------------ */

static inline uint8_t ax_inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t ax_inw(uint16_t port)
{
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint32_t ax_inl(uint16_t port)
{
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/*
 * Retardo corto para dispositivos lentos (por ejemplo el controlador de
 * teclado o el bus ISA).  Escribir en el puerto 0x80 desperdicia un ciclo
 * de bus y es la forma clasica de esperar sin temporizador.
 */
static inline void ax_io_wait(void)
{
    __asm__ volatile("outb %%al, $0x80" : : "a"(0));
}

/* --- Alias cortos ------------------------------------------------ */
/* Algunos drivers historicos usan los nombres sin prefijo. */
#define outb(port, value) ax_outb((port), (value))
#define outw(port, value) ax_outw((port), (value))
#define outl(port, value) ax_outl((port), (value))
#define inb(port)         ax_inb((port))
#define inw(port)         ax_inw((port))
#define inl(port)         ax_inl((port))

#endif /* AX_IO_H */
