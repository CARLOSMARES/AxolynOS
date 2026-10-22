/*
 * AxolynOS - Script de enlazado del kernel de prueba del bootloader.
 *
 * La imagen se carga en 0x200000 fisico (como el kernel real) y se ejecuta
 * en 0xFFFFFFFF80200000 virtual, que es la ventana que mapea el bootloader
 * con su PT de 4 KiB.
 *
 * Los valores de AT() siguen la misma regla que Kernel/linker/kernel.ld:
 *   p_paddr = p_vaddr - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE
 */
ENTRY(_start)

KERNEL_VIRT_BASE = 0xFFFFFFFF80200000;
KERNEL_PHYS_BASE = 0x0000000000200000;

SECTIONS
{
    . = KERNEL_VIRT_BASE;

    .text : AT(ADDR(.text) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        *(.text .text.*)
    }

    . = ALIGN(4096);
    .rodata : AT(ADDR(.rodata) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        *(.rodata .rodata.*)
    }

    . = ALIGN(4096);
    .data : AT(ADDR(.data) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        *(.data .data.*)
    }

    . = ALIGN(4096);
    .bss : AT(ADDR(.bss) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        *(COMMON)
        *(.bss .bss.*)
    }
}
