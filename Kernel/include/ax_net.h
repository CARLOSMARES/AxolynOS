/*
 * AxolynOS - Pila de red (e1000 + ARP/IPv4/ICMP/UDP/DHCP/DNS/TCP).
 */
#ifndef AX_NET_H
#define AX_NET_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     present;
    bool     link;
    bool     configured;
    bool     dhcp;
    uint8_t  mac[6];
    uint32_t ip;
    uint32_t mask;
    uint32_t gateway;
    uint32_t dns;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t arp_requests;
    uint64_t arp_replies;
    uint64_t ip_in;
    uint64_t ip_out;
    uint64_t icmp_in;
    uint64_t icmp_out;
    uint64_t udp_in;
    uint64_t udp_out;
    uint64_t tcp_in;
    uint64_t tcp_out;
} AxNetInfo;

void     ax_net_init(void);
bool     ax_net_present(void);
bool     ax_net_configured(void);
void     ax_net_info(AxNetInfo* out);

int      ax_net_dhcp(void);
int      ax_net_ping(uint32_t ip, uint32_t timeout_ms, uint32_t* rtt_ms);
int      ax_net_resolve(const char* name, uint32_t* ip);
int      ax_net_http_get(const char* host, uint16_t port, const char* path,
                         char* out, uint64_t max, uint64_t* got);

uint32_t ax_net_parse_ip(const char* text);
void     ax_net_format_ip(uint32_t ip, char* out, uint64_t max);
void     ax_net_format_mac(const uint8_t mac[6], char* out, uint64_t max);

#ifdef __cplusplus
}
#endif

#endif /* AX_NET_H */
