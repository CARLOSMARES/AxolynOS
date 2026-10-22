/*
 * AxolynOS - API de ficheros para aplicaciones de user mode.
 *
 * Syscalls usadas (ver axos/abi.h):
 *
 *      AX_SYS_OPEN  (3) rdi=ruta rsi=flags -> fd | -errno
 *      AX_SYS_READ  (2) rdi=fd rsi=buf rdx=len -> bytes leidos
 *      AX_SYS_WRITE (1) rdi=fd rsi=buf rdx=len -> bytes escritos
 *      AX_SYS_CLOSE (4) rdi=fd -> 0 | -errno
 *      AX_SYS_LSEEK (5) rdi=fd rsi=offset rdx=whence -> nueva posicion
 *      AX_SYS_GETS  (6) rdi=buf rsi=max -> longitud leida
 *
 * Convencion de retorno: >= 0 exito, < 0 error (-errno).
 */
#ifndef AXOS_FS_H
#define AXOS_FS_H

#include "axos/abi.h"
#include "axos/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Abre un fichero.
 *   path  : ruta (soportada por el kernel: "/", "/bin/sh", ...)
 *   flags : combinacion de AX_O_RDONLY, AX_O_WRONLY, AX_O_RDWR,
 *           AX_O_CREAT, AX_O_TRUNC, AX_O_APPEND.
 * Devuelve el descriptor (>= 0) o -errno.
 */
long ax_fs_open(const char* path, int flags);

/* Lee hasta `len` bytes. Devuelve los bytes leidos o -errno. */
long ax_fs_read(int fd, void* buf, unsigned long len);

/* Escribe hasta `len` bytes. Devuelve los bytes escritos o -errno. */
long ax_fs_write(int fd, const void* buf, unsigned long len);

/* Cierra un descriptor. Devuelve 0 o -errno. */
long ax_fs_close(int fd);

/* Reposiciona el descriptor. Devuelve la nueva posicion o -errno. */
long ax_fs_lseek(int fd, long offset, int whence);

/*
 * Lee una linea de texto del descriptor (normalmente 0) y la copia al
 * buffer terminado en '\0'. Devuelve la longitud leida o -errno.
 */
long ax_fs_gets(char* buf, unsigned long max);

/* Escribe una cadena terminada en '\0' en el descriptor. */
long ax_fs_puts(int fd, const char* text);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace axos {

/*
 * File: envoltorio RAII de un descriptor de fichero.
 *
 *  - El constructor abre el fichero; si falla, el objeto queda cerrado
 *    (fd_ == -1) y `ok()` devuelve false.
 *  - El destructor cierra el descriptor si sigue abierto.
 *  - No hay copias: la clase se mueve (move) pero no se copia, de modo
 *    que nunca hay dos objetos que intentionen cerrar el mismo fd.
 */
class File {
public:
    File() noexcept : fd_(-1) {}
    explicit File(const char* path, int flags) noexcept : fd_(-1) { open(path, flags); }
    ~File() noexcept { close(); }

    File(const File&)            = delete;
    File& operator=(const File&) = delete;

    File(File&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

    File& operator=(File&& other) noexcept
    {
        if (this != &other) {
            close();
            fd_     = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    /* Abre un fichero. Devuelve true si el kernel devuelve un fd >= 0. */
    bool open(const char* path, int flags) noexcept
    {
        close();
        long r = ax_fs_open(path, flags);
        if (r < 0) {
            fd_ = -1;
            return false;
        }
        fd_ = (int)r;
        return true;
    }

    /* Cierra el descriptor. Devuelve true si estaba abierto. */
    bool close() noexcept
    {
        if (fd_ < 0) {
            return false;
        }
        ax_fs_close(fd_);
        fd_ = -1;
        return true;
    }

    bool        ok()     const noexcept { return fd_ >= 0; }
    bool        is_open() const noexcept { return fd_ >= 0; }
    int         fd()     const noexcept { return fd_; }
    explicit operator bool() const noexcept { return fd_ >= 0; }

    long read(void* buf, unsigned long len) noexcept
    {
        return ax_fs_read(fd_, buf, len);
    }

    long write(const void* buf, unsigned long len) noexcept
    {
        return ax_fs_write(fd_, buf, len);
    }

    long lseek(long offset, int whence) noexcept
    {
        return ax_fs_lseek(fd_, offset, whence);
    }

    long gets(char* buf, unsigned long max) noexcept
    {
        return ax_fs_gets(buf, max);
    }

    /* Libera la propiedad del fd sin cerrarlo (para casos especiales). */
    int release() noexcept
    {
        int fd = fd_;
        fd_ = -1;
        return fd;
    }

private:
    int fd_;
};

} /* namespace axos */

#endif /* __cplusplus */

#endif /* AXOS_FS_H */
