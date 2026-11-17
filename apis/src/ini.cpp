/*
 * AxolynOS - Ficheros INI (implementacion de usuario).
 */
#include "axos/ini.h"

long ax_ini_parse(const char* path)
{
    if (path == nullptr || path[0] == '\0') {
        return AX_EINVAL;
    }
    return ax_syscall1(AX_SYS_INI_PARSE, (long)path);
}

long ax_ini_get(const char* section, const char* key,
                char* out, unsigned long max)
{
    if (section == nullptr || key == nullptr || out == nullptr || max == 0) {
        return AX_EINVAL;
    }
    return ax_syscall4(AX_SYS_INI_GET, (long)section, (long)key,
                       (long)out, (long)max);
}

long ax_ini_set(const char* section, const char* key, const char* value)
{
    if (section == nullptr || key == nullptr || value == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall3(AX_SYS_INI_SET, (long)section, (long)key, (long)value);
}

long ax_ini_enum(unsigned int index, AxIniEntry* out)
{
    if (out == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall2(AX_SYS_INI_ENUM, (long)index, (long)out);
}

long ax_ini_save(const char* path)
{
    if (path == nullptr || path[0] == '\0') {
        return AX_EINVAL;
    }
    return ax_syscall1(AX_SYS_INI_SAVE, (long)path);
}
