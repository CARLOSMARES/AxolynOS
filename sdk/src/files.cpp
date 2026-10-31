/*
 * AxolynOS SDK - manejo de archivos
 */
#include "../include/axos/sdk.h"
#include <axos/syscall.h>

/* Verificar si un archivo existe */
int sdk_file_exists(const char* path)
{
    if (!path || !path[0]) return AX_EINVAL;
    int fd = (int)ax_syscall2(AX_SYS_OPEN, (long)path, AX_O_RDONLY);
    if (fd >= 0) {
        ax_syscall1(AX_SYS_CLOSE, fd);
        return 0;
    }
    return -1;
}

/* Leer archivo completo */
int sdk_file_read(const char* path, void* buf, ax_u32 max_size)
{
    if (!path || !path[0] || !buf || max_size == 0) return AX_EINVAL;
    int fd = (int)ax_syscall2(AX_SYS_OPEN, (long)path, AX_O_RDONLY);
    if (fd < 0) return fd;
    ax_u32 bytes_read = (ax_u32)ax_syscall3(AX_SYS_READ, fd, (ax_u64)buf, max_size);
    ax_syscall1(AX_SYS_CLOSE, fd);
    return bytes_read;
}

/* Escribir archivo */
int sdk_file_write(const char* path, const void* buf, ax_u32 size)
{
    if (!path || !path[0] || !buf || size == 0) return AX_EINVAL;
    int fd = (int)ax_syscall2(AX_SYS_OPEN, (long)path, AX_O_WRONLY | AX_O_CREAT | AX_O_TRUNC);
    if (fd < 0) return fd;
    ax_u32 bytes_written = (ax_u32)ax_syscall3(AX_SYS_WRITE, fd, (ax_u64)buf, size);
    ax_syscall1(AX_SYS_CLOSE, fd);
    return bytes_written;
}

/* Eliminar archivo */
int sdk_file_delete(const char* path)
{
    if (!path || !path[0]) return AX_EINVAL;
    return (int)ax_syscall1(AX_SYS_CLOSE, (long)path);
}