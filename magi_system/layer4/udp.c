#include "udp.h"
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

static size_t udp_packet_to_bytes(const Packet* self, uint8_t* out, size_t out_size)
{
    return udp_to_bytes((const UDPDatagram*)self, out, out_size);
}

static int udp_packet_from_bytes(Packet* self, const uint8_t* raw, size_t raw_len)
{
    return udp_from_bytes((UDPDatagram*)self, raw, raw_len);
}

static const PacketVTable UDP_VTABLE = {
    .to_bytes = udp_packet_to_bytes,
    .from_bytes = udp_packet_from_bytes
};

void udp_init(UDPDatagram* datagram)
{
    if (datagram == NULL) {
        return;
    }

    memset(datagram, 0, sizeof(*datagram));
    datagram->base.vtable = &UDP_VTABLE;
    datagram->base.type = UDP;
    datagram->length = UDP_HEADER_SIZE;
}

int udp_create(
    UDPDatagram* datagram,
    uint16_t src_port,
    uint16_t dst_port,
    const uint8_t* payload,
    size_t payload_len
)
{
    if (datagram == NULL || payload_len > UDP_MAX_PAYLOAD) {
        return 0;
    }

    if (payload_len > 0 && payload == NULL) {
        return 0;
    }

    udp_init(datagram);
    datagram->src_port = src_port;
    datagram->dst_port = dst_port;
    datagram->payload_len = payload_len;
    datagram->length = (uint16_t)(UDP_HEADER_SIZE + payload_len);
    if (payload_len > 0) {
        memcpy(datagram->payload, payload, payload_len);
    }

    return 1;
}

uint16_t udp_compute_checksum(const uint8_t* raw, size_t raw_len,
                               const IpAddress* src_ip, const IpAddress* dst_ip)
{
    // Pseudo-header + UDP datagram checksum (RFC 768)
    // Pseudo-header: src_ip(4) + dst_ip(4) + zero(1) + protocol(1) + UDP length(2)
    uint32_t sum = 0;
    uint16_t protocol = IPV4_PROTOCOL_UDP;
    size_t i;

    if (raw == NULL || src_ip == NULL || dst_ip == NULL) {
        return 0;
    }

    // Pseudo-header: src IP
    for (i = 0; i < 4; i += 2) {
        uint16_t word = (uint16_t)src_ip->octet[i] << 8;
        if (i + 1 < 4) word |= src_ip->octet[i + 1];
        sum += word;
        while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    // Pseudo-header: dst IP
    for (i = 0; i < 4; i += 2) {
        uint16_t word = (uint16_t)dst_ip->octet[i] << 8;
        if (i + 1 < 4) word |= dst_ip->octet[i + 1];
        sum += word;
        while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    // Pseudo-header: zero (1 byte) + protocol (1 byte)
    sum += (uint16_t)protocol;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);

    // Pseudo-header: UDP length
    uint16_t udp_len = (uint16_t)raw_len;
    sum += udp_len;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);

    // UDP datagram
    for (i = 0; i < raw_len; i += 2) {
        uint16_t word = (uint16_t)raw[i] << 8;
        if (i + 1 < raw_len) word |= raw[i + 1];
        sum += word;
        while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    return (uint16_t)(~sum & 0xFFFFu);
}

bool udp_validate_checksum(const UDPDatagram* datagram,
                           const IpAddress* src_ip, const IpAddress* dst_ip)
{
    uint8_t raw[UDP_HEADER_SIZE + UDP_MAX_PAYLOAD];
    size_t len;

    if (datagram == NULL || src_ip == NULL || dst_ip == NULL) {
        return false;
    }

    len = udp_to_bytes(datagram, raw, sizeof(raw));
    if (len < UDP_HEADER_SIZE) {
        return false;
    }

    // Restore original checksum before computing
    write_u16_be(raw + 6, datagram->checksum);
    return udp_compute_checksum(raw, len, src_ip, dst_ip) == 0;
}

size_t udp_to_bytes(const UDPDatagram* datagram, uint8_t* out, size_t out_size)
{
    size_t total_len;

    if (datagram == NULL || out == NULL || datagram->payload_len > UDP_MAX_PAYLOAD) {
        return 0;
    }

    total_len = UDP_HEADER_SIZE + datagram->payload_len;
    if (out_size < total_len) {
        return 0;
    }

    write_u16_be(out + 0, datagram->src_port);
    write_u16_be(out + 2, datagram->dst_port);
    write_u16_be(out + 4, datagram->length);
    write_u16_be(out + 6, datagram->checksum);

    if (datagram->payload_len > 0) {
        memcpy(out + UDP_HEADER_SIZE, datagram->payload, datagram->payload_len);
    }

    return total_len;
}

int udp_from_bytes(UDPDatagram* datagram, const uint8_t* raw, size_t raw_len)
{
    size_t payload_len;

    if (datagram == NULL || raw == NULL || raw_len < UDP_HEADER_SIZE) {
        return 0;
    }

    udp_init(datagram);
    datagram->src_port = read_u16_be(raw + 0);
    datagram->dst_port = read_u16_be(raw + 2);
    datagram->length = read_u16_be(raw + 4);
    datagram->checksum = read_u16_be(raw + 6);

    payload_len = (size_t)datagram->length - UDP_HEADER_SIZE;
    if (payload_len > UDP_MAX_PAYLOAD) {
        return 0;
    }

    datagram->payload_len = payload_len;
    if (payload_len > 0) {
        memcpy(datagram->payload, raw + UDP_HEADER_SIZE, payload_len);
    }

    return 1;
}
