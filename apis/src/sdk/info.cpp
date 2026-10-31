/*
 * AxolynOS SDK - informacion del sistema
 */
#include "axos/sdk.h"

int sdk_sysinfo(AxSysInfo* info)
{
    if (!info) return AX_EINVAL;
    return (int)ax_syscall6(AX_SYS_SYSINFO, (ax_u64)info, 0, 0, 0, 0, 0);
}

int sdk_meminfo(AxMemInfo* info)
{
    if (!info) return AX_EINVAL;
    return (int)ax_syscall6(AX_SYS_MEMINFO, (ax_u64)info, 0, 0, 0, 0, 0);
}