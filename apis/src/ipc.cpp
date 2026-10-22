/*
 * AxolynOS - API de IPC (implementacion).
 */
#include "axos/ipc.h"

long ax_ipc_send(int port, const void* buf, unsigned long len)
{
    if (buf == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall3(AX_SYS_IPC_SEND, port, (long)buf, (long)len);
}

long ax_ipc_recv(int port, void* buf, unsigned long max)
{
    if (buf == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall3(AX_SYS_IPC_RECV, port, (long)buf, (long)max);
}
