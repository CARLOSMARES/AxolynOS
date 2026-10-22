/*
 * AxolynOS - IPC por puertos de mensajes.
 */
#ifndef AX_IPC_H
#define AX_IPC_H

#include "ax_types.h"

#define AX_IPC_PORT_ANY (-1)

typedef struct {
    uint64_t len;
    uint64_t sender;
    uint8_t  data[AX_IPC_MSG_MAX];
} ax_ipc_message;

void ax_ipc_init(void);
int  ax_ipc_send(int port, const void* data, uint64_t len);
int  ax_ipc_recv(int port, void* buf, uint64_t max);
int  ax_ipc_try_recv(int port, void* buf, uint64_t max);
int  ax_ipc_pending(int port);

#endif /* AX_IPC_H */
