#ifndef IPV4_H
#define IPV4_H

#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"

#include "../core/packet.h"

#define IPV4_HEADER_SIZE 20
#define IPV4_MAX_PAYLOAD 1480
#define IPV4_PROTOCOL_ICMP 1
#define IPV4_PROTOCOL_TCP 6
#define IPV4_PROTOCOL_UDP 17
#define IPV4_DEFAULT_TTL 64

// Class IP Address
typedef struct IpAddress{
    uint8_t octet[4];
    uint8_t prefix;
} IpAddress;

typedef struct IPv4Packet {
    Packet base;
    uint8_t version;
    uint8_t ihl;
    uint16_t total_length;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    IpAddress src_ip;
    IpAddress dst_ip;
    uint8_t payload[IPV4_MAX_PAYLOAD];
    size_t payload_len;
} IPv4Packet;

// Method - IP Address
IpAddress ip_init(uint8_t* octet_, uint8_t prefix_);
bool ip_equal(IpAddress* ip_1, IpAddress* ip_2);
bool ip_octets_equal_public(const IpAddress* ip_1, const IpAddress* ip_2);
uint32_t ip_to_u32_public(const IpAddress* ip);
uint32_t ip_prefix_to_mask(uint8_t prefix);
IpAddress ip_network_address(IpAddress ip);
int ip_parse(const char* str, IpAddress* out_ip);
void ip_to_string(const IpAddress* ip, char* out, size_t out_size, bool include_prefix);

void ipv4_init(IPv4Packet* packet);
int ipv4_create(
    IPv4Packet* packet,
    IpAddress src_ip,
    IpAddress dst_ip,
    uint8_t ttl,
    uint8_t protocol,
    const uint8_t* payload,
    size_t payload_len
);
uint16_t ipv4_compute_checksum(const uint8_t* header, size_t header_len);
bool ipv4_validate_checksum(const IPv4Packet* packet);
size_t ipv4_to_bytes(const IPv4Packet* packet, uint8_t* out, size_t out_size);
int ipv4_from_bytes(IPv4Packet* packet, const uint8_t* raw, size_t raw_len);

#endif
