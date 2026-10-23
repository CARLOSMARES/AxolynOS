/*
 * AxolynOS - Gestión de energía
 */
#include "ax_power.h"
#include "ax_console.h"
#include "ax_cpu.h"

void ax_power_init(void)
{
    ax_printf("[power] inicializado\n");
}

void ax_power_halt(void)
{
    ax_printf("[power] deteniendo CPU...\n");
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

void ax_power_shutdown(void)
{
    ax_printf("[power] apagando sistema...\n");
    /* ACPI: intento básico con puerto 0x604 (QEMU/Bochs) */
    __asm__ volatile("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
    /* También intentar APM si falla */
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x00), "Nd"((uint16_t)0xB004));
    ax_power_halt();
}

void ax_power_reboot(void)
{
    ax_printf("[power] reiniciando...\n");
    /* Controlador de teclado */
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
    /* Triple fault como fallback */
    __asm__ volatile("lidt (%0)" : : "r"((uint64_t)0));
    __asm__ volatile("int $0x00");
    ax_power_halt();
}

void ax_power_suspend(void)
{
    ax_printf("[power] suspendiendo (ACPI S3)...\n");
    /* Implementación mínima: detener CPU (suspend-to-ram no implementado aún) */
    __asm__ volatile("sti");
    for (;;) {
        __asm__ volatile("hlt");
    }
}
