#ifndef TCP_SOCKET_H
#define TCP_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "tcp.h"
#include "../layer3/ipv4.h"

#define TCP_SOCKET_BUFFER_SIZE 4096

typedef struct TCPSendBuffer {
    uint8_t data[TCP_SOCKET_BUFFER_SIZE];
    size_t size;
    uint32_t start_seq;
} TCPSendBuffer;

typedef struct TCPReceiveBuffer {
    uint8_t data[TCP_SOCKET_BUFFER_SIZE];
    size_t size;
    uint8_t pending[TCP_SOCKET_BUFFER_SIZE];
    bool pending_valid[TCP_SOCKET_BUFFER_SIZE];
    size_t pending_count;
} TCPReceiveBuffer;

typedef struct TCPSocket {
    bool in_use;
    TCPState state;
    
    // Address info
    IpAddress local_ip;
    uint16_t local_port;
    IpAddress remote_ip;
    uint16_t remote_port;
    
    // Sequence numbers
    uint32_t send_seq;       // next seq to send
    uint32_t recv_seq;       // next expected seq
    uint32_t send_ack;       // last ack sent
    uint32_t recv_ack;       // last ack received
    
    // Buffers
    TCPSendBuffer send_buffer;
    TCPReceiveBuffer recv_buffer;
    
    // For listening sockets
    bool is_listening;
    struct TCPSocket* parent;  // for accepted sockets to reference listener
    
    // Last received info (for querying)
    uint8_t last_payload[TCP_MAX_PAYLOAD];
    size_t last_payload_len;
    bool has_data;
} TCPSocket;

#define TCP_MAX_SOCKETS 16

// Socket management
void tcp_socket_init(TCPSocket* sockets, size_t count);
int tcp_socket_alloc(TCPSocket* sockets, size_t count);
void tcp_socket_free(TCPSocket* socket);
TCPSocket* tcp_socket_find(TCPSocket* sockets, size_t count,
                            const IpAddress* local_ip, uint16_t local_port,
                            const IpAddress* remote_ip, uint16_t remote_port);
uint16_t tcp_socket_select_source_port(const TCPSocket* sockets, size_t count,
                                       const IpAddress* remote_ip, uint16_t remote_port,
                                       uint16_t preferred_port);

// TCP operations
int tcp_socket_connect(TCPSocket* socket, const IpAddress* local_ip, uint16_t src_port,
                       const IpAddress* remote_ip, uint16_t dst_port);
int tcp_socket_listen(TCPSocket* socket, const IpAddress* local_ip, uint16_t port);
TCPSocket* tcp_socket_accept(TCPSocket* socket, TCPSocket* all_sockets, size_t count);
int tcp_socket_send(TCPSocket* socket, const uint8_t* data, size_t len);
int tcp_socket_recv(TCPSocket* socket, uint8_t* out, size_t max_len);
int tcp_socket_close(TCPSocket* socket);

// Process incoming TCP segment
int tcp_socket_receive_segment(TCPSocket* socket, const TCPSegment* segment,
                                const IpAddress* src_ip, const IpAddress* dst_ip,
                                TCPSegment* response);

// Check if socket has data available
bool tcp_socket_has_data(const TCPSocket* socket);

#endif
