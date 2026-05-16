#ifndef TCP_H
#define TCP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "../core/packet.h"
#include "../layer3/ipv4.h"

#define TCP_HEADER_SIZE 20
#define TCP_MAX_PAYLOAD 1460
#define TCP_WINDOW_SIZE 65535

// TCP Flags
#define TCP_FLAG_FIN  0x01
#define TCP_FLAG_SYN  0x02
#define TCP_FLAG_RST  0x04
#define TCP_FLAG_PSH  0x08
#define TCP_FLAG_ACK  0x10
#define TCP_FLAG_SYN_ACK 0x12

// TCP States
typedef enum TCPState {
    TCP_CLOSED,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSING,
    TCP_TIME_WAIT,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK
} TCPState;

typedef struct TCPSegment {
    Packet base;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t data_offset;  // upper 4 bits = header length in 32-bit words
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
    uint8_t payload[TCP_MAX_PAYLOAD];
    size_t payload_len;
} TCPSegment;

// State name for debugging
const char* tcp_state_name(TCPState state);

void tcp_init(TCPSegment* segment);
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
);
uint16_t tcp_compute_checksum(const uint8_t* raw, size_t raw_len,
                               const IpAddress* src_ip, const IpAddress* dst_ip);
bool tcp_validate_checksum(const TCPSegment* segment,
                           const IpAddress* src_ip, const IpAddress* dst_ip);
size_t tcp_to_bytes(const TCPSegment* segment, uint8_t* out, size_t out_size);
int tcp_from_bytes(TCPSegment* segment, const uint8_t* raw, size_t raw_len);

#endif
