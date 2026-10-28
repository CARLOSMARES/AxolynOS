/*
 * AxolynOS - Pila de red.
 *
 * Implementa, en orden de capas:
 *
 *   e1000 (Intel 82540EM y compatibles)  -> tramas Ethernet
 *   ARP                                  -> resolucion IP/MAC
 *   IPv4                                 -> encaminamiento y checksum
 *   ICMP                                 -> ping / respuesta de eco
 *   UDP                                  -> datagramas y puerto de espera
 *   DHCP                                 -> configuracion automatica
 *   DNS                                  -> resolucion de nombres
 *   TCP                                  -> conexion fiable de un solo flujo
 *   HTTP                                 -> wget
 *
 * El driver usa sondeo (polling): no depende de IRQ ni de la PIC.  QEMU y
 * VirtualBox emulan el e1000 de forma compatible.  El anillo de recepcion
 * tiene 16 descriptores de 2 KiB y el de transmision 8 descriptores.
 *
 * Direcciones IP: se guardan como enteros con el valor logico
 * 0xAABBCCDD para A.B.C.D.  Al escribir en la cabecera se usa put32(),
 * que emite los bytes en orden de red (big endian).
 */
#include "ax_net.h"
#include "ax_pci.h"
#include "ax_memory.h"
#include "ax_sched.h"
#include "ax_console.h"

/* ------------------------------------------------------------------ */
/* Registros del e1000                                                 */
/* ------------------------------------------------------------------ */
#define E1000_CTRL      0x0000
#define E1000_STATUS    0x0008
#define E1000_ICR       0x00C0
#define E1000_IMS       0x00D0
#define E1000_IMC       0x00D8
#define E1000_RCTL      0x0100
#define E1000_TCTL      0x0400
#define E1000_RDBAL     0x2800
#define E1000_RDBAH     0x2804
#define E1000_RDLEN     0x2808
#define E1000_RDH       0x2810
#define E1000_RDT       0x2818
#define E1000_TDBAL     0x3800
#define E1000_TDBAH     0x3804
#define E1000_TDLEN     0x3808
#define E1000_TDH       0x3810
#define E1000_TDT       0x3818
#define E1000_RAL0      0x5400
#define E1000_RAH0      0x5404

#define E1000_CTRL_RST  (1u << 26)
#define E1000_CTRL_SLU  (1u << 6)
#define E1000_STATUS_LU (1u << 1)

#define E1000_RCTL_EN      (1u << 1)
#define E1000_RCTL_SBP     (1u << 2)
#define E1000_RCTL_BAM     (1u << 15)
#define E1000_RCTL_SECRC   (1u << 26)

#define E1000_TCTL_EN      (1u << 1)
#define E1000_TCTL_PSP     (1u << 3)

#define E1000_TXD_CMD_EOP  (1u << 0)
#define E1000_TXD_CMD_IFCS (1u << 1)
#define E1000_TXD_CMD_RS   (1u << 3)

#define RX_COUNT 16
#define TX_COUNT 8
#define RX_BUF_SIZE 2048
#define NET_FRAME_MAX 1600

#define ETH_ARP  0x0806
#define ETH_IPV4 0x0800

#define IP_ICMP 1
#define IP_TCP  6
#define IP_UDP  17

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#define NET_TCP_RX_MAX 65536

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed)) E1000RxDesc;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed)) E1000TxDesc;

/* ------------------------------------------------------------------ */
/* Estado del driver                                                   */
/* ------------------------------------------------------------------ */
static volatile uint8_t* g_mmio = nullptr;
static bool     g_present = false;
static uint8_t  g_mac[6];
static uint8_t* g_rx_bufs = nullptr;
static E1000RxDesc* g_rx_desc = nullptr;
static E1000TxDesc* g_tx_desc = nullptr;
static uint32_t g_rx_head = 0;
static uint32_t g_tx_tail = 0;

static uint8_t g_frame[NET_FRAME_MAX];
static uint8_t g_rx_packet[NET_FRAME_MAX];
static uint8_t g_ipbuf[NET_FRAME_MAX];

/* ------------------------------------------------------------------ */
/* Estado de la pila                                                   */
/* ------------------------------------------------------------------ */
static bool     g_configured = false;
static bool     g_dhcp_used = false;
static uint32_t g_ip = 0;
static uint32_t g_mask = 0;
static uint32_t g_gateway = 0;
static uint32_t g_dns = 0;
static AxNetInfo g_info;

#define ARP_COUNT 8
typedef struct {
    bool     used;
    uint32_t ip;
    uint8_t  mac[6];
    uint64_t expires;
} ArpEntry;

static ArpEntry g_arp[ARP_COUNT];
static uint32_t g_arp_next = 0;
static uint32_t g_ip_id = 1;

/* Espera de un datagrama UDP. */
static bool     g_udp_active = false;
static bool     g_udp_pending = false;
static uint16_t g_udp_wait_port = 0;
static uint32_t g_udp_src_ip = 0;
static uint16_t g_udp_src_port = 0;
static uint8_t  g_udp_data[2048];
static uint32_t g_udp_len = 0;

/* ICMP. */
static bool     g_ping_active = false;
static bool     g_ping_reply = false;
static uint16_t g_ping_id = 0x4158;
static uint16_t g_ping_seq = 1;
static uint64_t g_ping_start = 0;
static uint32_t g_ping_rtt = 0;

/* TCP (un unico flujo). */
static int      g_tcp_state = 0;          /* 0 cerrado, 1 SYN, 2 establecido, 3 FIN */
static uint16_t g_tcp_lport = 0;
static uint16_t g_tcp_rport = 0;
static uint32_t g_tcp_rip = 0;
static uint32_t g_tcp_snd_nxt = 0;
static uint32_t g_tcp_snd_una = 0;
static uint32_t g_tcp_rcv_nxt = 0;
static uint32_t g_tcp_rx_len = 0;
static bool     g_tcp_fin = false;
static uint16_t g_tcp_ephemeral = 41000;
static uint8_t  g_tcp_rx[NET_TCP_RX_MAX];

static const uint8_t g_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */
static void net_memset(void* dst, int value, uint64_t count)
{
    uint8_t* p = (uint8_t*)dst;
    for (uint64_t i = 0; i < count; ++i) {
        p[i] = (uint8_t)value;
    }
}

static void net_memcpy(void* dst, const void* src, uint64_t count)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint64_t i = 0; i < count; ++i) {
        d[i] = s[i];
    }
}

static uint64_t net_strlen(const char* s)
{
    uint64_t n = 0;
    while (s && s[n]) {
        n++;
    }
    return n;
}

static bool net_strneq(const char* a, const char* b, uint64_t n)
{
    for (uint64_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return true;
        }
        if (!a[i]) {
            return false;
        }
    }
    return false;
}

static void put16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static uint16_t get16(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void put32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t csum_add(const uint8_t* p, uint32_t len, uint32_t sum)
{
    while (len > 1) {
        sum += ((uint16_t)p[0] << 8) | (uint16_t)p[1];
        p += 2;
        len -= 2;
    }
    if (len) {
        sum += (uint16_t)p[0] << 8;
    }
    return sum;
}

static uint16_t csum_finish(uint32_t sum)
{
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

static uint16_t net_checksum(const uint8_t* p, uint32_t len)
{
    return csum_finish(csum_add(p, len, 0));
}

static bool is_broadcast(uint32_t ip)
{
    return ip == 0xFFFFFFFFu;
}

static bool is_multicast(uint32_t ip)
{
    return (ip & 0xF0000000u) == 0xE0000000u;
}

static bool same_subnet(uint32_t a, uint32_t b)
{
    return (a & g_mask) == (b & g_mask);
}

/* ------------------------------------------------------------------ */
/* e1000: acceso a registros                                           */
/* ------------------------------------------------------------------ */
static uint32_t e1000_read(uint32_t reg)
{
    return *(volatile uint32_t*)(g_mmio + reg);
}

static void e1000_write(uint32_t reg, uint32_t value)
{
    *(volatile uint32_t*)(g_mmio + reg) = value;
    (void)e1000_read(reg);
}

static void e1000_delay_ms(uint32_t ms)
{
    ax_thread_sleep(ms);
}

/* ------------------------------------------------------------------ */
/* e1000: anillos                                                      */
/* ------------------------------------------------------------------ */
static bool e1000_send_frame(const void* data, uint32_t len)
{
    if (!g_present || len > NET_FRAME_MAX) {
        return false;
    }
    E1000TxDesc* d = &g_tx_desc[g_tx_tail];
    net_memcpy(g_frame, data, len);
    while (len < 60) {
        g_frame[len++] = 0;
    }
    /* El buffer vive en el kernel alto; el e1000 necesita la direccion
       fisica para el DMA.  ax_vmm_translate() tambien funciona para las
       direcciones bajas de identidad del heap. */
    d->addr = ax_vmm_translate((uint64_t)(uintptr_t)g_frame);
    d->length = (uint16_t)len;
    d->cso = 0;
    d->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    d->status = 0;
    d->css = 0;
    d->special = 0;

    uint32_t tail = (g_tx_tail + 1) % TX_COUNT;
    g_tx_tail = tail;
    e1000_write(E1000_TDT, tail);

    for (int spins = 0; spins < 1000; ++spins) {
        if (d->status & 0x01) {
            return true;
        }
        e1000_delay_ms(1);
    }
    g_info.tx_errors++;
    return false;
}

static bool e1000_receive(uint8_t* out, uint32_t max, uint32_t* out_len)
{
    if (!g_present) {
        return false;
    }
    E1000RxDesc* d = &g_rx_desc[g_rx_head];
    if (!(d->status & 0x01)) {
        return false;
    }

    uint32_t len = d->length;
    if (d->errors) {
        g_info.rx_errors++;
        len = 0;
    }
    if (len > max) {
        len = max;
    }
    if (out && len) {
        net_memcpy(out, (const void*)(uintptr_t)d->addr, len);
    }

    d->status = 0;
    d->length = 0;
    e1000_write(E1000_RDT, g_rx_head);
    g_rx_head = (g_rx_head + 1) % RX_COUNT;
    *out_len = len;
    return true;
}

/* ------------------------------------------------------------------ */
/* Envio y recepcion de tramas Ethernet                                */
/* ------------------------------------------------------------------ */
static bool net_send_frame(const uint8_t dst_mac[6], uint16_t ethertype,
                           const void* payload, uint32_t payload_len)
{
    if (payload_len + 14 > NET_FRAME_MAX) {
        return false;
    }
    net_memcpy(g_frame, dst_mac, 6);
    net_memcpy(g_frame + 6, g_mac, 6);
    put16(g_frame + 12, ethertype);
    net_memcpy(g_frame + 14, payload, payload_len);
    bool ok = e1000_send_frame(g_frame, payload_len + 14);
    if (ok) {
        g_info.tx_packets++;
    } else {
        g_info.tx_errors++;
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* ARP                                                                 */
/* ------------------------------------------------------------------ */
static int arp_lookup(uint32_t ip, uint8_t mac[6])
{
    uint64_t now = ax_uptime_ms();
    for (int i = 0; i < ARP_COUNT; ++i) {
        if (g_arp[i].used && g_arp[i].ip == ip && g_arp[i].expires > now) {
            net_memcpy(mac, g_arp[i].mac, 6);
            return 0;
        }
    }
    return AX_ENOENT;
}

static void arp_learn(uint32_t ip, const uint8_t mac[6])
{
    int index = -1;
    for (int i = 0; i < ARP_COUNT; ++i) {
        if (g_arp[i].used && g_arp[i].ip == ip) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        index = (int)g_arp_next;
        g_arp_next = (g_arp_next + 1) % ARP_COUNT;
    }
    g_arp[index].used = true;
    g_arp[index].ip = ip;
    net_memcpy(g_arp[index].mac, mac, 6);
    g_arp[index].expires = ax_uptime_ms() + 60000;
}

static void net_poll(void);

static void arp_send(uint16_t oper, const uint8_t dst_mac[6],
                     uint32_t dst_ip, uint32_t src_ip)
{
    uint8_t packet[28];
    put16(packet + 0, 1);          /* Ethernet */
    put16(packet + 2, ETH_IPV4);
    packet[4] = 6;
    packet[5] = 4;
    put16(packet + 6, oper);
    net_memcpy(packet + 8, g_mac, 6);
    put32(packet + 14, src_ip);
    net_memcpy(packet + 18, dst_mac, 6);
    put32(packet + 24, dst_ip);

    if (oper == 1) {
        net_send_frame(g_broadcast_mac, ETH_ARP, packet, sizeof(packet));
        g_info.arp_requests++;
    } else {
        net_send_frame(dst_mac, ETH_ARP, packet, sizeof(packet));
        g_info.arp_replies++;
    }
}

static int arp_resolve(uint32_t ip, uint8_t mac[6])
{
    if (is_broadcast(ip) || is_multicast(ip)) {
        net_memcpy(mac, g_broadcast_mac, 6);
        return 0;
    }
    if (ip == 0) {
        net_memcpy(mac, g_broadcast_mac, 6);
        return 0;
    }
    if (arp_lookup(ip, mac) == 0) {
        return 0;
    }

    for (int attempt = 0; attempt < 3; ++attempt) {
        arp_send(1, g_broadcast_mac, ip, g_ip);
        uint64_t start = ax_uptime_ms();
        while (ax_uptime_ms() - start < 250) {
            net_poll();
            if (arp_lookup(ip, mac) == 0) {
                return 0;
            }
            e1000_delay_ms(2);
        }
    }
    return AX_EAGAIN;
}

/* ------------------------------------------------------------------ */
/* DHCP                                                                */
/* ------------------------------------------------------------------ */
static void dhcp_build(uint8_t* pkt, uint8_t type, uint32_t xid,
                       uint32_t requested, uint32_t server)
{
    net_memset(pkt, 0, 300);
    pkt[0] = 1;                      /* BOOTREQUEST */
    pkt[1] = 1;                      /* Ethernet */
    pkt[2] = 6;                      /* longitud MAC */
    put32(pkt + 4, xid);
    put16(pkt + 10, 0x8000);         /* broadcast */
    net_memcpy(pkt + 28, g_mac, 6);
    put32(pkt + 236, 0x63825363u);   /* magic cookie */

    uint32_t o = 240;
    pkt[o++] = 53; pkt[o++] = 1; pkt[o++] = type;
    if (type == 3) {
        pkt[o++] = 50; pkt[o++] = 4; put32(pkt + o, requested); o += 4;
        pkt[o++] = 54; pkt[o++] = 4; put32(pkt + o, server); o += 4;
    }
    pkt[o++] = 55; pkt[o++] = 4; pkt[o++] = 1; pkt[o++] = 3;
    pkt[o++] = 6; pkt[o++] = 51;
    pkt[o++] = 255;
}

typedef struct {
    uint32_t yiaddr;
    uint32_t server;
    uint32_t mask;
    uint32_t gateway;
    uint32_t dns;
    uint8_t  type;
} DhcpOffer;

static bool dhcp_parse(const uint8_t* pkt, uint32_t len, uint32_t xid, DhcpOffer* out)
{
    if (len < 240 || pkt[0] != 2 || get32(pkt + 4) != xid || get32(pkt + 236) != 0x63825363u) {
        return false;
    }
    net_memset(out, 0, sizeof(*out));
    out->yiaddr = get32(pkt + 16);
    out->mask = 0xFFFFFF00u;

    uint32_t i = 240;
    while (i < len) {
        uint8_t code = pkt[i++];
        if (code == 0) {
            continue;
        }
        if (code == 255) {
            break;
        }
        if (i >= len) {
            break;
        }
        uint8_t olen = pkt[i++];
        if (i + olen > len) {
            break;
        }
        if (code == 53 && olen >= 1) {
            out->type = pkt[i];
        } else if (code == 54 && olen >= 4) {
            out->server = get32(pkt + i);
        } else if (code == 1 && olen >= 4) {
            out->mask = get32(pkt + i);
        } else if (code == 3 && olen >= 4) {
            out->gateway = get32(pkt + i);
        } else if (code == 6 && olen >= 4) {
            out->dns = get32(pkt + i);
        }
        i += olen;
    }
    return out->type != 0;
}

/* ------------------------------------------------------------------ */
/* IPv4, UDP, ICMP y TCP: envio                                        */
/* ------------------------------------------------------------------ */
static int ip_send(uint32_t dst, uint8_t proto, const void* payload, uint32_t len)
{
    if (len > 1480) {
        return AX_EINVAL;
    }
    uint8_t hop_mac[6];
    if (is_broadcast(dst)) {
        net_memcpy(hop_mac, g_broadcast_mac, 6);
    } else {
        uint32_t next = dst;
        if (g_gateway && g_mask && !same_subnet(dst, g_ip)) {
            next = g_gateway;
        }
        int rc = arp_resolve(next, hop_mac);
        if (rc != 0) {
            return rc;
        }
    }

    net_memset(g_ipbuf, 0, 20 + len);
    g_ipbuf[0] = 0x45;
    g_ipbuf[1] = 0;
    put16(g_ipbuf + 2, (uint16_t)(20 + len));
    put16(g_ipbuf + 4, (uint16_t)g_ip_id++);
    put16(g_ipbuf + 6, 0x4000);              /* no fragmentar */
    g_ipbuf[8] = 64;
    g_ipbuf[9] = proto;
    put32(g_ipbuf + 12, g_ip);
    put32(g_ipbuf + 16, dst);
    put16(g_ipbuf + 10, net_checksum(g_ipbuf, 20));
    if (len) {
        net_memcpy(g_ipbuf + 20, payload, len);
    }

    if (!net_send_frame(hop_mac, ETH_IPV4, g_ipbuf, 20 + len)) {
        return AX_EIO;
    }
    g_info.ip_out++;
    return 0;
}

static int udp_send(uint16_t src_port, uint32_t dst_ip, uint16_t dst_port,
                    const void* payload, uint32_t len)
{
    if (len > 1400) {
        return AX_EINVAL;
    }
    uint8_t packet[1500];
    uint32_t udp_len = 8 + len;
    put16(packet + 0, src_port);
    put16(packet + 2, dst_port);
    put16(packet + 4, (uint16_t)udp_len);
    put16(packet + 6, 0);
    if (len) {
        net_memcpy(packet + 8, payload, len);
    }

    uint8_t pseudo[12];
    put32(pseudo + 0, g_ip);
    put32(pseudo + 4, dst_ip);
    pseudo[8] = 0;
    pseudo[9] = IP_UDP;
    put16(pseudo + 10, (uint16_t)udp_len);
    uint32_t sum = csum_add(pseudo, 12, 0);
    sum = csum_add(packet, udp_len, sum);
    uint16_t csum = csum_finish(sum);
    if (csum == 0) {
        csum = 0xFFFF;
    }
    put16(packet + 6, csum);

    int rc = ip_send(dst_ip, IP_UDP, packet, udp_len);
    if (rc == 0) {
        g_info.udp_out++;
    }
    return rc;
}

static int tcp_xmit(uint32_t seq, uint8_t flags, const void* data, uint32_t len)
{
    if (len > 1400) {
        return AX_EINVAL;
    }
    uint8_t packet[1500];
    uint32_t tcp_len = 20 + len;
    put16(packet + 0, g_tcp_lport);
    put16(packet + 2, g_tcp_rport);
    put32(packet + 4, seq);
    put32(packet + 8, g_tcp_rcv_nxt);
    packet[12] = 5 << 4;
    packet[13] = flags;
    put16(packet + 14, 64240);
    put16(packet + 16, 0);
    put16(packet + 18, 0);
    if (len) {
        net_memcpy(packet + 20, data, len);
    }

    uint8_t pseudo[12];
    put32(pseudo + 0, g_ip);
    put32(pseudo + 4, g_tcp_rip);
    pseudo[8] = 0;
    pseudo[9] = IP_TCP;
    put16(pseudo + 10, (uint16_t)tcp_len);
    uint32_t sum = csum_add(pseudo, 12, 0);
    sum = csum_add(packet, tcp_len, sum);
    put16(packet + 16, csum_finish(sum));

    int rc = ip_send(g_tcp_rip, IP_TCP, packet, tcp_len);
    if (rc == 0) {
        g_info.tcp_out++;
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* Recepcion                                                           */
/* ------------------------------------------------------------------ */
static void tcp_handle(const uint8_t* packet, uint32_t len)
{
    if (len < 20 || g_tcp_state == 0) {
        return;
    }
    uint16_t sport = get16(packet + 0);
    uint16_t dport = get16(packet + 2);
    if (dport != g_tcp_lport || sport != g_tcp_rport) {
        return;
    }

    uint8_t flags = packet[13];
    uint32_t seq = get32(packet + 4);
    uint32_t ack = get32(packet + 8);
    uint32_t hlen = (uint32_t)(packet[12] >> 4) * 4;
    if (hlen < 20 || hlen > len) {
        return;
    }
    uint32_t payload_len = len - hlen;
    g_info.tcp_in++;

    if (flags & TCP_RST) {
        g_tcp_state = 0;
        g_tcp_fin = true;
        return;
    }

    if (g_tcp_state == 1) {                 /* SYN enviado */
        if ((flags & TCP_SYN) && (flags & TCP_ACK)) {
            g_tcp_rcv_nxt = seq + 1;
            g_tcp_snd_una = ack;
            g_tcp_state = 2;
            tcp_xmit(g_tcp_snd_nxt, TCP_ACK, nullptr, 0);
        }
        return;
    }

    if (flags & TCP_ACK) {
        if (ack > g_tcp_snd_una) {
            g_tcp_snd_una = ack;
        }
    }

    if (payload_len) {
        if (seq == g_tcp_rcv_nxt) {
            uint32_t room = NET_TCP_RX_MAX - g_tcp_rx_len;
            uint32_t copy = payload_len < room ? payload_len : room;
            net_memcpy(g_tcp_rx + g_tcp_rx_len, packet + hlen, copy);
            g_tcp_rx_len += copy;
            g_tcp_rcv_nxt += payload_len;
        }
        tcp_xmit(g_tcp_snd_nxt, TCP_ACK, nullptr, 0);
    }

    if (flags & TCP_FIN) {
        g_tcp_rcv_nxt++;
        tcp_xmit(g_tcp_snd_nxt, TCP_ACK, nullptr, 0);
        g_tcp_fin = true;
        if (g_tcp_state == 3) {
            g_tcp_state = 0;
        }
    }
}

static void icmp_handle(uint32_t src, const uint8_t* packet, uint32_t len)
{
    if (len < 8) {
        return;
    }
    g_info.icmp_in++;
    uint8_t type = packet[0];
    if (type == 0) {                        /* echo reply */
        if (g_ping_active && get16(packet + 4) == g_ping_id &&
            get16(packet + 6) == g_ping_seq) {
            g_ping_reply = true;
            g_ping_rtt = (uint32_t)(ax_uptime_ms() - g_ping_start);
        }
    } else if (type == 8 && g_configured) { /* echo request: responder */
        uint8_t reply[1500];
        if (len > sizeof(reply)) {
            return;
        }
        net_memcpy(reply, packet, len);
        reply[0] = 0;
        put16(reply + 2, 0);
        put16(reply + 2, net_checksum(reply, len));
        ip_send(src, IP_ICMP, reply, len);
    }
}

static void udp_handle(uint32_t src, const uint8_t* packet, uint32_t len)
{
    if (len < 8) {
        return;
    }
    uint16_t sport = get16(packet + 0);
    uint16_t dport = get16(packet + 2);
    uint16_t ulen = get16(packet + 4);
    if (ulen < 8 || ulen > len) {
        return;
    }
    g_info.udp_in++;

    if (g_udp_active && dport == g_udp_wait_port) {
        uint32_t payload_len = ulen - 8;
        if (payload_len > sizeof(g_udp_data)) {
            payload_len = sizeof(g_udp_data);
        }
        net_memcpy(g_udp_data, packet + 8, payload_len);
        g_udp_len = payload_len;
        g_udp_src_ip = src;
        g_udp_src_port = sport;
        g_udp_pending = true;
    }
}

static void ip_handle(const uint8_t* packet, uint32_t len)
{
    if (len < 20 || (packet[0] >> 4) != 4) {
        return;
    }
    uint32_t ihl = (uint32_t)(packet[0] & 0x0F) * 4;
    if (ihl < 20 || ihl > len) {
        return;
    }
    uint32_t total = get16(packet + 2);
    if (total < ihl || total > len) {
        total = len;
    }
    uint32_t dst = get32(packet + 16);
    uint32_t src = get32(packet + 12);

    /* Antes de configurar aceptamos cualquier destino: asi llega el DHCP. */
    if (g_configured && dst != g_ip && !is_broadcast(dst) && !is_multicast(dst)) {
        return;
    }

    g_info.ip_in++;
    switch (packet[9]) {
        case IP_ICMP:
            icmp_handle(src, packet + ihl, total - ihl);
            break;
        case IP_UDP:
            udp_handle(src, packet + ihl, total - ihl);
            break;
        case IP_TCP:
            tcp_handle(packet + ihl, total - ihl);
            break;
        default:
            break;
    }
}

static void arp_handle(const uint8_t* packet, uint32_t len)
{
    if (len < 28) {
        return;
    }
    if (get16(packet + 0) != 1 || get16(packet + 2) != ETH_IPV4 ||
        packet[4] != 6 || packet[5] != 4) {
        return;
    }
    uint16_t oper = get16(packet + 6);
    uint32_t sender_ip = get32(packet + 14);
    const uint8_t* sender_mac = packet + 8;
    uint32_t target_ip = get32(packet + 24);

    arp_learn(sender_ip, sender_mac);

    if (oper == 1 && target_ip == g_ip && g_ip != 0) {
        arp_send(2, sender_mac, sender_ip, g_ip);
    }
}

static void net_poll(void)
{
    for (int packets = 0; packets < 32; ++packets) {
        uint32_t len = 0;
        if (!e1000_receive(g_rx_packet, sizeof(g_rx_packet), &len)) {
            break;
        }
        g_info.rx_packets++;
        if (len < 14) {
            continue;
        }
        uint16_t type = get16(g_rx_packet + 12);
        const uint8_t* payload = g_rx_packet + 14;
        uint32_t payload_len = len - 14;
        if (type == ETH_ARP) {
            arp_handle(payload, payload_len);
        } else if (type == ETH_IPV4) {
            ip_handle(payload, payload_len);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Espera de UDP                                                       */
/* ------------------------------------------------------------------ */
static int udp_wait(uint16_t port, void* out, uint32_t max, uint32_t timeout_ms,
                    uint32_t* from_ip, uint16_t* from_port)
{
    g_udp_wait_port = port;
    g_udp_active = true;
    g_udp_pending = false;
    g_udp_len = 0;

    uint64_t start = ax_uptime_ms();
    while (ax_uptime_ms() - start < timeout_ms) {
        net_poll();
        if (g_udp_pending) {
            uint32_t len = g_udp_len;
            if (len > max) {
                len = max;
            }
            net_memcpy(out, g_udp_data, len);
            if (from_ip) *from_ip = g_udp_src_ip;
            if (from_port) *from_port = g_udp_src_port;
            g_udp_active = false;
            g_udp_pending = false;
            return (int)len;
        }
        e1000_delay_ms(2);
    }
    g_udp_active = false;
    return AX_EAGAIN;
}

/* ------------------------------------------------------------------ */
/* PING                                                                */
/* ------------------------------------------------------------------ */
int ax_net_ping(uint32_t ip, uint32_t timeout_ms, uint32_t* rtt_ms)
{
    if (!g_configured || !g_present) {
        return AX_EAGAIN;
    }
    uint8_t packet[32];
    net_memset(packet, 0, sizeof(packet));
    packet[0] = 8;                          /* echo request */
    packet[1] = 0;
    put16(packet + 4, g_ping_id);
    g_ping_seq++;
    put16(packet + 6, g_ping_seq);
    for (int i = 8; i < 32; ++i) {
        packet[i] = (uint8_t)('a' + (i % 23));
    }
    put16(packet + 2, net_checksum(packet, sizeof(packet)));

    g_ping_active = true;
    g_ping_reply = false;
    g_ping_start = ax_uptime_ms();

    int rc = ip_send(ip, IP_ICMP, packet, sizeof(packet));
    if (rc != 0) {
        g_ping_active = false;
        return rc;
    }
    g_info.icmp_out++;

    uint64_t start = ax_uptime_ms();
    while (ax_uptime_ms() - start < timeout_ms) {
        net_poll();
        if (g_ping_reply) {
            g_ping_active = false;
            if (rtt_ms) *rtt_ms = g_ping_rtt;
            return 0;
        }
        e1000_delay_ms(2);
    }
    g_ping_active = false;
    return AX_EAGAIN;
}

/* ------------------------------------------------------------------ */
/* DHCP                                                                */
/* ------------------------------------------------------------------ */
int ax_net_dhcp(void)
{
    if (!g_present) {
        return AX_ENOENT;
    }
    uint32_t xid = (uint32_t)(ax_uptime_ms() ^ 0x44484350u);
    uint8_t packet[300];
    uint8_t reply[1024];
    DhcpOffer offer;

    dhcp_build(packet, 1, xid, 0, 0);       /* DISCOVER */
    int rc = udp_send(68, 0xFFFFFFFFu, 67, packet, 300);
    if (rc != 0) {
        return rc;
    }
    int n = udp_wait(68, reply, sizeof(reply), 3000, nullptr, nullptr);
    if (n < 0) {
        return n;
    }
    if (!dhcp_parse(reply, (uint32_t)n, xid, &offer) || offer.type != 2) {
        return AX_EIO;
    }

    dhcp_build(packet, 3, xid, offer.yiaddr, offer.server);  /* REQUEST */
    rc = udp_send(68, 0xFFFFFFFFu, 67, packet, 300);
    if (rc != 0) {
        return rc;
    }
    n = udp_wait(68, reply, sizeof(reply), 3000, nullptr, nullptr);
    if (n < 0) {
        return n;
    }
    if (!dhcp_parse(reply, (uint32_t)n, xid, &offer) || offer.type != 5) {
        return AX_EIO;
    }

    g_ip = offer.yiaddr;
    g_mask = offer.mask ? offer.mask : 0xFFFFFF00u;
    g_gateway = offer.gateway;
    g_dns = offer.dns ? offer.dns : offer.gateway;
    g_configured = true;
    g_dhcp_used = true;
    return 0;
}

/* ------------------------------------------------------------------ */
/* DNS                                                                 */
/* ------------------------------------------------------------------ */
static bool parse_ip_literal(const char* text, uint32_t* out)
{
    uint32_t parts[4] = {0, 0, 0, 0};
    int index = 0;
    uint32_t value = 0;
    bool any = false;
    for (const char* p = text; ; ++p) {
        if (*p >= '0' && *p <= '9') {
            value = value * 10 + (uint32_t)(*p - '0');
            any = true;
        } else if (*p == '.' || *p == '\0') {
            if (!any || index > 3 || value > 255) {
                return false;
            }
            parts[index++] = value;
            value = 0;
            any = false;
            if (*p == '\0') {
                break;
            }
        } else {
            return false;
        }
    }
    if (index != 4) {
        return false;
    }
    *out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return true;
}

uint32_t ax_net_parse_ip(const char* text)
{
    uint32_t ip = 0;
    if (text && parse_ip_literal(text, &ip)) {
        return ip;
    }
    return 0;
}

static uint32_t dns_build_query(uint8_t* packet, const char* name)
{
    static uint32_t counter = 0x1200;
    uint32_t id = counter++ & 0xFFFF;
    net_memset(packet, 0, 512);
    put16(packet + 0, (uint16_t)id);
    put16(packet + 2, 0x0100);           /* recursion desired */
    put16(packet + 4, 1);
    uint32_t offset = 12;
    const char* p = name;
    while (*p) {
        const char* dot = p;
        while (*dot && *dot != '.') {
            dot++;
        }
        uint32_t part = (uint32_t)(dot - p);
        if (part == 0 || part > 63 || offset + part + 2 >= 512) {
            return 0;
        }
        packet[offset++] = (uint8_t)part;
        net_memcpy(packet + offset, p, part);
        offset += part;
        if (*dot == '.') {
            dot++;
        }
        p = dot;
    }
    packet[offset++] = 0;
    put16(packet + offset, 1);           /* type A */
    put16(packet + offset + 2, 1);       /* class IN */
    offset += 4;
    return offset;
}

static uint32_t dns_skip_name(const uint8_t* packet, uint32_t len, uint32_t offset)
{
    while (offset < len) {
        uint8_t c = packet[offset++];
        if (c == 0) {
            return offset;
        }
        if ((c & 0xC0) == 0xC0) {
            return offset + 1;
        }
        if ((c & 0xC0) != 0) {
            return len;
        }
        offset += c;
    }
    return len;
}

int ax_net_resolve(const char* name, uint32_t* ip)
{
    if (!name || !ip) {
        return AX_EINVAL;
    }
    if (parse_ip_literal(name, ip)) {
        return 0;
    }
    if (!g_configured || !g_present) {
        return AX_EAGAIN;
    }
    uint32_t server = g_dns ? g_dns : g_gateway;
    if (!server) {
        return AX_ENOENT;
    }

    for (int attempt = 0; attempt < 3; ++attempt) {
        uint8_t query[512];
        uint32_t qlen = dns_build_query(query, name);
        if (qlen == 0) {
            return AX_EINVAL;
        }
        uint16_t local_port = (uint16_t)(53000 + (attempt * 37));
        int rc = udp_send(local_port, server, 53, query, qlen);
        if (rc != 0) {
            return rc;
        }
        uint8_t reply[1024];
        int n = udp_wait(local_port, reply, sizeof(reply), 2000, nullptr, nullptr);
        if (n < 12) {
            continue;
        }
        uint16_t id = get16(reply + 0);
        if (id != get16(query + 0)) {
            continue;
        }
        uint32_t qd = get16(reply + 4);
        uint32_t an = get16(reply + 6);
        uint32_t offset = 12;
        for (uint32_t i = 0; i < qd; ++i) {
            offset = dns_skip_name(reply, (uint32_t)n, offset);
            offset += 4;
        }
        for (uint32_t i = 0; i < an && offset + 10 <= (uint32_t)n; ++i) {
            offset = dns_skip_name(reply, (uint32_t)n, offset);
            if (offset + 10 > (uint32_t)n) {
                break;
            }
            uint16_t type = get16(reply + offset);
            uint16_t cls = get16(reply + offset + 2);
            uint16_t rdlen = get16(reply + offset + 8);
            offset += 10;
            if (offset + rdlen > (uint32_t)n) {
                break;
            }
            if (type == 1 && cls == 1 && rdlen == 4) {
                *ip = get32(reply + offset);
                return 0;
            }
            offset += rdlen;
        }
    }
    return AX_ENOENT;
}

/* ------------------------------------------------------------------ */
/* TCP                                                                 */
/* ------------------------------------------------------------------ */
static int tcp_connect(uint32_t ip, uint16_t port)
{
    if (!g_configured || !g_present) {
        return AX_EAGAIN;
    }
    g_tcp_state = 1;
    g_tcp_rip = ip;
    g_tcp_rport = port;
    g_tcp_lport = g_tcp_ephemeral++;
    if (g_tcp_lport < 41000) {
        g_tcp_ephemeral = 41000;
        g_tcp_lport = 41000;
    }
    g_tcp_snd_nxt = (uint32_t)(ax_uptime_ms() * 2654435761u) ^ 0x9E3779B9u;
    g_tcp_snd_una = g_tcp_snd_nxt;
    g_tcp_rcv_nxt = 0;
    g_tcp_rx_len = 0;
    g_tcp_fin = false;

    uint32_t isn = g_tcp_snd_nxt;
    for (int attempt = 0; attempt < 3; ++attempt) {
        g_tcp_state = 1;
        g_tcp_snd_nxt = isn + 1;
        g_tcp_snd_una = isn;
        tcp_xmit(isn, TCP_SYN, nullptr, 0);
        uint64_t start = ax_uptime_ms();
        while (ax_uptime_ms() - start < 1200) {
            net_poll();
            if (g_tcp_state == 2) {
                return 0;
            }
            e1000_delay_ms(3);
        }
    }
    g_tcp_state = 0;
    return AX_EAGAIN;
}

static int tcp_send_data(const void* data, uint32_t len)
{
    const uint8_t* p = (const uint8_t*)data;
    while (len) {
        uint32_t chunk = len > 1400 ? 1400 : len;
        uint32_t seq = g_tcp_snd_nxt;
        uint32_t target = seq + chunk;
        bool acked = false;

        for (int attempt = 0; attempt < 3 && !acked; ++attempt) {
            int rc = tcp_xmit(seq, TCP_PSH | TCP_ACK, p, chunk);
            if (rc != 0) {
                return rc;
            }
            uint64_t start = ax_uptime_ms();
            while (ax_uptime_ms() - start < 1500) {
                net_poll();
                if (g_tcp_snd_una >= target) {
                    acked = true;
                    break;
                }
                e1000_delay_ms(2);
            }
        }
        if (!acked) {
            return AX_EAGAIN;
        }
        g_tcp_snd_nxt = target;
        p += chunk;
        len -= chunk;
    }
    return 0;
}

static void tcp_close(void)
{
    if (g_tcp_state == 2) {
        tcp_xmit(g_tcp_snd_nxt, TCP_FIN | TCP_ACK, nullptr, 0);
        g_tcp_snd_nxt++;
        g_tcp_state = 3;
        uint64_t start = ax_uptime_ms();
        while (ax_uptime_ms() - start < 600) {
            net_poll();
            if (g_tcp_state == 0) {
                break;
            }
            e1000_delay_ms(2);
        }
    }
    g_tcp_state = 0;
}

int ax_net_http_get(const char* host, uint16_t port, const char* path,
                    char* out, uint64_t max, uint64_t* got)
{
    if (got) *got = 0;
    if (!host || !path || !out || !g_configured) {
        return AX_EAGAIN;
    }
    uint32_t ip = 0;
    int rc = ax_net_resolve(host, &ip);
    if (rc != 0) {
        return rc;
    }
    rc = tcp_connect(ip, port);
    if (rc != 0) {
        return rc;
    }

    char request[512];
    uint32_t r = 0;
    const char* prefix = "GET ";
    while (*prefix && r + 1 < sizeof(request)) request[r++] = *prefix++;
    while (*path && r + 1 < sizeof(request)) request[r++] = *path++;
    const char* suffix = " HTTP/1.0\r\nHost: ";
    while (*suffix && r + 1 < sizeof(request)) request[r++] = *suffix++;
    while (*host && r + 1 < sizeof(request)) request[r++] = *host++;
    const char* tail = "\r\nUser-Agent: AxolynOS/0.2\r\nConnection: close\r\n\r\n";
    while (*tail && r + 1 < sizeof(request)) request[r++] = *tail++;

    rc = tcp_send_data(request, r);
    if (rc != 0) {
        tcp_close();
        return rc;
    }

    uint64_t start = ax_uptime_ms();
    while (ax_uptime_ms() - start < 8000) {
        net_poll();
        if (g_tcp_fin) {
            break;
        }
        e1000_delay_ms(3);
    }

    int status = 0;
    uint32_t body = 0;
    if (g_tcp_rx_len >= 12 && !net_strneq((const char*)g_tcp_rx, "HTTP/", 5)) {
        uint32_t i = 9;
        while (i < g_tcp_rx_len && (g_tcp_rx[i] < '0' || g_tcp_rx[i] > '9')) i++;
        if (i + 2 < g_tcp_rx_len) {
            status = (g_tcp_rx[i] - '0') * 100 +
                     (g_tcp_rx[i + 1] - '0') * 10 +
                     (g_tcp_rx[i + 2] - '0');
        }
        for (uint32_t j = 0; j + 3 < g_tcp_rx_len; ++j) {
            if (g_tcp_rx[j] == '\r' && g_tcp_rx[j + 1] == '\n' &&
                g_tcp_rx[j + 2] == '\r' && g_tcp_rx[j + 3] == '\n') {
                body = j + 4;
                break;
            }
        }
        if (body == 0) {
            for (uint32_t j = 0; j + 1 < g_tcp_rx_len; ++j) {
                if (g_tcp_rx[j] == '\n' && g_tcp_rx[j + 1] == '\n') {
                    body = j + 2;
                    break;
                }
            }
        }
    }

    uint32_t body_len = g_tcp_rx_len > body ? g_tcp_rx_len - body : 0;
    uint32_t copy = body_len;
    if (copy > max) {
        copy = (uint32_t)max;
    }
    if (copy) {
        net_memcpy(out, g_tcp_rx + body, copy);
    }
    if (got) *got = copy;

    tcp_close();
    if (status >= 200 && status < 300) {
        return 0;
    }
    return AX_EIO;
}

/* ------------------------------------------------------------------ */
/* Formato y API publica                                               */
/* ------------------------------------------------------------------ */
static void append_text(char* out, uint64_t max, uint64_t* pos, const char* text)
{
    while (*text && *pos + 1 < max) {
        out[(*pos)++] = *text++;
    }
    out[*pos] = '\0';
}

static void append_u32(char* out, uint64_t max, uint64_t* pos, uint32_t value)
{
    char buf[12];
    int n = 0;
    if (value == 0) {
        buf[n++] = '0';
    } else {
        while (value && n < (int)sizeof(buf)) {
            buf[n++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }
    while (n > 0 && *pos + 1 < max) {
        out[(*pos)++] = buf[--n];
    }
    out[*pos] = '\0';
}

void ax_net_format_ip(uint32_t ip, char* out, uint64_t max)
{
    uint64_t pos = 0;
    if (!out || max == 0) {
        return;
    }
    out[0] = '\0';
    append_u32(out, max, &pos, (ip >> 24) & 0xFF);
    append_text(out, max, &pos, ".");
    append_u32(out, max, &pos, (ip >> 16) & 0xFF);
    append_text(out, max, &pos, ".");
    append_u32(out, max, &pos, (ip >> 8) & 0xFF);
    append_text(out, max, &pos, ".");
    append_u32(out, max, &pos, ip & 0xFF);
}

void ax_net_format_mac(const uint8_t mac[6], char* out, uint64_t max)
{
    static const char hex[] = "0123456789ABCDEF";
    uint64_t pos = 0;
    if (!out || max < 18) {
        return;
    }
    for (int i = 0; i < 6; ++i) {
        out[pos++] = hex[(mac[i] >> 4) & 0xF];
        out[pos++] = hex[mac[i] & 0xF];
        if (i != 5) {
            out[pos++] = ':';
        }
    }
    out[pos] = '\0';
}

bool ax_net_present(void)
{
    return g_present;
}

bool ax_net_configured(void)
{
    return g_configured;
}

void ax_net_info(AxNetInfo* out)
{
    if (!out) {
        return;
    }
    *out = g_info;
    out->present = g_present;
    out->link = g_present && (e1000_read(E1000_STATUS) & E1000_STATUS_LU) != 0;
    out->configured = g_configured;
    out->dhcp = g_dhcp_used;
    net_memcpy(out->mac, g_mac, 6);
    out->ip = g_ip;
    out->mask = g_mask;
    out->gateway = g_gateway;
    out->dns = g_dns;
}

void ax_net_init(void)
{
    net_memset(&g_info, 0, sizeof(g_info));
    net_memset(g_arp, 0, sizeof(g_arp));

    int pci_count = ax_pci_count();
    AxPciDevice* nic = nullptr;
    for (int i = 0; i < pci_count; ++i) {
        AxPciDevice* d = ax_pci_get(i);
        if (!d || d->vendor != 0x8086) {
            continue;
        }
        if (d->device == 0x100E || d->device == 0x100F ||
            d->device == 0x10D3 || d->device == 0x153A ||
            d->device == 0x15A3 || d->device == 0x15B8) {
            nic = d;
            break;
        }
    }
    if (!nic) {
        ax_puts("[net] no se encontro una tarjeta e1000\n");
        return;
    }

    /* Habilitar memoria y bus master en la configuracion PCI. */
    uint16_t cmd = ax_pci_read16(nic->bus, nic->dev, nic->func, 0x04);
    ax_pci_write16(nic->bus, nic->dev, nic->func, 0x04, (uint16_t)(cmd | 0x0006));

    uint32_t bar = nic->bar[0] & ~0x0Fu;
    if (!bar || (nic->bar[0] & 0x01)) {
        ax_puts("[net] BAR0 del e1000 no es MMIO\n");
        return;
    }

    const uint64_t mmio_virt = 0xFFFFFFFF90000000ull;
    for (uint64_t offset = 0; offset < 0x20000; offset += AX_PAGE_SIZE) {
        if (!ax_vmm_map(mmio_virt + offset, (uint64_t)bar + offset,
                        AX_PAGE_WRITE | AX_PAGE_CACHE_DISABLE)) {
            ax_puts("[net] no se pudo mapear la MMIO del e1000\n");
            return;
        }
    }
    g_mmio = (volatile uint8_t*)(uintptr_t)mmio_virt;

    /* Reset del controlador. */
    e1000_write(E1000_IMC, 0xFFFFFFFFu);
    e1000_write(E1000_CTRL, e1000_read(E1000_CTRL) | E1000_CTRL_RST);
    e1000_delay_ms(10);
    for (int i = 0; i < 200; ++i) {
        if (!(e1000_read(E1000_CTRL) & E1000_CTRL_RST)) {
            break;
        }
        e1000_delay_ms(1);
    }
    e1000_write(E1000_IMC, 0xFFFFFFFFu);

    /* MAC. */
    uint32_t ral = e1000_read(E1000_RAL0);
    uint32_t rah = e1000_read(E1000_RAH0);
    g_mac[0] = (uint8_t)ral;
    g_mac[1] = (uint8_t)(ral >> 8);
    g_mac[2] = (uint8_t)(ral >> 16);
    g_mac[3] = (uint8_t)(ral >> 24);
    g_mac[4] = (uint8_t)rah;
    g_mac[5] = (uint8_t)(rah >> 8);
    if ((g_mac[0] == 0 && g_mac[1] == 0 && g_mac[2] == 0 &&
         g_mac[3] == 0 && g_mac[4] == 0 && g_mac[5] == 0) ||
        (g_mac[0] == 0xFF && g_mac[1] == 0xFF && g_mac[2] == 0xFF &&
         g_mac[3] == 0xFF && g_mac[4] == 0xFF && g_mac[5] == 0xFF)) {
        static const uint8_t fallback[6] = {0x02, 0x41, 0x58, 0x4C, 0x59, 0x4E};
        net_memcpy(g_mac, fallback, 6);
    }

    /* El filtro de recepcion debe conocer la MAC aunque el EEPROM no la
       haya cargado tras el reset.  RAH.AV (bit 31) activa la entrada. */
    ral = (uint32_t)g_mac[0] | ((uint32_t)g_mac[1] << 8) |
          ((uint32_t)g_mac[2] << 16) | ((uint32_t)g_mac[3] << 24);
    rah = (uint32_t)g_mac[4] | ((uint32_t)g_mac[5] << 8) | 0x80000000u;
    e1000_write(E1000_RAL0, ral);
    e1000_write(E1000_RAH0, rah);

    g_rx_desc = (E1000RxDesc*)ax_calloc(RX_COUNT, sizeof(E1000RxDesc));
    g_tx_desc = (E1000TxDesc*)ax_calloc(TX_COUNT, sizeof(E1000TxDesc));
    g_rx_bufs = (uint8_t*)ax_malloc(RX_COUNT * RX_BUF_SIZE);
    if (!g_rx_desc || !g_tx_desc || !g_rx_bufs) {
        ax_puts("[net] sin memoria para los anillos del e1000\n");
        return;
    }
    net_memset(g_rx_bufs, 0, RX_COUNT * RX_BUF_SIZE);
    for (int i = 0; i < RX_COUNT; ++i) {
        g_rx_desc[i].addr = ax_vmm_translate((uint64_t)(uintptr_t)(g_rx_bufs + i * RX_BUF_SIZE));
        g_rx_desc[i].status = 0;
    }

    e1000_write(E1000_RDBAL, (uint32_t)ax_vmm_translate((uint64_t)(uintptr_t)g_rx_desc));
    e1000_write(E1000_RDBAH, (uint32_t)(ax_vmm_translate((uint64_t)(uintptr_t)g_rx_desc) >> 32));
    e1000_write(E1000_RDLEN, RX_COUNT * sizeof(E1000RxDesc));
    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, RX_COUNT - 1);

    e1000_write(E1000_TDBAL, (uint32_t)ax_vmm_translate((uint64_t)(uintptr_t)g_tx_desc));
    e1000_write(E1000_TDBAH, (uint32_t)(ax_vmm_translate((uint64_t)(uintptr_t)g_tx_desc) >> 32));
    e1000_write(E1000_TDLEN, TX_COUNT * sizeof(E1000TxDesc));
    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);

    e1000_write(E1000_CTRL, e1000_read(E1000_CTRL) | E1000_CTRL_SLU);
    e1000_write(E1000_RCTL,
                E1000_RCTL_EN | E1000_RCTL_SBP | E1000_RCTL_BAM |
                E1000_RCTL_SECRC);
    e1000_write(E1000_TCTL,
                E1000_TCTL_EN | E1000_TCTL_PSP |
                (0x10u << 4) | (0x40u << 12));

    g_present = true;
    for (int i = 0; i < 300; ++i) {
        if (e1000_read(E1000_STATUS) & E1000_STATUS_LU) {
            break;
        }
        e1000_delay_ms(10);
    }

    char mac[20];
    ax_net_format_mac(g_mac, mac, sizeof(mac));
    ax_printf("[net] e1000 %s enlazado=%s\n", mac,
              (e1000_read(E1000_STATUS) & E1000_STATUS_LU) ? "si" : "no");
}
