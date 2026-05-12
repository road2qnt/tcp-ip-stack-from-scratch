// Ethernet: Turunan dari Packet
#ifndef ETHERNET_H
#define ETHERNET_H

#include "../core/packet.h"
#include "../core/mac.h"

#define ETHERNET_MAX_PAYLOAD 1500

typedef struct EthernetFrame {
    Packet base;

    MacAddress dst_mac;
    MacAddress src_mac;
    uint16_t ethertype;

    uint8_t payload[ETHERNET_MAX_PAYLOAD];
    size_t payload_len;
} EthernetFrame;

void ethernet_init(EthernetFrame *frame);

#endif
