/*
 * AxolynOS SDK
 */
#include "axos/sdk.h"
#include "axos/syscall.h"
#include "axos/console.h"

void sdk_init(void)
{
}

int sdk_version(void)
{
    return 10000;
}

int sdk_poweroff(void)
{
    return (int)ax_syscall6(AX_SYS_POWEROFF, 0, 0, 0, 0, 0, 0);
}

int sdk_reboot(void)
{
    return (int)ax_syscall6(AX_SYS_REBOOT, 0, 0, 0, 0, 0, 0);
}

int sdk_suspend(void)
{
    return (int)ax_syscall6(AX_SYS_SUSPEND, 0, 0, 0, 0, 0, 0);
}

void sdk_halt(void)
{
    ax_syscall6(AX_SYS_HALT, 0, 0, 0, 0, 0, 0);
}

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
