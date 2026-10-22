/*
 * AxolynOS - API de IPC (comunicacion entre procesos por puertos).
 *
 * Syscalls usadas (ver axos/abi.h):
 *
 *      AX_SYS_IPC_SEND (12) rdi=puerto rsi=buf rdx=len -> bytes enviados
 *      AX_SYS_IPC_RECV (13) rdi=puerto rsi=buf rdx=max -> bytes recibidos
 *
 * Un "puerto" es un numero entero positivo que identifica un buzon de
 * mensajes en el kernel (ver AX_IPC_MAX_PORTS en Kernel/include/ax_config.h).
 *
 * Convencion de retorno: >= 0 exito (bytes transferidos), < 0 error (-errno).
 */
#ifndef AXOS_IPC_H
#define AXOS_IPC_H

#include "axos/abi.h"
#include "axos/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Envia un mensaje al puerto indicado.
 *   port : numero de puerto (1 .. AX_IPC_MAX_PORTS).
 *   buf  : datos a enviar.
 *   len  : numero de bytes.
 * Devuelve los bytes enviados o -errno.
 */
long ax_ipc_send(int port, const void* buf, unsigned long len);

/*
 * Recibe un mensaje del puerto indicado.
 *   port : numero de puerto.
 *   buf  : buffer de destino.
 *   max  : capacidad del buffer.
 * Devuelve los bytes recibidos o -errno.
 */
long ax_ipc_recv(int port, void* buf, unsigned long max);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AXOS_IPC_H */
