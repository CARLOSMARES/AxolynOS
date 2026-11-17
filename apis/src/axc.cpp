/*
 * AxolynOS - Configuracion de aplicaciones .axc (implementacion de usuario).
 */
#include "axos/axc.h"

long ax_axc_list(unsigned int index, AxcInfo* out)
{
    if (out == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall2(AX_SYS_AXC_LIST, (long)index, (long)out);
}

long ax_axc_load(const char* path)
{
    if (path == nullptr || path[0] == '\0') {
        return AX_EINVAL;
    }
    return ax_syscall1(AX_SYS_AXC_LOAD, (long)path);
}

long ax_axc_conn(unsigned int index, unsigned int conn_index,
                 AxcConnection* out)
{
    if (out == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall3(AX_SYS_AXC_CONN, (long)index, (long)conn_index, (long)out);
}

long ax_axc_find(const char* name)
{
    if (name == nullptr || name[0] == '\0') {
        return AX_EINVAL;
    }
    return ax_syscall1(AX_SYS_AXC_FIND, (long)name);
}

long ax_axc_connection(const char* name, const char* key,
                       char* out, unsigned long max)
{
    if (name == nullptr || key == nullptr || out == nullptr || max == 0) {
        return AX_EINVAL;
    }
    return ax_syscall4(AX_SYS_AXC_CONNECTION, (long)name, (long)key,
                       (long)out, (long)max);
}
