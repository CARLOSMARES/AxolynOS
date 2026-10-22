/*
 * AxolynOS - API de ficheros (implementacion).
 */
#include "axos/fs.h"
#include "axos/string.h"

long ax_fs_open(const char* path, int flags)
{
    if (path == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall2(AX_SYS_OPEN, (long)path, flags);
}

long ax_fs_read(int fd, void* buf, unsigned long len)
{
    if (buf == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall3(AX_SYS_READ, fd, (long)buf, (long)len);
}

long ax_fs_write(int fd, const void* buf, unsigned long len)
{
    if (buf == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall3(AX_SYS_WRITE, fd, (long)buf, (long)len);
}

long ax_fs_close(int fd)
{
    return ax_syscall1(AX_SYS_CLOSE, fd);
}

long ax_fs_lseek(int fd, long offset, int whence)
{
    return ax_syscall3(AX_SYS_LSEEK, fd, offset, whence);
}

long ax_fs_gets(char* buf, unsigned long max)
{
    if (buf == nullptr || max == 0UL) {
        return AX_EINVAL;
    }
    /* AX_SYS_GETS deja la linea terminada en '\0' dentro del buffer. */
    return ax_syscall2(AX_SYS_GETS, (long)buf, (long)max);
}

long ax_fs_puts(int fd, const char* text)
{
    if (text == nullptr) {
        return AX_EINVAL;
    }
    return ax_fs_write(fd, text, ax_strlen(text));
}
