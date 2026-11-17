/*
 * AxolynOS - Archivos de sistema .axsys (implementacion de usuario).
 */
#include "axos/sysfile.h"

long ax_sysfile_list(unsigned int index, AxSysFileInfo* out)
{
    if (out == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall2(AX_SYS_SYSFILE_LIST, (long)index, (long)out);
}

long ax_sysfile_load(const char* path)
{
    if (path == nullptr || path[0] == '\0') {
        return AX_EINVAL;
    }
    return ax_syscall1(AX_SYS_SYSFILE_LOAD, (long)path);
}

long ax_sysfile_meta(unsigned int index, const char* key,
                     char* out, unsigned long max)
{
    if (key == nullptr || out == nullptr || max == 0) {
        return AX_EINVAL;
    }
    return ax_syscall4(AX_SYS_SYSFILE_META, (long)index, (long)key,
                       (long)out, (long)max);
}
