#ifndef ROUTER_H
#define ROUTER_H

#include "../core/interface.h"
#include "../layer2/arp.h"
#include "ipv4.h"

#define ROUTER_MAX_ROUTES 128
#define ROUTER_PENDING_QUEUE_MAX_ENTRIES 128

typedef struct RouterInterfaceAddress {
    int portNumber;
    IpAddress ip_address;
    bool has_ip;
} RouterInterfaceAddress;

typedef struct RoutingTableEntry {
    IpAddress destination;
    IpAddress next_hop;
    int out_interface;
} RoutingTableEntry;

typedef struct RouterPendingPacket {
    IpAddress next_hop_ip;
    int out_interface;
    int vlan_id;
    IPv4Packet packet;
} RouterPendingPacket;

typedef struct RouterPendingQueue {
    RouterPendingPacket items[ROUTER_PENDING_QUEUE_MAX_ENTRIES];
    size_t head;
    size_t tail;
    size_t size;
} RouterPendingQueue;

typedef struct Router{
    Node base;
    RouterInterfaceAddress interface_ips[MAX_PORT];
    RoutingTableEntry routing_table[ROUTER_MAX_ROUTES];
    size_t route_count;
    ArpTable arp_table;
    int arp_entry_vlans[ARP_TABLE_MAX_ENTRIES];
    int interface_vlans[MAX_PORT];
    RouterPendingQueue pending_queue;
}Router;

void router_init(Router* router, int num_interfaces);
void router_init_with_macs(Router* router, int num_interfaces, const MacAddress* mac_addresses);
void router_pending_queue_init(RouterPendingQueue* queue);
int router_add_route(Router* router, IpAddress destination, IpAddress next_hop, int out_interface);
int router_add_direct_routes(Router* router);
const RoutingTableEntry* router_lookup_route(const Router* router, const IpAddress* dst_ip);
int router_learn_arp(Router* router, const ARPMessage* message, int vlan_id);
int router_send_ipv4_packet(Router* router, IPv4Packet* packet);
int router_send_icmp_echo_request(Router* router, const IpAddress* target_ip, uint8_t ttl, uint16_t sequence);
void router_print_routes(const Router* router);

#endif
