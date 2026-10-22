/*
 * AxolynOS - Tabla de descriptores globales (GDT), TSS y utilidades de control.
 *
 * Disposicion de la GDT (los selectores son los del contrato, ax_config.h):
 *
 *   0x00  null
 *   0x08  codigo de kernel   (DPL 0, L=1 -> modo 64 bits)
 *   0x10  datos de kernel    (DPL 0)
 *   0x20  codigo de usuario  (DPL 3)  -> selector 0x23 en user mode
 *   0x24  datos de usuario   (DPL 3)  -> selector 0x2B en user mode
 *   0x28  TSS (descriptor de 16 bytes)
 *
 * El TSS es imprescindible: la CPU lo usa para saber en que pila apilar el
 * marco cuando una interrupcion llega desde user mode (campo rsp0).
 */

#include "ax_types.h"
#include "ax_cpu.h"

/* Pila de entrada del kernel (la usa la CPU en las transiciones ring 3 -> 0).
   Se define en kernel.cpp porque debe sobrevivir al apagado/reinicio. */
extern "C" uint64_t ax_entry_stack_top;

/* Pila de trabajo de los manejadores (la usa isr_stubs.S). */
extern "C" uint64_t ax_kernel_stack_top;

/* Pila de excepcion (IST) para el doble fallo y el NMI: permite recuperar el
   sistema cuando la pila del kernel esta corrupta o desbordada. */
extern "C" uint64_t ax_ist_stack_top;

/* ------------------------------------------------------------------ */
/* Utilidades propias: el kernel no usa la libc del host.              */
/* ------------------------------------------------------------------ */
static void ax_memset(void* dst, int value, uint64_t count)
{
    uint8_t* p = (uint8_t*)dst;
    for (uint64_t i = 0; i < count; ++i) {
        p[i] = (uint8_t)value;
    }
}

/* ------------------------------------------------------------------ */
/* Estructura de la GDT                                                */
/* ------------------------------------------------------------------ */
struct ax_gdt_desc {
    ax_gdt_entry entries[5];   /* null, kcode, kdata, ucode, udata */
    ax_gdt_entry tss_low;      /* mitad baja del descriptor del TSS  */
    uint64_t     tss_high;     /* base[63:32] del TSS                */
};

static ax_gdt_desc ax_gdt_data;
static ax_tss      ax_tss_data;

/* ------------------------------------------------------------------ */
/* Utilidades de control de la CPU                                     */
/* ------------------------------------------------------------------ */
uint64_t ax_read_cr0(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr0, %0" : "=r"(value));
    return value;
}

uint64_t ax_read_cr2(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

uint64_t ax_read_cr3(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

void ax_load_cr3(uint64_t value)
{
    /* Escribir CR3 vacia la TLB: es la forma estandar de invalidar las
       traducciones cacheadas sin invocar INVLPG en cada entrada. */
    __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

uint64_t ax_read_cr4(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr4, %0" : "=r"(value));
    return value;
}

void ax_flush_tlb(void)
{
    uint64_t cr3 = ax_read_cr3();
    ax_load_cr3(cr3);
}

void ax_disable_interrupts(void)
{
    __asm__ volatile("cli" : : : "memory");
}

void ax_enable_interrupts(void)
{
    __asm__ volatile("sti" : : : "memory");
}

bool ax_interrupts_enabled(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq\n\tpopq %0" : "=r"(flags) : : "memory");
    return (flags & 0x200ull) != 0;
}

/* ------------------------------------------------------------------ */
/* Construccion de la GDT                                              */
/* ------------------------------------------------------------------ */
static void ax_gdt_set_entry(ax_gdt_entry* entry,
                              uint32_t base, uint32_t limit,
                              uint8_t access, uint8_t granularity)
{
    entry->limit_low   = (uint16_t)(limit & 0xFFFF);
    entry->base_low    = (uint16_t)(base & 0xFFFF);
    entry->base_mid    = (uint8_t)((base >> 16) & 0xFF);
    entry->access      = access;
    entry->granularity = granularity;
    entry->base_high   = (uint8_t)((base >> 24) & 0xFF);
}

void ax_gdt_init(void)
{
    ax_memset(&ax_gdt_data, 0, sizeof(ax_gdt_data));

    /* Segmento de codigo de 64 bits: L=1 (long mode), D/B=1, G=1 (granularidad
       de 4 KiB) y limite de 0xFFFFF -> un rango de 4 GiB por segmento. */
    ax_gdt_set_entry(&ax_gdt_data.entries[1], 0, 0x000FFFFF, 0x9A, 0xAF);
    /* Segmento de datos del kernel: base 0, writable, D/B=1. */
    ax_gdt_set_entry(&ax_gdt_data.entries[2], 0, 0x000FFFFF, 0x92, 0xCF);
    /* Segmentos de usuario: DPL 3 para que el codigo de ring 3 pueda
       cargarlos con el bit RPL=3 en el selector (0x23 / 0x2B). */
    ax_gdt_set_entry(&ax_gdt_data.entries[3], 0, 0x000FFFFF, 0xFA, 0xAF);
    ax_gdt_set_entry(&ax_gdt_data.entries[4], 0, 0x000FFFFF, 0xF2, 0xCF);

    ax_gdt_ptr pointer;
    pointer.limit = (uint16_t)(sizeof(ax_gdt_data) - 1);
    pointer.base  = (uint64_t)(uintptr_t)&ax_gdt_data;

    __asm__ volatile("lgdt %0" : : "m"(pointer) : "memory");

    /* Hay que recargar CS: el valor anterior estaba cacheado en la CPU y
       apuntaria a un descriptor que ya no existe.  El salto lejano (lretq)
       carga CS con el selector 0x08 (codigo de kernel de 64 bits). */
    __asm__ volatile(
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        : : : "rax", "memory");

    /* Recargamos tambien los segmentos de datos del kernel. */
    __asm__ volatile(
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        : : : "rax", "memory");
}

/* ------------------------------------------------------------------ */
/* TSS                                                                 */
/* ------------------------------------------------------------------ */
void ax_tss_init(uint64_t kernel_stack_top)
{
    ax_memset(&ax_tss_data, 0, sizeof(ax_tss_data));

    ax_tss_data.reserved0 = 0;
    ax_tss_data.rsp0 = kernel_stack_top;   /* user -> kernel                    */
    ax_tss_data.rsp1 = 0;                   /* user -> kernel (sin usar)         */
    ax_tss_data.rsp2 = 0;                   /* supervisor -> user (sin usar)     */
    ax_tss_data.reserved1 = 0;
    ax_tss_data.reserved2 = 0;
    ax_tss_data.reserved3 = 0;
    /* Pila de excepcion (IST 1) para doble fallo y NMI. */
    ax_tss_data.ist[0] = ax_ist_stack_top;
    /* iomap_base = sizeof(TSS): no hay mapa de permisos de E/S, asi que el
       bit de excepcion de E/S se bloquea con IOPL 0 (comportamiento por
       defecto en modo anillo 0). */
    ax_tss_data.iomap_base = (uint16_t)sizeof(ax_tss);

    /* Descriptor del TSS: 16 bytes (dos entradas de 8 bytes).
       La primera describe limit[15:0], base[31:0] y el byte de acceso 0x89
       (TSS de 64 bits disponible); la segunda, base[63:32]. */
    uint64_t base = (uint64_t)(uintptr_t)&ax_tss_data;
    uint32_t limit = (uint32_t)(sizeof(ax_tss_data) - 1);

    ax_gdt_set_entry(&ax_gdt_data.tss_low, base & 0xFFFFFFFFu, limit,
                     0x89, (uint8_t)((limit >> 16) & 0x0F));
    ax_gdt_data.tss_high = base >> 32;

    /* Hay que recargar la GDT porque hemos cambiado el descriptor del TSS. */
    ax_gdt_ptr pointer;
    pointer.limit = (uint16_t)(sizeof(ax_gdt_data) - 1);
    pointer.base  = (uint64_t)(uintptr_t)&ax_gdt_data;
    __asm__ volatile("lgdt %0" : : "m"(pointer) : "memory");

    /* ltr carga el selector 0x28 en el registro de tarea (TR). */
    __asm__ volatile("ltr %%ax" : : "a"((uint16_t)AX_GDT_TSS) : "memory");
}

void ax_tss_set_rsp0(uint64_t rsp0)
{
    /* El planificador llama a esta funcion en cada cambio de hilo para que
       la CPU apile las interrupciones en la pila del hilo activo. */
    ax_tss_data.rsp0 = rsp0;
}
