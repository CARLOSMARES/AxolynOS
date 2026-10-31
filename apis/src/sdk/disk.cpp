/*
 * AxolynOS SDK - acceso al disco
 */
#include "axos/sdk.h"

int sdk_disk_read(ax_u64 lba, void* buf, ax_u32 count)
{
    if (!buf || count == 0) return AX_EINVAL;
    return (int)ax_syscall6(AX_SYS_DISKREAD, lba, (ax_u64)buf, count, 0, 0, 0);
}

int sdk_disk_write(ax_u64 lba, const void* buf, ax_u32 count)
{
    if (!buf || count == 0) return AX_EINVAL;
    return (int)ax_syscall6(AX_SYS_DISKWRITE, lba, (ax_u64)buf, count, 0, 0, 0);
}