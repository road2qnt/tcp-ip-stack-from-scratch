#ifndef HOST_H
#define HOST_H

#include "../core/interface.h"
#include "../layer3/ipv4.h"
#include "../layer4/udp.h"
#include "../layer4/tcp_socket.h"
#include "arp.h"
#include "ethernet.h"

#include <time.h>

#define HOST_PENDING_QUEUE_MAX_ENTRIES 64

typedef struct HostPendingPacket {
    IpAddress target_ip;
    uint16_t ethertype;
    uint8_t payload[ETHERNET_MAX_PAYLOAD];
    size_t payload_len;
} HostPendingPacket;

typedef struct HostPendingQueue {
    HostPendingPacket items[HOST_PENDING_QUEUE_MAX_ENTRIES];    
    size_t head;
    size_t tail;
    size_t size;
} HostPendingQueue;

// Definisi Host (inheritance dari node)
typedef struct Host{
    Node base;
    IpAddress ip_address;
    IpAddress default_gateway;
    bool has_ip;
    uint8_t last_icmp_type;
    IpAddress last_icmp_source;
    uint16_t last_icmp_sequence;
    bool has_last_icmp;
    double icmp_send_time_ms;
    bool icmp_in_flight;
    double last_icmp_rtt_ms;
    // Tambahan untuk Host (ARP Table: IP -> MAC)
    ArpTable arp_table;
    HostPendingQueue pending_queue;
    
    // Transport Layer (Milestone 3)
    TCPSocket tcp_sockets[TCP_MAX_SOCKETS];
    UDPDatagram last_udp;
    IpAddress last_udp_src_ip;
    bool has_last_udp;
    TCPSegment last_tcp;
    bool has_last_tcp;
    bool tcp_data_ready;
    uint16_t next_tcp_source_port;

    // Application Layer (Milestone 4) - MagiSocket fd
    int magi_sock_fd;
}Host;

void host_init(Host* host, int num_interfaces);
void host_init_with_macs(Host* host, int num_interfaces, const MacAddress* mac_addresses);

void host_pending_queue_init(HostPendingQueue* queue);
int host_queue_pending_packet(Host* host, const IpAddress* target_ip, uint16_t ethertype, const uint8_t* payload, size_t payload_len);
int host_dequeue_pending_packet_for_ip(Host* host, const IpAddress* target_ip, HostPendingPacket* out_packet);
size_t host_pending_count_for_ip(const Host* host, const IpAddress* target_ip);

int host_learn_arp(Host* host, const ARPMessage* message);
int host_send_l3_packet(Host* host, const IpAddress* target_ip, uint16_t ethertype, const uint8_t* payload, size_t payload_len);
int host_flush_pending_packets(Host* host, const IpAddress* resolved_ip);
int host_send_icmp_echo_request(Host* host, const IpAddress* target_ip, uint8_t ttl, uint16_t sequence);
uint16_t host_select_tcp_source_port(Host* host, const IpAddress* remote_ip,
                                     uint16_t remote_port, uint16_t initial_preferred);

#endif
