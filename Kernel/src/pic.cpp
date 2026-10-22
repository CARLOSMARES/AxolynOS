/*
 * AxolynOS - Controlador de interrupciones PIC 8259 (dos cascados).
 *
 * El BIOS deja el PIC con las IRQ en 0x08..0x0F y 0x70..0x77, que solapan
 * con las excepciones de la CPU.  El kernel lo remapea a 0x20..0x2F para que
 * las excepciones ocupen 0x00..0x1F (como dicta la arquitectura x86-64).
 */

#include "ax_types.h"
#include "ax_cpu.h"

/* ------------------------------------------------------------------ */
/* Puertos del PIC                                                      */
/* ------------------------------------------------------------------ */
#define AX_PIC1_CMD   0x20
#define AX_PIC1_DATA  0x21
#define AX_PIC2_CMD   0xA0
#define AX_PIC2_DATA  0xA1

/* Comandos de inicializacion */
#define AX_PIC_OCW1   0x11    /* inicio + ICW4                            */
#define AX_PIC_OCW2   0x04    /* bit 2 = IRQ2 del maestro conectada al esclavo */
#define AX_PIC_OCW3   0x02    /* identificador del esclavo (2)             */
#define AX_PIC_OCW4   0x01    /* modo 8086                                  */
#define AX_PIC_EOI    0x20    /* fin de interrupcion                        */

static inline void ax_outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t ax_inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Retardo corto: el PIC necesita tiempo entre comandos ICW. */
static void ax_io_wait(void)
{
    /* El puerto 0x80 se usa como "puerta de espera" en los PC reales. */
    ax_outb(0x80, 0);
}

void ax_pic_init(void)
{
    /* --- ICW1: iniciar, esperar ICW4 ------------------------------- */
    ax_outb(AX_PIC1_CMD, AX_PIC_OCW1);
    ax_io_wait();
    ax_outb(AX_PIC2_CMD, AX_PIC_OCW1);
    ax_io_wait();

    /* --- ICW2: vectores base ---------------------------------------- */
    /* Maestro: IRQ0..IRQ7 -> 0x20..0x27, esclavo: IRQ8..IRQ15 -> 0x28..0x2F */
    ax_outb(AX_PIC1_DATA, 0x20);
    ax_io_wait();
    ax_outb(AX_PIC2_DATA, 0x28);
    ax_io_wait();

    /* --- ICW3: cascado --------------------------------------------- */
    ax_outb(AX_PIC1_DATA, AX_PIC_OCW2);
    ax_io_wait();
    ax_outb(AX_PIC2_DATA, AX_PIC_OCW3);
    ax_io_wait();

    /* --- ICW4: modo 8086 ------------------------------------------- */
    ax_outb(AX_PIC1_DATA, AX_PIC_OCW4);
    ax_io_wait();
    ax_outb(AX_PIC2_DATA, AX_PIC_OCW4);
    ax_io_wait();

    /* --- OCW1: mascara --------------------------------------------- */
    /* Solo dejamos activas el temporizador (IRQ0) y el teclado (IRQ1). */
    ax_outb(AX_PIC1_DATA, 0xFC);
    ax_outb(AX_PIC2_DATA, 0xFF);
}

void ax_pic_mask(uint8_t irq, bool masked)
{
    uint16_t port = (irq < 8) ? AX_PIC1_DATA : AX_PIC2_DATA;
    uint8_t bit = (uint8_t)(1u << (irq & 0x07));
    uint8_t value = ax_inb(port);

    if (masked) {
        value |= bit;
    } else {
        value = (uint8_t)(value & (uint8_t)~bit);
    }
    ax_outb(port, value);
}

void ax_pic_eoi(uint8_t irq)
{
    /* Si la IRQ venia del esclavo hay que avisar primero a este: si no, el
       maestro mantiene bloqueada la linea IRQ2 y no llega ninguna IRQ. */
    if (irq >= 8) {
        ax_outb(AX_PIC2_CMD, AX_PIC_EOI);
    }
    ax_outb(AX_PIC1_CMD, AX_PIC_EOI);
}
