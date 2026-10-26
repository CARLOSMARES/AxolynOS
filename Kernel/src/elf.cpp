/*
 * AxolynOS - Cargador de ELF64 para aplicaciones en user mode.
 *
 * Las aplicaciones de libaxos se compilan como PIE (-fPIE -pie) y se
 * enlazan con apis/linker/userspace.ld, que las deja con direcciones
 * virtuales 0 (vaddr = offset).  El cargador las coloca en una ranura de
 * user mode:
 *
 *   direccion real = AX_USER_VIRT_BASE + ranura*AX_USER_SLOT_SIZE + vaddr
 *
 * Como el mapa de identidad cubre esa region, la direccion real coincide
 * con la fisica y no hay que reservar frames nuevos.
 *
 * Las relocaciones R_X86_64_RELATIVE (que es lo unico que necesita un PIE
 * sin PLT dinamica) se resuelven anadiendo la base de carga al valor
 * almacenado en el sitio de la relocacion.
 *
 * Documentacion: manuales/08-crear-aplicaciones.md
 */

#include "ax_types.h"
#include "ax_elf.h"
#include "ax_console.h"
#include "ax_memory.h"

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */
static void ax_memcpy(void* dst, const void* src, uint64_t n)
{
    uint8_t*       d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) {
        *d++ = *s++;
    }
}

static void ax_memset(void* dst, int value, uint64_t n)
{
    uint8_t* d = (uint8_t*)dst;
    while (n--) {
        *d++ = (uint8_t)value;
    }
}

static bool ax_magic_ok(const uint8_t* ident)
{
    return ident[0] == 0x7F && ident[1] == 'E' && ident[2] == 'L' && ident[3] == 'F';
}

/* ------------------------------------------------------------------ */
/* Informacion basica                                                  */
/* ------------------------------------------------------------------ */
int ax_elf_inspect(const void* image, uint64_t size, uint64_t* entry,
                    uint64_t* lowest_vaddr, uint64_t* highest_vaddr)
{
    if (!image || size < sizeof(ax_elf64_ehdr)) {
        return AX_EINVAL;
    }

    const ax_elf64_ehdr* eh = (const ax_elf64_ehdr*)image;
    if (!ax_magic_ok(eh->e_ident)) {
        return AX_EINVAL;
    }
    if (eh->e_ident[4] != 2) {          /* ELFCLASS64 */
        return AX_EINVAL;
    }
    if (eh->e_machine != 62) {          /* EM_X86_64 */
        return AX_EINVAL;
    }

    if (entry) {
        *entry = eh->e_entry;
    }

    uint64_t low  = 0xFFFFFFFFFFFFFFFFull;
    uint64_t high = 0;
    bool     any  = false;

    if (eh->e_phoff && eh->e_phnum &&
        eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize <= size) {
        for (uint16_t i = 0; i < eh->e_phnum; ++i) {
            const ax_elf64_phdr* ph = (const ax_elf64_phdr*)
                ((const uint8_t*)image + eh->e_phoff + (uint64_t)i * eh->e_phentsize);
            if (ph->p_type != AX_ELF_PT_LOAD || ph->p_memsz == 0) {
                continue;
            }
            if (ph->p_vaddr < low) {
                low = ph->p_vaddr;
            }
            if (ph->p_vaddr + ph->p_memsz > high) {
                high = ph->p_vaddr + ph->p_memsz;
            }
            any = true;
        }
    }

    if (lowest_vaddr) {
        *lowest_vaddr = any ? low : 0;
    }
    if (highest_vaddr) {
        *highest_vaddr = any ? high : 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Carga                                                               */
/* ------------------------------------------------------------------ */

/*
 * Copia los segmentos PT_LOAD dentro de la ranura y devuelve la direccion
 * virtual del punto de entrada.
 */
int ax_elf_load_user(const void* image, uint64_t size, uint64_t vaddr_base,
                     uint64_t* entry_out)
{
    if (!image || !entry_out) {
        return AX_EINVAL;
    }
    if (size < sizeof(ax_elf64_ehdr)) {
        return AX_EINVAL;
    }

    const ax_elf64_ehdr* eh = (const ax_elf64_ehdr*)image;
    if (!ax_magic_ok(eh->e_ident) || eh->e_ident[4] != 2 || eh->e_machine != 62) {
        return AX_EINVAL;
    }
    if (eh->e_type != 2 /* ET_EXEC */ && eh->e_type != 3 /* ET_DYN */) {
        return AX_EINVAL;
    }
    if (!eh->e_phnum || !eh->e_phoff) {
        return AX_EINVAL;
    }
    if (eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize > size) {
        return AX_EINVAL;
    }

    /* Para un ET_EXEC las direcciones ya son absolutas: la base es 0. */
    uint64_t base = (eh->e_type == 3) ? vaddr_base : 0;

    uint64_t lowest  = 0xFFFFFFFFFFFFFFFFull;
    uint64_t highest = 0;

    /* --- Segmentos ------------------------------------------------- */
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const ax_elf64_phdr* ph = (const ax_elf64_phdr*)
            ((const uint8_t*)image + eh->e_phoff + (uint64_t)i * eh->e_phentsize);

        if (ph->p_type != AX_ELF_PT_LOAD || ph->p_memsz == 0) {
            continue;
        }
        if (ph->p_offset + ph->p_filesz > size) {
            return AX_EINVAL;
        }
        if (ph->p_vaddr + ph->p_memsz < ph->p_vaddr) {
            return AX_EINVAL;
        }

        uint64_t dest = base + ph->p_vaddr;

        /* El destino tiene que estar dentro de la region de user mode. */
        if (dest < AX_USER_VIRT_BASE ||
            dest + ph->p_memsz > AX_USER_REGION_END) {
            return AX_ERANGE;
        }
        /* Y la pagina tiene que estar mapeada (el mapa de identidad cubre
           la region de user mode si hay suficiente RAM). */
        if (ax_vmm_translate(dest) == 0) {
            return AX_ENOMEM;
        }

        if (ph->p_filesz > 0) {
            ax_memcpy((void*)(uintptr_t)dest,
                      (const uint8_t*)image + ph->p_offset, ph->p_filesz);
        }
        if (ph->p_memsz > ph->p_filesz) {
            ax_memset((void*)(uintptr_t)(dest + ph->p_filesz), 0,
                      ph->p_memsz - ph->p_filesz);
        }

        if (ph->p_vaddr < lowest) {
            lowest = ph->p_vaddr;
        }
        if (ph->p_vaddr + ph->p_memsz > highest) {
            highest = ph->p_vaddr + ph->p_memsz;
        }
    }

    if (highest == 0) {
        return AX_EINVAL;
    }

    /* --- Relocaciones ---------------------------------------------- */
    /*
     * Solo se necesitan las R_X86_64_RELATIVE: las aplicaciones se enlazan
     * sin dependencias dinamicas, asi que no hay GOT/PLT que resolver.
     */
    const ax_elf64_phdr* dyn = nullptr;
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const ax_elf64_phdr* ph = (const ax_elf64_phdr*)
            ((const uint8_t*)image + eh->e_phoff + (uint64_t)i * eh->e_phentsize);
        if (ph->p_type == AX_ELF_PT_DYNAMIC) {
            dyn = ph;
            break;
        }
    }

    if (dyn && dyn->p_offset + dyn->p_filesz <= size) {
        const uint64_t* tags = (const uint64_t*)
            ((const uint8_t*)image + dyn->p_offset);
        uint64_t rela_off   = 0;
        uint64_t rela_size  = 0;
        uint64_t rela_ent   = sizeof(ax_elf64_rela);
        uint64_t jmprel_off = 0;
        uint64_t jmprel_sz  = 0;

        for (uint64_t t = 0; t + 1 < dyn->p_filesz / 8; ++t) {
            uint64_t tag = tags[t];
            uint64_t val = tags[t + 1];
            switch (tag) {
                case AX_ELF_DT_RELA:    rela_off = val; break;
                case AX_ELF_DT_RELASZ:  rela_size = val; break;
                case AX_ELF_DT_RELAENT: rela_ent = val ? val : sizeof(ax_elf64_rela); break;
                case AX_ELF_DT_JMPREL:  jmprel_off = val; break;
                case AX_ELF_DT_PLTRELSZ: jmprel_sz = val; break;
                case AX_ELF_DT_NULL:    t = dyn->p_filesz / 8; break;
                default: break;
            }
        }

        if (rela_ent < sizeof(ax_elf64_rela)) {
            rela_ent = sizeof(ax_elf64_rela);
        }

        /* Relaaciones normales + las de PLT (que en un PIE tambien son
           RELATIVE cuando la PLT es local). */
        for (int pass = 0; pass < 2; ++pass) {
            uint64_t off  = (pass == 0) ? rela_off : jmprel_off;
            uint64_t sz   = (pass == 0) ? rela_size : jmprel_sz;
            if (!off || !sz) {
                continue;
            }
            if (off + sz > size) {
                return AX_EINVAL;
            }
            for (uint64_t o = 0; o + rela_ent <= sz; o += rela_ent) {
                const ax_elf64_rela* rela = (const ax_elf64_rela*)
                    ((const uint8_t*)image + off + o);
                uint32_t type = (uint32_t)(rela->r_info & 0xFFFFFFFFull);
                uint32_t sym  = (uint32_t)(rela->r_info >> 32);
                (void)sym;

                if (type != AX_ELF_R_X86_64_RELATIVE) {
                    continue;   /* el resto no es necesario en este OS */
                }

                uint64_t where = base + rela->r_offset;
                if (where < AX_USER_VIRT_BASE || where + 8 > AX_USER_REGION_END) {
                    return AX_ERANGE;
                }
                uint64_t* slot = (uint64_t*)(uintptr_t)where;
                *slot = base + (uint64_t)rela->r_addend;
            }
        }
    }

    *entry_out = base + eh->e_entry;
    return 0;
}
