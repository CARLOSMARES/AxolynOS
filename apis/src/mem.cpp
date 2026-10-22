/*
 * AxolynOS - Informacion de memoria y heap (implementacion).
 */
#include "axos/mem.h"

long ax_mem_info(AxMemInfo* out)
{
    if (out == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall1(AX_SYS_MEMINFO, (long)out);
}

long ax_heap_info(AxHeapInfo* out)
{
    if (out == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall1(AX_SYS_HEAPINFO, (long)out);
}
