#include "ipv4.h"

#include <stdio.h>
#include <string.h>

static uint16_t read_u16_be(const uint8_t* raw)
{
    return (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
}

static void write_u16_be(uint8_t* out, uint16_t value)
{
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)(value & 0xFF);
}

static size_t ipv4_packet_to_bytes(const Packet* self, uint8_t* out, size_t out_size)
{
    return ipv4_to_bytes((const IPv4Packet*)self, out, out_size);
}

static int ipv4_packet_from_bytes(Packet* self, const uint8_t* raw, size_t raw_len)
{
    return ipv4_from_bytes((IPv4Packet*)self, raw, raw_len);
}

static const PacketVTable IPV4_VTABLE = {
    .to_bytes = ipv4_packet_to_bytes,
    .from_bytes = ipv4_packet_from_bytes
};

IpAddress ip_init(uint8_t* octet_, uint8_t prefix_){
    IpAddress IP;
    for (int i=0;i<4;i++){
        IP.octet[i] = octet_[i];
    }
    IP.prefix = prefix_;
    return IP;
}

bool ip_equal(IpAddress* ip_1, IpAddress* ip_2){
    for (int i=0;i<4;i++){
        if (ip_1->octet[i]!=ip_2->octet[i]){
            return false;
        }
    }

    if (ip_1->prefix!=ip_2->prefix) return false;

    return true;
}

bool ip_octets_equal_public(const IpAddress* ip_1, const IpAddress* ip_2)
{
    if (ip_1 == NULL || ip_2 == NULL) {
        return false;
    }

    return memcmp(ip_1->octet, ip_2->octet, 4) == 0;
}

uint32_t ip_to_u32_public(const IpAddress* ip)
{
    if (ip == NULL) {
        return 0;
    }

    return ((uint32_t)ip->octet[0] << 24) |
           ((uint32_t)ip->octet[1] << 16) |
           ((uint32_t)ip->octet[2] << 8) |
           (uint32_t)ip->octet[3];
}

uint32_t ip_prefix_to_mask(uint8_t prefix)
{
    if (prefix == 0) {
        return 0;
    }

    if (prefix >= 32) {
        return 0xFFFFFFFFu;
    }

    return 0xFFFFFFFFu << (32 - prefix);
}

IpAddress ip_network_address(IpAddress ip)
{
    uint32_t value = ip_to_u32_public(&ip);
    uint32_t mask = ip_prefix_to_mask(ip.prefix);
    uint32_t network = value & mask;

    ip.octet[0] = (uint8_t)(network >> 24);
    ip.octet[1] = (uint8_t)(network >> 16);
    ip.octet[2] = (uint8_t)(network >> 8);
    ip.octet[3] = (uint8_t)(network & 0xFF);
    return ip;
}

int ip_parse(const char* str, IpAddress* out_ip)
{
    unsigned int octets[4];
    int prefix = 0;
    int count;

    if (str == NULL || out_ip == NULL) {
        return 0;
    }

    count = sscanf(str, "%u.%u.%u.%u/%d",
                   &octets[0], &octets[1], &octets[2], &octets[3], &prefix);
    if (count != 5) {
        count = sscanf(str, "%u.%u.%u.%u",
                       &octets[0], &octets[1], &octets[2], &octets[3]);
        if (count != 4) {
            return 0;
        }
        prefix = 0;
    }

    if (prefix < 0 || prefix > 32) {
        return 0;
    }

    for (int i = 0; i < 4; i++) {
        if (octets[i] > 255) {
            return 0;
        }
        out_ip->octet[i] = (uint8_t)octets[i];
    }
    out_ip->prefix = (uint8_t)prefix;
    return 1;
}

void ip_to_string(const IpAddress* ip, char* out, size_t out_size, bool include_prefix)
{
    if (out == NULL || out_size == 0) {
        return;
    }

    if (ip == NULL) {
        snprintf(out, out_size, "?.?.?.?");
        return;
    }

    if (include_prefix) {
        snprintf(out, out_size, "%u.%u.%u.%u/%u",
                 ip->octet[0], ip->octet[1], ip->octet[2], ip->octet[3], ip->prefix);
    } else {
        snprintf(out, out_size, "%u.%u.%u.%u",
                 ip->octet[0], ip->octet[1], ip->octet[2], ip->octet[3]);
    }
}

void ipv4_init(IPv4Packet* packet)
{
    if (packet == NULL) {
        return;
    }

    memset(packet, 0, sizeof(*packet));
    packet->base.vtable = &IPV4_VTABLE;
    packet->base.type = IPV4;
    packet->version = 4;
    packet->ihl = 5;
    packet->ttl = IPV4_DEFAULT_TTL;
}

int ipv4_create(
    IPv4Packet* packet,
    IpAddress src_ip,
    IpAddress dst_ip,
    uint8_t ttl,
    uint8_t protocol,
    const uint8_t* payload,
    size_t payload_len
)
{
    if (packet == NULL || payload_len > IPV4_MAX_PAYLOAD) {
        return 0;
    }

    if (payload_len > 0 && payload == NULL) {
        return 0;
    }

    ipv4_init(packet);
    packet->src_ip = src_ip;
    packet->dst_ip = dst_ip;
    packet->ttl = ttl;
    packet->protocol = protocol;
    packet->payload_len = payload_len;
    packet->total_length = (uint16_t)(IPV4_HEADER_SIZE + payload_len);
    if (payload_len > 0) {
        memcpy(packet->payload, payload, payload_len);
    }

    return 1;
}

uint16_t ipv4_compute_checksum(const uint8_t* header, size_t header_len)
{
    uint32_t sum = 0;

    if (header == NULL || header_len == 0) {
        return 0;
    }

    for (size_t i = 0; i < header_len; i += 2) {
        uint16_t word = (uint16_t)header[i] << 8;
        if (i + 1 < header_len) {
            word |= header[i + 1];
        }
        sum += word;
        while (sum >> 16) {
            sum = (sum & 0xFFFFu) + (sum >> 16);
        }
    }

    return (uint16_t)(~sum & 0xFFFFu);
}

size_t ipv4_to_bytes(const IPv4Packet* packet, uint8_t* out, size_t out_size)
{
    size_t offset = 0;
    uint16_t checksum;
    uint16_t total_length;

    if (packet == NULL || out == NULL || packet->payload_len > IPV4_MAX_PAYLOAD) {
        return 0;
    }

    total_length = (uint16_t)(IPV4_HEADER_SIZE + packet->payload_len);
    if (out_size < total_length) {
        return 0;
    }

    out[offset++] = (uint8_t)((4u << 4) | 5u);
    out[offset++] = 0;
    write_u16_be(out + offset, total_length);
    offset += 2;
    write_u16_be(out + offset, 0);
    offset += 2;
    write_u16_be(out + offset, 0);
    offset += 2;
    out[offset++] = packet->ttl;
    out[offset++] = packet->protocol;
    write_u16_be(out + offset, 0);
    offset += 2;
    memcpy(out + offset, packet->src_ip.octet, 4);
    offset += 4;
    memcpy(out + offset, packet->dst_ip.octet, 4);
    offset += 4;

    checksum = ipv4_compute_checksum(out, IPV4_HEADER_SIZE);
    write_u16_be(out + 10, checksum);

    if (packet->payload_len > 0) {
        memcpy(out + offset, packet->payload, packet->payload_len);
        offset += packet->payload_len;
    }

    return offset;
}

int ipv4_from_bytes(IPv4Packet* packet, const uint8_t* raw, size_t raw_len)
{
    size_t header_len;

    if (packet == NULL || raw == NULL || raw_len < IPV4_HEADER_SIZE) {
        return 0;
    }

    ipv4_init(packet);
    packet->version = raw[0] >> 4;
    packet->ihl = raw[0] & 0x0F;
    header_len = (size_t)packet->ihl * 4u;
    if (packet->version != 4 || packet->ihl < 5 || header_len > raw_len) {
        return 0;
    }

    packet->total_length = read_u16_be(raw + 2);
    if (packet->total_length < header_len || packet->total_length > raw_len) {
        return 0;
    }

    packet->ttl = raw[8];
    packet->protocol = raw[9];
    packet->checksum = read_u16_be(raw + 10);
    memcpy(packet->src_ip.octet, raw + 12, 4);
    packet->src_ip.prefix = 0;
    memcpy(packet->dst_ip.octet, raw + 16, 4);
    packet->dst_ip.prefix = 0;

    packet->payload_len = packet->total_length - header_len;
    if (packet->payload_len > IPV4_MAX_PAYLOAD) {
        return 0;
    }
    memcpy(packet->payload, raw + header_len, packet->payload_len);

    return 1;
}

bool ipv4_validate_checksum(const IPv4Packet* packet)
{
    uint8_t raw[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
    size_t len;

    if (packet == NULL) {
        return false;
    }

    len = ipv4_to_bytes(packet, raw, sizeof(raw));
    if (len < IPV4_HEADER_SIZE) {
        return false;
    }

    write_u16_be(raw + 10, packet->checksum);
    return ipv4_compute_checksum(raw, IPV4_HEADER_SIZE) == 0;
}
