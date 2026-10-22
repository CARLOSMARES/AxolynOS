/*
 * AxolynOS - GDT, TSS, IDT y trampolines de interrupcion.
 */
#ifndef AX_CPU_H
#define AX_CPU_H

#include "ax_types.h"

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) ax_gdt_entry;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) ax_gdt_ptr;

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) ax_tss;

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  flags;       /* tipo + DPL + P */
    uint16_t offset_high;
    uint64_t offset_ist;
} __attribute__((packed)) ax_idt_entry;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) ax_idt_ptr;

/* Numeros de vector de excepciones */
enum {
    AX_EXC_DIV_ZERO   = 0,
    AX_EXC_DEBUG      = 1,
    AX_EXC_NMI        = 2,
    AX_EXC_BREAKPOINT  = 3,
    AX_EXC_OVERFLOW   = 4,
    AX_EXC_BOUND      = 5,
    AX_EXC_ILLOP      = 6,
    AX_EXC_DEVICE     = 7,
    AX_EXC_DOUBLE     = 8,
    AX_EXC_TSS        = 10,
    AX_EXC_SEGNP      = 11,
    AX_EXC_STACK      = 12,
    AX_EXC_GPFAULT    = 13,
    AX_EXC_PGFAULT    = 14,
    AX_EXC_FPERR      = 16,
    AX_EXC_ALIGN      = 17,
    AX_EXC_MCHECK     = 18,
    AX_EXC_SIMD       = 19,
    AX_EXC_DEBUG2     = 21,
    AX_IRQ_BASE       = 32,
    AX_IRQ_TIMER      = 0,
    AX_IRQ_KEYBOARD   = 1,
    AX_IRQ_CASCADE    = 2,
    AX_IRQ_COM2       = 3,
    AX_IRQ_COM1       = 4,
    AX_IRQ_LPT2       = 5,
    AX_IRQ_FLOPPY     = 6,
    AX_IRQ_RTC        = 8,
    AX_IRQ_PIT_SPURIOUS = 15,
    AX_IRQ_PC_ERROR   = 16,
    AX_IRQ_CASCADE2   = 18,
    AX_IRQ_HD         = 14,
    AX_IRQ_IDE0       = 14,
    AX_VECTOR_COUNT   = 48
};

/* Marco de pila de una interrupcion (pusha + numero de error) */
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) ax_interrupt_frame;

void ax_gdt_init(void);
void ax_idt_init(void);
void ax_idt_set_gate(uint8_t vector, uint64_t handler, uint16_t selector, uint8_t ist, uint8_t dpl);
void ax_tss_init(uint64_t kernel_stack_top);
void ax_tss_set_rsp0(uint64_t rsp0);

/* Manejadores de excepciones (Kernel/src/isr.cpp) */
void ax_isr_handler(ax_interrupt_frame* frame);
void ax_irq_handler(ax_interrupt_frame* frame);

/* Descarga de registros de control */
uint64_t ax_read_cr2(void);
uint64_t ax_read_cr3(void);
void     ax_load_cr3(uint64_t value);
uint64_t ax_read_cr0(void);
uint64_t ax_read_cr4(void);
void     ax_flush_tlb(void);
void     ax_disable_interrupts(void);
void     ax_enable_interrupts(void);
bool     ax_interrupts_enabled(void);

/* Habilita las lineas de IRQ en la mascara del PIC */
void ax_pic_init(void);
void ax_pic_mask(uint8_t irq, bool masked);
void ax_pic_eoi(uint8_t irq);

#endif /* AX_CPU_H */
