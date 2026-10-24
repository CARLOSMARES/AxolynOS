#ifndef AX_UEFI_BOOTINFO_H
#define AX_UEFI_BOOTINFO_H

#include <stdint.h>

typedef struct {
    uint32_t type;
    uint64_t phys_start;
    uint64_t virt_start;
    uint64_t num_pages;
    uint64_t attribute;
} AxUefiMemDesc;

typedef struct {
    uint64_t magic;
    uint64_t kernel_phys;
    uint64_t kernel_virt;
    uint64_t initrd_phys;
    uint64_t initrd_size;
    uint64_t mem_size;
    uint32_t e820_entries;
    uint64_t e820_ptr;
    uint64_t cmdline;
    uint32_t mmap_size;
    uint32_t mmap_desc_size;
    uint32_t mmap_desc_ver;
    uint64_t mmap_ptr;
    uint64_t fb_addr;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    uint32_t fb_bpp;
} AxBootInfoUefi;

#endif /* AX_UEFI_BOOTINFO_H */
