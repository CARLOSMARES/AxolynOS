/*
 * AxolynOS - Paginacion por demanda y swap para aplicaciones.
 *
 * Syscalls usadas (ver axos/abi.h):
 *
 *      AX_SYS_PAGEINFO     (51) rdi=AxPagingInfo*
 *      AX_SYS_SWAP_OUT     (52) rdi=direccion de user mode
 *      AX_SYS_SWAP_ENABLE  (53) rdi=ruta (0 = por defecto)
 *      AX_SYS_SWAP_DISABLE (54)
 *
 * Convencion de retorno: >= 0 exito, < 0 error (-errno).
 */
#ifndef AXOS_PAGING_H
#define AXOS_PAGING_H

#include "axos/abi.h"
#include "axos/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Informacion del sistema de paginacion. */
long ax_paging_info(AxPagingInfo* out);

/* Evaca una pagina de user mode al fichero de intercambio. */
long ax_paging_swap_out(unsigned long addr);

/* Activa el intercambio (path = 0 usa /system/pagefile.axswap). */
long ax_paging_swap_enable(const char* path);

/* Desactiva la evacuacion (las paginas ya evacuadas siguen recuperandose). */
long ax_paging_swap_disable(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace axos {

/* Instantanea del estado de la paginacion. */
struct PagingInfo {
    ax_u64 total_pages;
    ax_u64 mapped_pages;
    ax_u64 free_pages;
    ax_u64 swapped_pages;
    ax_u64 page_faults;
    ax_u64 swap_ins;
    ax_u64 swap_outs;
    ax_u64 swap_enabled;
    ax_u64 swap_slots;
    ax_u64 swap_used;
    ax_u64 swap_bytes;
    long   error;
};

inline PagingInfo paging_info() noexcept
{
    PagingInfo   p{};
    AxPagingInfo raw{};
    long         r = ax_paging_info(&raw);
    p.error = r;
    if (r >= 0) {
        p.total_pages   = raw.total_pages;
        p.mapped_pages  = raw.mapped_pages;
        p.free_pages    = raw.free_pages;
        p.swapped_pages = raw.swapped_pages;
        p.page_faults   = raw.page_faults;
        p.swap_ins      = raw.swap_ins;
        p.swap_outs     = raw.swap_outs;
        p.swap_enabled  = raw.swap_enabled;
        p.swap_slots    = raw.swap_slots;
        p.swap_used     = raw.swap_used;
        p.swap_bytes    = raw.swap_bytes;
    }
    return p;
}

inline long swap_out(unsigned long addr) noexcept
{
    return ax_paging_swap_out(addr);
}

inline long swap_enable(const char* path = nullptr) noexcept
{
    return ax_paging_swap_enable(path);
}

inline long swap_disable() noexcept
{
    return ax_paging_swap_disable();
}

} /* namespace axos */

#endif /* __cplusplus */

#endif /* AXOS_PAGING_H */
