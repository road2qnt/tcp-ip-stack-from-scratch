#include "icmp.h"

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

static size_t icmp_packet_to_bytes(const Packet* self, uint8_t* out, size_t out_size)
{
    return icmp_to_bytes((const ICMPMessage*)self, out, out_size);
}

static int icmp_packet_from_bytes(Packet* self, const uint8_t* raw, size_t raw_len)
{
    return icmp_from_bytes((ICMPMessage*)self, raw, raw_len);
}

static const PacketVTable ICMP_VTABLE = {
    .to_bytes = icmp_packet_to_bytes,
    .from_bytes = icmp_packet_from_bytes
};

void icmp_init(ICMPMessage* message)
{
    if (message == NULL) {
        return;
    }

    memset(message, 0, sizeof(*message));
    message->base.vtable = &ICMP_VTABLE;
    message->base.type = ICMP;
}

int icmp_create(
    ICMPMessage* message,
    uint8_t type,
    uint8_t code,
    uint16_t identifier,
    uint16_t sequence,
    const uint8_t* payload,
    size_t payload_len
)
{
    if (message == NULL || payload_len > ICMP_MAX_PAYLOAD) {
        return 0;
    }

    if (payload_len > 0 && payload == NULL) {
        return 0;
    }

    icmp_init(message);
    message->type = type;
    message->code = code;
    message->identifier = identifier;
    message->sequence = sequence;
    message->payload_len = payload_len;
    if (payload_len > 0) {
        memcpy(message->payload, payload, payload_len);
    }

    return 1;
}

uint16_t icmp_compute_checksum(const uint8_t* raw, size_t raw_len)
{
    uint32_t sum = 0;

    if (raw == NULL || raw_len == 0) {
        return 0;
    }

    for (size_t i = 0; i < raw_len; i += 2) {
        uint16_t word = (uint16_t)raw[i] << 8;
        if (i + 1 < raw_len) {
            word |= raw[i + 1];
        }
        sum += word;
        while (sum >> 16) {
            sum = (sum & 0xFFFFu) + (sum >> 16);
        }
    }

    return (uint16_t)(~sum & 0xFFFFu);
}

size_t icmp_to_bytes(const ICMPMessage* message, uint8_t* out, size_t out_size)
{
    size_t total_len;
    uint16_t checksum;

    if (message == NULL || out == NULL || message->payload_len > ICMP_MAX_PAYLOAD) {
        return 0;
    }

    total_len = ICMP_HEADER_SIZE + message->payload_len;
    if (out_size < total_len) {
        return 0;
    }

    out[0] = message->type;
    out[1] = message->code;
    write_u16_be(out + 2, 0);
    write_u16_be(out + 4, message->identifier);
    write_u16_be(out + 6, message->sequence);
    if (message->payload_len > 0) {
        memcpy(out + ICMP_HEADER_SIZE, message->payload, message->payload_len);
    }

    checksum = icmp_compute_checksum(out, total_len);
    write_u16_be(out + 2, checksum);
    return total_len;
}

int icmp_from_bytes(ICMPMessage* message, const uint8_t* raw, size_t raw_len)
{
    if (message == NULL || raw == NULL || raw_len < ICMP_HEADER_SIZE) {
        return 0;
    }

    if (raw_len - ICMP_HEADER_SIZE > ICMP_MAX_PAYLOAD) {
        return 0;
    }

    icmp_init(message);
    message->type = raw[0];
    message->code = raw[1];
    message->checksum = read_u16_be(raw + 2);
    message->identifier = read_u16_be(raw + 4);
    message->sequence = read_u16_be(raw + 6);
    message->payload_len = raw_len - ICMP_HEADER_SIZE;
    if (message->payload_len > 0) {
        memcpy(message->payload, raw + ICMP_HEADER_SIZE, message->payload_len);
    }

    return 1;
}

bool icmp_validate_checksum(const ICMPMessage* message)
{
    uint8_t raw[ICMP_HEADER_SIZE + ICMP_MAX_PAYLOAD];
    size_t len;

    if (message == NULL) {
        return false;
    }

    len = icmp_to_bytes(message, raw, sizeof(raw));
    if (len < ICMP_HEADER_SIZE) {
        return false;
    }

    write_u16_be(raw + 2, message->checksum);
    return icmp_compute_checksum(raw, len) == 0;
}
