/*
 * AxolynOS - Paginacion por demanda y swap (implementacion de usuario).
 */
#include "axos/paging.h"

long ax_paging_info(AxPagingInfo* out)
{
    if (out == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall1(AX_SYS_PAGEINFO, (long)out);
}

long ax_paging_swap_out(unsigned long addr)
{
    return ax_syscall1(AX_SYS_SWAP_OUT, (long)addr);
}

long ax_paging_swap_enable(const char* path)
{
    return ax_syscall1(AX_SYS_SWAP_ENABLE, (long)path);
}

long ax_paging_swap_disable(void)
{
    return ax_syscall0(AX_SYS_SWAP_DISABLE);
}
