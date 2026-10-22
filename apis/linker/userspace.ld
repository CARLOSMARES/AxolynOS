/*
 * AxolynOS - Script de enlazado de las aplicaciones en user mode.
 *
 * Las aplicaciones se compilan como PIE (-fPIE -pie -nostdlib -static)
 * de forma que el cargador ELF del kernel pueda colocarlas en cualquier
 * ranura de user mode aplicando las relocaciones R_X86_64_RELATIVE.
 *
 * La direccion virtual final es:
 *      AX_USER_VIRT_BASE + ranura * AX_USER_SLOT_SIZE
 */
ENTRY(_start)

SECTIONS
{
    . = 0;

    .text : {
        KEEP(*(.text.entry))
        *(.text .text.*)
    }

    . = ALIGN(4096);
    .rodata : {
        *(.rodata .rodata.*)
    }

    . = ALIGN(4096);
    .data : {
        *(.data .data.*)
    }

    . = ALIGN(4096);
    .bss : {
        *(COMMON)
        *(.bss .bss.*)
        . = ALIGN(16);
    }

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note .note.*)
    }
}
