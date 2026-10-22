/*
 * AxolynOS - Script de enlazado del kernel (higher half).
 *
 * El kernel vive en 0xFFFFFFFF80200000 (virtual) mapeado fisicamente
 * sobre 0x00200000. El bootloader usa las direcciones fisicas (p_paddr)
 * de las cabeceras ELF para copiar el kernel a memoria, y salta a
 * e_entry (virtual) una vez activa la paginacion de 64 bits.
 *
 * Simbolos exportados que usa el codigo de arranque (Kernel/src/boot.S):
 *   __bss_start / __bss_end  -> region a cero en el arranque
 *   __kernel_phys_start/end  -> rango fisico ocupado por la imagen
 */
ENTRY(_start)

KERNEL_VIRT_BASE = 0xFFFFFFFF80200000;
KERNEL_PHYS_BASE = 0x0000000000200000;

SECTIONS
{
    . = KERNEL_VIRT_BASE;

    __kernel_virt_start = .;

    .text : AT(ADDR(.text) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        KEEP(*(.text.boot))
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
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)

        /* Tablas de pagina temporales del arranque (Kernel/src/boot.S).
           Se reservan aqui, en .bss, porque todavia no existe el gestor de
           memoria fisica: el arranque necesita mapear identidad la memoria
           baja para poder leer el AxBootInfo en 0x1000.  Se descartan cuando
           ax_vmm_init() carga la tabla PML4 definitiva. */
        . = ALIGN(4096);
        __tmp_ptables = .;
        . += 0x5000;                 /* PML4 + 2 PDPT + 2 PD (5 paginas) */
        __tmp_ptables_end = .;

        /* Pila inicial del kernel: 64 KiB reservadas al final de .bss.
           __kernel_stack_top es la cima (alineada a 16) y la que usa boot.S
           como RSP inicial; el resto de la region queda como pila libre. */
        . = ALIGN(16 * 1024);
        __kernel_stack_bottom = .;
        . += 0x10000;                /* 64 KiB de pila */
        . = ALIGN(16);
        __kernel_stack_top = .;

        __bss_end = .;
    }

    . = ALIGN(4096);
    __kernel_end = .;
    __kernel_virt_end = .;

    __kernel_phys_start = KERNEL_PHYS_BASE;
    __kernel_phys_end   = KERNEL_PHYS_BASE + (__kernel_end - KERNEL_VIRT_BASE);

    /* Direccion fisica de las tablas de pagina temporales del arranque.
       El kernel se carga en 0x200000 fisico y se enlaza en 0xFFFFFFFF80200000
       virtual, de modo que la fisica se obtiene restando la base virtual. */
    __tmp_pml4_phys = __tmp_ptables - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE;

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.comment)
        *(.note .note.*)
    }
}
