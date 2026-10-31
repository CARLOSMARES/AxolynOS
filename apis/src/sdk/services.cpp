/*
 * AxolynOS SDK - gestion de servicios
 */
#include "axos/sdk.h"

int sdk_service_start(const char* name)
{
    if (!name || !name[0]) return AX_EINVAL;
    return (int)ax_syscall1(AX_SYS_SERVICE_START, (long)name);
}

int sdk_service_stop(const char* name)
{
    if (!name || !name[0]) return AX_EINVAL;
    return (int)ax_syscall1(AX_SYS_SERVICE_STOP, (long)name);
}

int sdk_service_list(AxServiceInfo* info, uint32_t max_me)
{
    if (!info) return AX_EINVAL;
    return (int)ax_syscall2(AX_SYS_SERVICE_LIST, (long)info, (long)max_me);
}