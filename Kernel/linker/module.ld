/*
 * AxolynOS - Script de enlazado de los modulos del kernel.
 *
 * Cada modulo se enlaza en una ranura fija de la ventana de modulos:
 *     AX_MODULES_VIRT_BASE + slot * AX_MODULES_SLOT_SIZE
 * de forma que no necesita relocaciones en tiempo de carga (el kernel
 * simplemente copia el binario plano a esa direccion).
 *
 * El valor de AX_MOD_BASE lo define el script de compilacion con
 * -defsym AX_MOD_BASE=0x....
 *
 * Disposicion (offsets relativos a la base del modulo):
 *   0x00000  .text
 *   ...      .rodata (alineado a 16)
 *   0x10000  .data   (alineado a 64 KiB)
 *   ...      .bss    (alineado a 4 KiB)
 * El cargador copia codigo+datos como un binario plano, de modo que
 * .data queda exactamente en el offset que el enlazador uso al
 * resolver las direcciones absolutas.
 */
ENTRY(_ax_module_entry)

SECTIONS
{
    . = AX_MOD_BASE;
    __module_base = .;

    .text : {
        *(.text.entry)
        *(.text .text.*)
    }

    . = ALIGN(16);
    .rodata : {
        *(.rodata .rodata.*)
    }

    . = ALIGN(64K);
    __module_data_start = .;

    .data : {
        *(.data .data.*)
    }

    . = ALIGN(4K);
    __module_bss_start = .;

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
    }

    __module_bss_end = .;
    __module_end = .;

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note .note.*)
    }
}
