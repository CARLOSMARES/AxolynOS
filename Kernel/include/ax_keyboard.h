/*
 * AxolynOS - Controlador de teclado PS/2 (IRQ1).
 *
 * El kernel mantiene un buffer circular con los caracteres ya traducidos a
 * ASCII.  La IRQ1 (vector 33) llama a ax_keyboard_irq(), que lee el scancode
 * del puerto 0x60 y lo mete en el buffer; la shell consume los caracteres
 * con ax_keyboard_getc()/ax_keyboard_has_char().
 *
 * Se asume el conjunto de scancodes 1 (el que usa el BIOS y QEMU por
 * defecto), con prefijo 0xE0 para las teclas extendidas.
 */
#ifndef AX_KEYBOARD_H
#define AX_KEYBOARD_H

#include "ax_types.h"

/* Inicializa el controlador 8042 y deja la IRQ1 activa. */
void ax_keyboard_init(void);

/* Se llama desde el manejador de IRQ1 (Kernel/src/isr.cpp). */
void ax_keyboard_irq(void);

/* Hay algun caracter pendiente en el buffer. */
bool ax_keyboard_has_char(void);

/* Saca un caracter del buffer (0 si esta vacio). */
char ax_keyboard_getc(void);

/* Numero de caracteres pendientes. */
uint32_t ax_keyboard_pending(void);

#endif /* AX_KEYBOARD_H */
