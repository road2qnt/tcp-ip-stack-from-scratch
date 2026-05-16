#include "tcp.h"
#include <string.h>
#include <stdio.h>

static uint16_t read_u16_be(const uint8_t* raw)
{
    return (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
}

static uint32_t read_u32_be(const uint8_t* raw)
{
    return ((uint32_t)raw[0] << 24) |
           ((uint32_t)raw[1] << 16) |
           ((uint32_t)raw[2] << 8) |
           (uint32_t)raw[3];
}

static void write_u16_be(uint8_t* out, uint16_t value)
{
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)(value & 0xFF);
}

static void write_u32_be(uint8_t* out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)(value & 0xFF);
}

static size_t tcp_packet_to_bytes(const Packet* self, uint8_t* out, size_t out_size)
{
    return tcp_to_bytes((const TCPSegment*)self, out, out_size);
}

static int tcp_packet_from_bytes(Packet* self, const uint8_t* raw, size_t raw_len)
{
    return tcp_from_bytes((TCPSegment*)self, raw, raw_len);
}

static const PacketVTable TCP_VTABLE = {
    .to_bytes = tcp_packet_to_bytes,
    .from_bytes = tcp_packet_from_bytes
};

const char* tcp_state_name(TCPState state)
{
    switch (state) {
        case TCP_CLOSED:        return "CLOSED";
        case TCP_LISTEN:        return "LISTEN";
        case TCP_SYN_SENT:      return "SYN_SENT";
        case TCP_SYN_RECEIVED:  return "SYN_RECEIVED";
        case TCP_ESTABLISHED:   return "ESTABLISHED";
        case TCP_FIN_WAIT_1:    return "FIN_WAIT_1";
        case TCP_FIN_WAIT_2:    return "FIN_WAIT_2";
        case TCP_CLOSING:       return "CLOSING";
        case TCP_TIME_WAIT:     return "TIME_WAIT";
        case TCP_CLOSE_WAIT:    return "CLOSE_WAIT";
        case TCP_LAST_ACK:      return "LAST_ACK";
        default:                return "UNKNOWN";
    }
}

void tcp_init(TCPSegment* segment)
{
    if (segment == NULL) {
        return;
    }

    memset(segment, 0, sizeof(*segment));
    segment->base.vtable = &TCP_VTABLE;
    segment->base.type = TCP;
    segment->data_offset = 5;  // 5 * 4 = 20 bytes header
    segment->window = TCP_WINDOW_SIZE;
}

int tcp_create(
    TCPSegment* segment,
    uint16_t src_port,
    uint16_t dst_port,
    uint32_t seq_num,
    uint32_t ack_num,
    uint8_t flags,
    uint16_t window,
    const uint8_t* payload,
    size_t payload_len
)
{
    if (segment == NULL || payload_len > TCP_MAX_PAYLOAD) {
        return 0;
    }

    if (payload_len > 0 && payload == NULL) {
        return 0;
    }

    tcp_init(segment);
    segment->src_port = src_port;
    segment->dst_port = dst_port;
    segment->seq_num = seq_num;
    segment->ack_num = ack_num;
    segment->flags = flags;
    segment->window = window;
    segment->payload_len = payload_len;
    if (payload_len > 0) {
        memcpy(segment->payload, payload, payload_len);
    }

    return 1;
}

uint16_t tcp_compute_checksum(const uint8_t* raw, size_t raw_len,
                               const IpAddress* src_ip, const IpAddress* dst_ip)
{
    // Pseudo-header + TCP segment checksum (RFC 793)
    uint32_t sum = 0;
    uint16_t protocol = IPV4_PROTOCOL_TCP;
    size_t i;

    if (raw == NULL || src_ip == NULL || dst_ip == NULL) {
        return 0;
    }

    // Pseudo-header: src IP (4 bytes)
    for (i = 0; i < 4; i += 2) {
        uint16_t word = (uint16_t)src_ip->octet[i] << 8;
        if (i + 1 < 4) word |= src_ip->octet[i + 1];
        sum += word;
        while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    // Pseudo-header: dst IP (4 bytes)
    for (i = 0; i < 4; i += 2) {
        uint16_t word = (uint16_t)dst_ip->octet[i] << 8;
        if (i + 1 < 4) word |= dst_ip->octet[i + 1];
        sum += word;
        while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    // Pseudo-header: zero (1 byte) + protocol (1 byte)
    sum += (uint16_t)protocol;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);

    // Pseudo-header: TCP length
    uint16_t tcp_len = (uint16_t)raw_len;
    sum += tcp_len;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);

    // TCP segment data
    for (i = 0; i < raw_len; i += 2) {
        uint16_t word = (uint16_t)raw[i] << 8;
        if (i + 1 < raw_len) word |= raw[i + 1];
        sum += word;
        while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    return (uint16_t)(~sum & 0xFFFFu);
}

bool tcp_validate_checksum(const TCPSegment* segment,
                           const IpAddress* src_ip, const IpAddress* dst_ip)
{
    uint8_t raw[TCP_HEADER_SIZE + TCP_MAX_PAYLOAD];
    size_t len;

    if (segment == NULL || src_ip == NULL || dst_ip == NULL) {
        return false;
    }

    len = tcp_to_bytes(segment, raw, sizeof(raw));
    if (len < TCP_HEADER_SIZE) {
        return false;
    }

    // Restore original checksum before computing
    write_u16_be(raw + 16, segment->checksum);
    return tcp_compute_checksum(raw, len, src_ip, dst_ip) == 0;
}

size_t tcp_to_bytes(const TCPSegment* segment, uint8_t* out, size_t out_size)
{
    size_t total_len;

    if (segment == NULL || out == NULL || segment->payload_len > TCP_MAX_PAYLOAD) {
        return 0;
    }

    total_len = TCP_HEADER_SIZE + segment->payload_len;
    if (out_size < total_len) {
        return 0;
    }

    write_u16_be(out + 0, segment->src_port);
    write_u16_be(out + 2, segment->dst_port);
    write_u32_be(out + 4, segment->seq_num);
    write_u32_be(out + 8, segment->ack_num);
    out[12] = (uint8_t)(segment->data_offset << 4);  // upper 4 bits
    out[13] = segment->flags;
    write_u16_be(out + 14, segment->window);
    write_u16_be(out + 16, 0);  // checksum placeholder
    write_u16_be(out + 18, segment->urgent_ptr);

    if (segment->payload_len > 0) {
        memcpy(out + TCP_HEADER_SIZE, segment->payload, segment->payload_len);
    }

    return total_len;
}

int tcp_from_bytes(TCPSegment* segment, const uint8_t* raw, size_t raw_len)
{
    size_t header_len;
    size_t payload_len;

    if (segment == NULL || raw == NULL || raw_len < TCP_HEADER_SIZE) {
        return 0;
    }

    tcp_init(segment);
    segment->src_port = read_u16_be(raw + 0);
    segment->dst_port = read_u16_be(raw + 2);
    segment->seq_num = read_u32_be(raw + 4);
    segment->ack_num = read_u32_be(raw + 8);
    segment->data_offset = raw[12] >> 4;
    segment->flags = raw[13];
    segment->window = read_u16_be(raw + 14);
    segment->checksum = read_u16_be(raw + 16);
    segment->urgent_ptr = read_u16_be(raw + 18);

    header_len = (size_t)segment->data_offset * 4u;
    if (header_len < TCP_HEADER_SIZE || header_len > raw_len) {
        return 0;
    }

    payload_len = raw_len - header_len;
    if (payload_len > TCP_MAX_PAYLOAD) {
        return 0;
    }

    segment->payload_len = payload_len;
    if (payload_len > 0) {
        memcpy(segment->payload, raw + header_len, payload_len);
    }

    return 1;
}
