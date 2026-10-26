/*
 * AxolynOS - Estructuras compartidas entre el cargador UEFI y el kernel.
 *
 * AxBootInfo tiene que coincidir EXACTAMENTE con el de
 * apis/include/axos/abi.h y Kernel/include/ax_kernel.h: boot.S copia 9
 * palabras de 64 bits desde la direccion fisica 0x1000 y el kernel las
 * interpreta como AxBootInfo.
 */
#ifndef AX_UEFI_BOOTINFO_H
#define AX_UEFI_BOOTINFO_H

#include <stdint.h>

#define AX_BOOTINFO_MAGIC 0x4158424F4F54494EULL /* "AXBOOTIN" */

typedef struct {
    uint64_t magic;
    uint64_t kernel_phys;
    uint64_t kernel_virt;
    uint64_t initrd_phys;
    uint64_t initrd_size;
    uint64_t mem_size;
    uint64_t e820_entries;
    uint64_t e820_ptr;
    uint64_t cmdline;
} AxBootInfo;

/* Entrada con el mismo formato que la tabla E820 del BIOS. */
typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;      /* 1 = usable */
    uint32_t reserved;
} AxE820Entry;

#define AX_E820_USABLE 1

#endif /* AX_UEFI_BOOTINFO_H */
