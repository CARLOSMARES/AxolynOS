/*
 * AxolynOS - API de modulos del kernel (implementacion).
 */
#include "axos/module.h"

long ax_mod_list(AxModInfo* out, unsigned long max)
{
    if (out == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall2(AX_SYS_MODLIST, (long)out, (long)max);
}

long ax_mod_load(const char* name)
{
    if (name == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall1(AX_SYS_MODLOAD, (long)name);
}

long ax_mod_unload(unsigned int index)
{
    return ax_syscall1(AX_SYS_MODUNLOAD, (long)index);
}
