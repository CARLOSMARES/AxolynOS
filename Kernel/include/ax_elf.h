/*
 * AxolynOS - Cargador de ELF64 para aplicaciones en user mode.
 */
#ifndef AX_ELF_H
#define AX_ELF_H

#include "ax_types.h"

#define AX_ELF_NIDENT 16

typedef struct {
    uint8_t  e_ident[AX_ELF_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) ax_elf64_ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) ax_elf64_phdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} __attribute__((packed)) ax_elf64_shdr;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} __attribute__((packed)) ax_elf64_rela;

#define AX_ELF_PT_LOAD 1
#define AX_ELF_PT_DYNAMIC 2
#define AX_ELF_PT_INTERP 3

#define AX_ELF_DT_NULL     0
#define AX_ELF_DT_RELA     7
#define AX_ELF_DT_RELASZ   8
#define AX_ELF_DT_RELAENT  9
#define AX_ELF_DT_REL      17
#define AX_ELF_DT_RELSZ    18
#define AX_ELF_DT_RELENT   19
#define AX_ELF_DT_JMPREL   23
#define AX_ELF_DT_PLTRELSZ 2
#define AX_ELF_DT_PLTREL   20

#define AX_ELF_R_X86_64_NONE       0
#define AX_ELF_R_X86_64_RELATIVE  8
#define AX_ELF_R_X86_64_GLOB_DAT  6
#define AX_ELF_R_X86_64_JUMP_SLOT 7
#define AX_ELF_R_X86_64_64        1

/*
 * Carga una imagen ELF (ET_DYN PIE o ET_EXEC) en user mode.
 *  - vaddr_base: direccion virtual donde se colocara la imagen
 *  - entry_out:  recibe el punto de entrada virtual
 *  - stack_top:  pila de user mode ya reservada por el llamante
 * Devuelve 0 o un codigo de error negativo.
 */
int ax_elf_load_user(const void* image, uint64_t size, uint64_t vaddr_base,
                     uint64_t* entry_out);

/* Extrae metadatos basicos sin mapear nada. */
int ax_elf_inspect(const void* image, uint64_t size, uint64_t* entry,
                    uint64_t* lowest_vaddr, uint64_t* highest_vaddr);

#endif /* AX_ELF_H */
