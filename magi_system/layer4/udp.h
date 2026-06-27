#ifndef UDP_H
#define UDP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "../core/packet.h"
#include "../layer3/ipv4.h"

#define UDP_HEADER_SIZE 8
#define UDP_MAX_PAYLOAD 1472

typedef struct UDPDatagram {
    Packet base;
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
    uint8_t payload[UDP_MAX_PAYLOAD];
    size_t payload_len;
} UDPDatagram;

void udp_init(UDPDatagram* datagram);
int udp_create(
    UDPDatagram* datagram,
    uint16_t src_port,
    uint16_t dst_port,
    const uint8_t* payload,
    size_t payload_len
);
uint16_t udp_compute_checksum(const uint8_t* raw, size_t raw_len,
                               const IpAddress* src_ip, const IpAddress* dst_ip);
bool udp_validate_checksum(const UDPDatagram* datagram,
                           const IpAddress* src_ip, const IpAddress* dst_ip);
size_t udp_to_bytes(const UDPDatagram* datagram, uint8_t* out, size_t out_size);
int udp_from_bytes(UDPDatagram* datagram, const uint8_t* raw, size_t raw_len);

#endif
