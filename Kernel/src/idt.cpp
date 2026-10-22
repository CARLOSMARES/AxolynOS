/*
 * AxolynOS - Tabla de descriptores de interrupcion (IDT).
 *
 * La IDT tiene 256 entradas (16 bytes cada una, el formato de 64 bits).  Se
 * rellenan:
 *   - vectores 0..31 : excepciones de la CPU,
 *   - vectores 32..47: IRQ del PIC 8259 remapeadas a 0x20..0x2F,
 *   - vector  0x80   : llamada al sistema (int 0x80) desde user mode.
 *
 * NOTA sobre ax_idt_entry (ax_cpu.h): los nombres de los campos son
 * historicos, pero el orden de los bytes coincide con el formato hardware:
 *
 *   offset_low   (0-1)  offset[15:0]
 *   selector     (2-3)  selector de segmento
 *   zero         (4)    byte IST (indice de la pila de excepcion)
 *   flags        (5)    tipo de puerta | DPL | presente
 *   offset_high  (6-7)  offset[31:16]
 *   offset_ist   (8-15) offset[63:32]
 */

#include "ax_types.h"
#include "ax_cpu.h"
#include "ax_config.h"

/* ------------------------------------------------------------------ */
/* Tabla de interrupciones                                             */
/* ------------------------------------------------------------------ */
static ax_idt_entry ax_idt_table[256] __attribute__((aligned(16)));

/* ------------------------------------------------------------------ */
/* Declaraciones de los trampolines de ensamblador                     */
/* ------------------------------------------------------------------ */
#define AX_DECLARE_STUB(n) extern "C" void ax_isr_stub_##n(void);

AX_DECLARE_STUB(0)  AX_DECLARE_STUB(1)  AX_DECLARE_STUB(2)  AX_DECLARE_STUB(3)
AX_DECLARE_STUB(4)  AX_DECLARE_STUB(5)  AX_DECLARE_STUB(6)  AX_DECLARE_STUB(7)
AX_DECLARE_STUB(8)  AX_DECLARE_STUB(9)  AX_DECLARE_STUB(10) AX_DECLARE_STUB(11)
AX_DECLARE_STUB(12) AX_DECLARE_STUB(13) AX_DECLARE_STUB(14) AX_DECLARE_STUB(15)
AX_DECLARE_STUB(16) AX_DECLARE_STUB(17) AX_DECLARE_STUB(18) AX_DECLARE_STUB(19)
AX_DECLARE_STUB(20) AX_DECLARE_STUB(21) AX_DECLARE_STUB(22) AX_DECLARE_STUB(23)
AX_DECLARE_STUB(24) AX_DECLARE_STUB(25) AX_DECLARE_STUB(26) AX_DECLARE_STUB(27)
AX_DECLARE_STUB(28) AX_DECLARE_STUB(29) AX_DECLARE_STUB(30) AX_DECLARE_STUB(31)
AX_DECLARE_STUB(32) AX_DECLARE_STUB(33) AX_DECLARE_STUB(34) AX_DECLARE_STUB(35)
AX_DECLARE_STUB(36) AX_DECLARE_STUB(37) AX_DECLARE_STUB(38) AX_DECLARE_STUB(39)
AX_DECLARE_STUB(40) AX_DECLARE_STUB(41) AX_DECLARE_STUB(42) AX_DECLARE_STUB(43)
AX_DECLARE_STUB(44) AX_DECLARE_STUB(45) AX_DECLARE_STUB(46) AX_DECLARE_STUB(47)

/* Trampolin de la llamada al sistema (int 0x80). */
extern "C" void ax_syscall_stub(void);

#undef AX_DECLARE_STUB

/* ------------------------------------------------------------------ */
/* Utilidades propias                                                   */
/* ------------------------------------------------------------------ */
static void ax_memset(void* dst, int value, uint64_t count)
{
    uint8_t* p = (uint8_t*)dst;
    for (uint64_t i = 0; i < count; ++i) {
        p[i] = (uint8_t)value;
    }
}

/* ------------------------------------------------------------------ */
/* API                                                                  */
/* ------------------------------------------------------------------ */
void ax_idt_set_gate(uint8_t vector, uint64_t handler, uint16_t selector, uint8_t ist, uint8_t dpl)
{
    ax_idt_entry* entry = &ax_idt_table[vector];

    entry->offset_low  = (uint16_t)(handler & 0xFFFF);
    entry->selector    = selector;
    entry->zero        = (uint8_t)(ist & 0x07);      /* byte IST de la puerta */
    /* 0x8E: puerta de interrupcion (no de trampa) presente en ring 0.
       Los bits 5-6 contienen el DPL: con DPL 3 la puerta es accesible desde
       user mode, que es justo lo que necesita el vector 0x80. */
    entry->flags       = (uint8_t)(0x8E | ((dpl & 0x03) << 5));
    entry->offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
    entry->offset_ist  = handler >> 32;              /* offset[63:32] */
}

void ax_idt_init(void)
{
    ax_memset(ax_idt_table, 0, sizeof(ax_idt_table));

    /* --- Excepciones (0..31) --------------------------------------- */
    static const uint64_t exception_stubs[32] = {
        (uint64_t)(uintptr_t)&ax_isr_stub_0,  (uint64_t)(uintptr_t)&ax_isr_stub_1,
        (uint64_t)(uintptr_t)&ax_isr_stub_2,  (uint64_t)(uintptr_t)&ax_isr_stub_3,
        (uint64_t)(uintptr_t)&ax_isr_stub_4,  (uint64_t)(uintptr_t)&ax_isr_stub_5,
        (uint64_t)(uintptr_t)&ax_isr_stub_6,  (uint64_t)(uintptr_t)&ax_isr_stub_7,
        (uint64_t)(uintptr_t)&ax_isr_stub_8,  (uint64_t)(uintptr_t)&ax_isr_stub_9,
        (uint64_t)(uintptr_t)&ax_isr_stub_10, (uint64_t)(uintptr_t)&ax_isr_stub_11,
        (uint64_t)(uintptr_t)&ax_isr_stub_12, (uint64_t)(uintptr_t)&ax_isr_stub_13,
        (uint64_t)(uintptr_t)&ax_isr_stub_14, (uint64_t)(uintptr_t)&ax_isr_stub_15,
        (uint64_t)(uintptr_t)&ax_isr_stub_16, (uint64_t)(uintptr_t)&ax_isr_stub_17,
        (uint64_t)(uintptr_t)&ax_isr_stub_18, (uint64_t)(uintptr_t)&ax_isr_stub_19,
        (uint64_t)(uintptr_t)&ax_isr_stub_20, (uint64_t)(uintptr_t)&ax_isr_stub_21,
        (uint64_t)(uintptr_t)&ax_isr_stub_22, (uint64_t)(uintptr_t)&ax_isr_stub_23,
        (uint64_t)(uintptr_t)&ax_isr_stub_24, (uint64_t)(uintptr_t)&ax_isr_stub_25,
        (uint64_t)(uintptr_t)&ax_isr_stub_26, (uint64_t)(uintptr_t)&ax_isr_stub_27,
        (uint64_t)(uintptr_t)&ax_isr_stub_28, (uint64_t)(uintptr_t)&ax_isr_stub_29,
        (uint64_t)(uintptr_t)&ax_isr_stub_30, (uint64_t)(uintptr_t)&ax_isr_stub_31
    };

    for (uint32_t vector = 0; vector < 32; ++vector) {
        /* El NMI y el doble fallo usan la pila de excepcion (IST 1): si la
           pila del kernel esta corrupta o desbordada, al menos hay un marco
           donde escribir el diagnostico antes de reiniciar la maquina. */
        uint8_t ist = 0;
        if (vector == AX_EXC_NMI || vector == AX_EXC_DOUBLE) {
            ist = 1;
        }
        ax_idt_set_gate((uint8_t)vector, exception_stubs[vector],
                        AX_SELECTOR_KERNEL_CODE, ist, 0);
    }

    /* --- IRQ del PIC (32..47) -------------------------------------- */
    static const uint64_t irq_stubs[16] = {
        (uint64_t)(uintptr_t)&ax_isr_stub_32, (uint64_t)(uintptr_t)&ax_isr_stub_33,
        (uint64_t)(uintptr_t)&ax_isr_stub_34, (uint64_t)(uintptr_t)&ax_isr_stub_35,
        (uint64_t)(uintptr_t)&ax_isr_stub_36, (uint64_t)(uintptr_t)&ax_isr_stub_37,
        (uint64_t)(uintptr_t)&ax_isr_stub_38, (uint64_t)(uintptr_t)&ax_isr_stub_39,
        (uint64_t)(uintptr_t)&ax_isr_stub_40, (uint64_t)(uintptr_t)&ax_isr_stub_41,
        (uint64_t)(uintptr_t)&ax_isr_stub_42, (uint64_t)(uintptr_t)&ax_isr_stub_43,
        (uint64_t)(uintptr_t)&ax_isr_stub_44, (uint64_t)(uintptr_t)&ax_isr_stub_45,
        (uint64_t)(uintptr_t)&ax_isr_stub_46, (uint64_t)(uintptr_t)&ax_isr_stub_47
    };

    for (uint32_t vector = 32; vector < 48; ++vector) {
        ax_idt_set_gate((uint8_t)vector, irq_stubs[vector - 32],
                        AX_SELECTOR_KERNEL_CODE, 0, 0);
    }

    /* --- Llamada al sistema (0x80) --------------------------------- */
    /*
     * La puerta se instala con DPL 3 (flags = 0x8E | (3 << 5) = 0xEE).
     *
     * POR QUE DPL 3: el DPL (Descriptor Privilege Level) de una puerta de
     * interrupcion indica el nivel de privilegio minimo que puede invocar
     * esa puerta.  Con DPL 0 un `int 0x80` ejecutado en ring 3 provocaria un
     * #GP (proteccion general) y la aplicacion moriria antes de llegar al
     * manejador.  Con DPL 3 la CPU permite el `int 0x80` desde user mode y
     * el manejador recibe el control en ring 0 con el marco completo
     * (CS/RFLAGS/RIP/RSP/SS), de modo que el iretq final devuelve la
     * ejecucion al anillo de usuario con el valor devuelto en RAX.
     */
    ax_idt_set_gate(AX_SYSCALL_VECTOR, (uint64_t)(uintptr_t)&ax_syscall_stub,
                    AX_SELECTOR_KERNEL_CODE, 0, 3);

    /* --- Carga de la IDT ------------------------------------------- */
    ax_idt_ptr pointer;
    pointer.limit = (uint16_t)(sizeof(ax_idt_table) - 1);
    pointer.base  = (uint64_t)(uintptr_t)ax_idt_table;
    __asm__ volatile("lidt %0" : : "m"(pointer) : "memory");
}
