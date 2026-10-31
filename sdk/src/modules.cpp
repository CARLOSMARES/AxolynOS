/*
 * AxolynOS SDK - modulos del kernel
 */
#include "../include/axos/sdk.h"

int sdk_module_load(const char* name)
{
    if (!name || !name[0]) return AX_EINVAL;
    return (int)ax_syscall1(AX_SYS_MODLOAD, (long)name);
}

int sdk_module_unload(unsigned int index)
{
    return (int)ax_syscall1(AX_SYS_MODUNLOAD, (long)index);
}

int sdk_module_list(AxModInfo* out, uint32_t max_me)
{
    if (!out) return AX_EINVAL;
    return (int)ax_syscall2(AX_SYS_MODLIST, (long)out, (long)max_me);
}