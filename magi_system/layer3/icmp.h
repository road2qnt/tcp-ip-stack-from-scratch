#ifndef ICMP_H
#define ICMP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "../core/packet.h"

#define ICMP_ECHO_REPLY 0
#define ICMP_DEST_UNREACHABLE 3
#define ICMP_ECHO_REQUEST 8
#define ICMP_TIME_EXCEEDED 11

#define ICMP_MAX_PAYLOAD 1472
#define ICMP_HEADER_SIZE 8

typedef struct ICMPMessage {
    Packet base;
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
    uint8_t payload[ICMP_MAX_PAYLOAD];
    size_t payload_len;
} ICMPMessage;

void icmp_init(ICMPMessage* message);
int icmp_create(
    ICMPMessage* message,
    uint8_t type,
    uint8_t code,
    uint16_t identifier,
    uint16_t sequence,
    const uint8_t* payload,
    size_t payload_len
);
uint16_t icmp_compute_checksum(const uint8_t* raw, size_t raw_len);
bool icmp_validate_checksum(const ICMPMessage* message);
size_t icmp_to_bytes(const ICMPMessage* message, uint8_t* out, size_t out_size);
int icmp_from_bytes(ICMPMessage* message, const uint8_t* raw, size_t raw_len);

#endif
