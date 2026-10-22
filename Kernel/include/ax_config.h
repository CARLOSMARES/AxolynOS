/*
 * AxolynOS - Configuracion global del kernel.
 *
 * Este header es el contrato entre el bootloader, el kernel, los modulos y
 * las aplicaciones. Cualquier cambio aqui obliga a revisar:
 *   - Kernel/linker/kernel.ld
 *   - Kernel/src/boot.S
 *   - bootloader/stage2.asm  (ver scripts/tools/mkdisk.cpp)
 *
 * Documentacion completa: manuales/04-kernel-microkernel.md
 */
#ifndef AX_CONFIG_H
#define AX_CONFIG_H

/* ------------------------------------------------------------------ */
/* Paginacion                                                          */
/* ------------------------------------------------------------------ */
#define AX_PAGE_SIZE            0x1000u
#define AX_PAGE_SHIFT           12u
#define AX_PAGES_PER_PT         512u
#define AX_PAGES_PER_PD         512u
#define AX_PAGES_PER_PDPT       512u
#define AX_PDT_ENTRIES          512u
#define AX_PML4_ENTRIES         512u

/* ------------------------------------------------------------------ */
/* Mapa fisico de la memoria del kernel                               */
/*                                                                     */
/*  0x00000000 - 0x000FFFFF  BIOS / legacy / tablas de paginas del    */
/*                             firmware (no se mapea en user mode)      */
/*  0x00100000 - 0x001FFFFF  (disponible para user mode)               */
/*  0x00200000 - 0x021FFFFF  Region reservada del kernel (32 MiB)      */
/*      0x00200000  imagen del kernel (ELF)                             */
/*      0x00A00000  tablas de paginas (kernel + low map)               */
/*      0x00E00000  bitmap del PMM                                     */
/*      0x00F00000  heap del kernel                                    */
/*      0x01700000  area de staging para modulos                       */
/*      0x01F00000  initramfs (AXFS)                                  */
/*  0x02200000 - fin de la region reservada                            */
/* ------------------------------------------------------------------ */
#define AX_KERNEL_PHYS_BASE     0x0000000000200000ull
#define AX_KERNEL_REGION_SIZE   0x0000000002000000ull   /* 32 MiB        */

#define AX_KERNEL_IMAGE_PHYS    0x0000000000200000ull
#define AX_KERNEL_IMAGE_MAX     0x0000000000800000ull   /* 8 MiB         */

#define AX_PTABLES_PHYS         0x0000000000A00000ull
#define AX_PTABLES_MAX          0x0000000000400000ull   /* 4 MiB         */

#define AX_BITMAP_PHYS          0x0000000000E00000ull
#define AX_BITMAP_MAX           0x0000000000100000ull   /* 1 MiB         */

#define AX_HEAP_PHYS            0x0000000000F00000ull
#define AX_HEAP_SIZE            0x0000000000800000ull   /* 8 MiB         */

#define AX_MODSTAGE_PHYS        0x0000000001700000ull
#define AX_MODSTAGE_SIZE        0x0000000000800000ull   /* 8 MiB         */

#define AX_INITRD_PHYS          0x0000000001F00000ull
#define AX_INITRD_MAX           0x0000000000100000ull   /* 1 MiB         */

/* ------------------------------------------------------------------ */
/* Mapa virtual del kernel (higher half)                               */
/* ------------------------------------------------------------------ */
#define AX_KERNEL_VIRT_BASE     0xFFFFFFFF80200000ull
#define AX_KERNEL_VIRT_END      0xFFFFFFFF82200000ull   /* 32 MiB        */
#define AX_MODULES_VIRT_BASE    0xFFFFFFFF82200000ull   /* 16 slots x 1M */
#define AX_MODULES_SLOT_SIZE    0x0000000000100000ull   /* 1 MiB         */
#define AX_MODULES_MAX          16
#define AX_MODULES_VIRT_END     0xFFFFFFFF83200000ull

/* Ventana usada para mapear paginas de tabla en el kernel (shadow)     */
#define AX_SHADOW_VIRT_BASE     0xFFFFFFFFA0000000ull

/* ------------------------------------------------------------------ */
/* Mapa virtual de user mode (single address space, low half)          */
/*                                                                     */
/* Cada aplicacion en ring 3 ocupa una "ranura" (slot) de 16 MiB.      */
/* La ranura n vive en AX_USER_VIRT_BASE + n*AX_USER_SLOT_SIZE.        */
/*                                                                     */
/* La base NO puede estar dentro de la region reservada del kernel     */
/* (0x200000-0x2200000), porque ahi el mapa de identidad es de         */
/* supervisor: una pagina de user mode provocaria un #PF.  Por eso la  */
/* region de user mode empieza en 64 MiB, justo por encima del final   */
/* de la region reservada, donde el mapa de identidad ya es de user.   */
/*                                                                     */
/* Dentro de cada ranura:                                             */
/*                                                                     */
/*   +0x0000000  imagen de la aplicacion (ELF)                         */
/*   ...                                                                */
/*   +0xF00000   pila de user mode (1 MiB, crece hacia abajo)         */
/*   +0x100000   fin de la ranura                                      */
/* ------------------------------------------------------------------ */
#define AX_USER_VIRT_BASE       0x0000000004000000ull   /* 64 MiB        */
#define AX_USER_SLOT_SIZE       0x0000000000100000ull   /* 16 MiB        */
#define AX_USER_MAX_SLOTS       8
#define AX_USER_STACK_SIZE      0x0000000000100000ull   /* 1 MiB         */

/* Desplazamiento de la cima de la pila de user mode dentro de la ranura. */
#define AX_USER_STACK_OFFSET    (AX_USER_SLOT_SIZE - AX_USER_STACK_SIZE)

/* Direccion virtual de la region de user mode completa. */
#define AX_USER_REGION_END      (AX_USER_VIRT_BASE + AX_USER_MAX_SLOTS * AX_USER_SLOT_SIZE)

/* ------------------------------------------------------------------ */
/* Tabla de descriptores y segmentos                                   */
/* ------------------------------------------------------------------ */
#define AX_GDT_KERNEL_CODE      0x08u
#define AX_GDT_KERNEL_DATA      0x10u
#define AX_GDT_USER_CODE        0x20u
#define AX_GDT_USER_DATA        0x24u
#define AX_GDT_TSS              0x28u
#define AX_SELECTOR_KERNEL_CODE 0x08u
#define AX_SELECTOR_KERNEL_DATA 0x10u
#define AX_SELECTOR_USER_CODE   0x23u
#define AX_SELECTOR_USER_DATA   0x2Bu

/* ------------------------------------------------------------------ */
/* Syscalls                                                            */
/* ------------------------------------------------------------------ */
#define AX_SYSCALL_VECTOR       0x80
#define AX_SYSCALL_MAX          64

/* ------------------------------------------------------------------ */
/* Planificacion / hilos                                               */
/* ------------------------------------------------------------------ */
#define AX_MAX_THREADS          64
#define AX_THREAD_NAME_LEN      32
#define AX_KERNEL_STACK_SIZE    0x0000000000100000ull   /* 64 KiB        */
#define AX_TIMER_FREQ_HZ        100

/* ------------------------------------------------------------------ */
/* IPC                                                                */
/* ------------------------------------------------------------------ */
#define AX_IPC_MAX_PORTS        32
#define AX_IPC_MAX_MSG          64
#define AX_IPC_MSG_MAX          4096

/* ------------------------------------------------------------------ */
/* Consola                                                            */
/* ------------------------------------------------------------------ */
#define AX_VGA_WIDTH            80
#define AX_VGA_HEIGHT           25
#define AX_VGA_BUFFER           0x000B8000ull
#define AX_SERIAL_PORT          0x3F8

/* ------------------------------------------------------------------ */
/* Version                                                            */
/* ------------------------------------------------------------------ */
#define AX_VERSION_MAJOR        0
#define AX_VERSION_MINOR        1
#define AX_VERSION_PATCH        0
#define AX_VERSION_STRING       "0.1.0"
#define AX_KERNEL_NAME          "AxolynOS"

#endif /* AX_CONFIG_H */
