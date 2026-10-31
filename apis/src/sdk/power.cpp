/*
 * AxolynOS SDK - gestion de energia
 */
#include "axos/sdk.h"

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