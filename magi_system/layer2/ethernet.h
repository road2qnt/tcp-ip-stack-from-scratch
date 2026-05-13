// Ethernet: Turunan dari Packet
#ifndef ETHERNET_H
#define ETHERNET_H

#include "../core/packet.h"
#include "../core/mac.h"

#define ETHERNET_MAX_PAYLOAD 1500
#define ETHERNET_HEADER_SIZE 14
#define ETHERNET_VLAN_HEADER_SIZE 18

#define ETHERNET_TYPE_IPV4 0x0800
#define ETHERNET_TYPE_ARP 0x0806
#define ETHERNET_TYPE_VLAN 0x8100

#define ETHERNET_VLAN_ID_MASK 0x0FFF

typedef struct EthernetFrame {
    Packet base;
    MacAddress dst_mac;
    MacAddress src_mac;

    bool has_vlan;
    uint16_t vlan_id;

    uint16_t ethertype;

    uint8_t payload[ETHERNET_MAX_PAYLOAD];
    size_t payload_len;
} EthernetFrame;

void ethernet_init(EthernetFrame *frame);
int ethernet_create(EthernetFrame *frame,MacAddress dst_mac,MacAddress src_mac,uint16_t ethertype,const uint8_t *payload,size_t payload_len);
int ethernet_set_vlan(EthernetFrame *frame, uint16_t vlan_id);
void ethernet_clear_vlan(EthernetFrame *frame);
bool ethernet_is_broadcast(const MacAddress *mac);

#endif
