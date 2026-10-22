/*
 * AxolynOS - IPC por puertos de mensajes.
 *
 * Cada puerto es un buzon con una cola circular de mensajes.  Un mensaje es
 * un bloque de hasta AX_IPC_MSG_MAX bytes con el identificador del hilo
 * remitente.
 *
 * El receptor que espera (ax_ipc_recv) se bloquea si el buzon esta vacio;
 * el que no puede esperar (ax_ipc_try_recv) devuelve -AX_EAGAIN.
 *
 * Documentacion: manuales/07-ipc.md
 */

#include "ax_types.h"
#include "ax_ipc.h"
#include "ax_sched.h"
#include "ax_console.h"

/* ------------------------------------------------------------------ */
/* Estado                                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    uint64_t len;
    uint64_t sender;
    uint8_t  data[AX_IPC_MSG_MAX];
} ax_ipc_slot;

typedef struct {
    bool        used;
    ax_ipc_slot slots[AX_IPC_MAX_MSG];
    uint32_t    head;       /* indice de la siguiente escritura */
    uint32_t    count;      /* mensajes en cola                */
} ax_ipc_port;

static ax_ipc_port g_ports[AX_IPC_MAX_PORTS];

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */
static void ax_memcpy(void* dst, const void* src, uint64_t n)
{
    uint8_t*       d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) {
        *d++ = *s++;
    }
}

/* Valida un numero de puerto.  AX_IPC_PORT_ANY no es valido para recv. */
static bool ax_port_valid(int port)
{
    return port >= 0 && port < AX_IPC_MAX_PORTS;
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */
void ax_ipc_init(void)
{
    for (int i = 0; i < AX_IPC_MAX_PORTS; ++i) {
        g_ports[i].used = false;
        g_ports[i].head = 0;
        g_ports[i].count = 0;
    }
}

int ax_ipc_send(int port, const void* data, uint64_t len)
{
    if (!ax_port_valid(port)) {
        return -AX_EINVAL;
    }
    if (!data && len > 0) {
        return -AX_EINVAL;
    }
    if (len > AX_IPC_MSG_MAX) {
        return -AX_EINVAL;
    }

    ax_ipc_port* p = &g_ports[port];
    if (p->count >= AX_IPC_MAX_MSG) {
        return -AX_EBUSY;   /* el buzon esta lleno */
    }

    p->used = true;

    ax_ipc_slot* slot = &p->slots[p->head];
    slot->len   = len;
    slot->sender = 0;

    AxThread* cur = ax_current_thread();
    if (cur) {
        slot->sender = cur->id;
    }

    if (len > 0) {
        ax_memcpy(slot->data, data, len);
    }

    p->head = (p->head + 1) % AX_IPC_MAX_MSG;
    p->count++;

    return (int)len;
}

int ax_ipc_try_recv(int port, void* buf, uint64_t max)
{
    if (!ax_port_valid(port)) {
        return -AX_EINVAL;
    }
    if (!buf && max > 0) {
        return -AX_EINVAL;
    }

    ax_ipc_port* p = &g_ports[port];
    if (p->count == 0) {
        return -AX_EAGAIN;
    }

    uint32_t    idx  = (p->head + AX_IPC_MAX_MSG - p->count) % AX_IPC_MAX_MSG;
    ax_ipc_slot* slot = &p->slots[idx];

    uint64_t n = slot->len;
    if (n > max) {
        n = max;
    }
    if (n > 0) {
        ax_memcpy(buf, slot->data, n);
    }

    p->count--;
    return (int)n;
}

int ax_ipc_recv(int port, void* buf, uint64_t max)
{
    if (!ax_port_valid(port)) {
        return -AX_EINVAL;
    }

    for (;;) {
        int r = ax_ipc_try_recv(port, buf, max);
        if (r != -AX_EAGAIN) {
            return r;
        }
        /* No hay ningun mensaje: el hilo se duerme hasta que otro lo deje. */
        ax_thread_sleep(10);
    }
}

int ax_ipc_pending(int port)
{
    if (!ax_port_valid(port)) {
        return 0;
    }
    return (int)g_ports[port].count;
}
