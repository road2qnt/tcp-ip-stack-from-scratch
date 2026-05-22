#include "tcp_socket.h"
#include <string.h>
#include <stdio.h>

static void print_ip(const IpAddress* ip)
{
    printf("%u.%u.%u.%u", ip->octet[0], ip->octet[1], ip->octet[2], ip->octet[3]);
}

void tcp_socket_init(TCPSocket* sockets, size_t count)
{
    if (sockets == NULL) return;
    memset(sockets, 0, sizeof(TCPSocket) * count);
}

int tcp_socket_alloc(TCPSocket* sockets, size_t count)
{
    if (sockets == NULL) return -1;

    for (size_t i = 0; i < count; i++) {
        if (!sockets[i].in_use) {
            memset(&sockets[i], 0, sizeof(TCPSocket));
            sockets[i].in_use = true;
            sockets[i].state = TCP_CLOSED;
            return (int)i;
        }
    }
    return -1;
}

void tcp_socket_free(TCPSocket* socket)
{
    if (socket == NULL) return;
    memset(socket, 0, sizeof(TCPSocket));
}

TCPSocket* tcp_socket_find(TCPSocket* sockets, size_t count,
                            const IpAddress* local_ip, uint16_t local_port,
                            const IpAddress* remote_ip, uint16_t remote_port)
{
    if (sockets == NULL || local_ip == NULL || remote_ip == NULL) return NULL;

    for (size_t i = 0; i < count; i++) {
        if (!sockets[i].in_use) continue;
        if (sockets[i].is_listening) continue;
        if (ip_octets_equal_public(&sockets[i].local_ip, local_ip) &&
            sockets[i].local_port == local_port &&
            ip_octets_equal_public(&sockets[i].remote_ip, remote_ip) &&
            sockets[i].remote_port == remote_port) {
            return &sockets[i];
        }
    }

    for (size_t i = 0; i < count; i++) {
        if (!sockets[i].in_use) continue;
        if (!sockets[i].is_listening) continue;
        if (ip_octets_equal_public(&sockets[i].local_ip, local_ip) &&
            sockets[i].local_port == local_port) {
            return &sockets[i];
        }
    }

    return NULL;
}

int tcp_socket_connect(TCPSocket* socket, const IpAddress* local_ip, uint16_t src_port,
                       const IpAddress* remote_ip, uint16_t dst_port)
{
    if (socket == NULL || local_ip == NULL || remote_ip == NULL) return 0;

    socket->state = TCP_SYN_SENT;
    socket->local_ip = *local_ip;
    socket->local_port = src_port;
    socket->remote_ip = *remote_ip;
    socket->remote_port = dst_port;
    socket->send_seq = 1000;  // Initial sequence number
    socket->recv_ack = 0;
    socket->recv_seq = 0;
    socket->send_ack = 0;
    socket->has_data = false;

    printf("[TCP] Socket (%d) -> SYN_SENT: %u -> ", src_port, dst_port);
    print_ip(&socket->remote_ip);
    printf(":%u\n", dst_port);

    return 1;
}

int tcp_socket_listen(TCPSocket* socket, const IpAddress* local_ip, uint16_t port)
{
    if (socket == NULL || local_ip == NULL) return 0;

    socket->state = TCP_LISTEN;
    socket->local_ip = *local_ip;
    socket->local_port = port;
    socket->is_listening = true;
    socket->send_seq = 0;
    socket->recv_seq = 0;

    printf("[TCP] Socket listening on ");
    print_ip(local_ip);
    printf(":%u\n", port);

    return 1;
}

TCPSocket* tcp_socket_accept(TCPSocket* socket, TCPSocket* all_sockets, size_t count)
{
    if (socket == NULL || all_sockets == NULL || !socket->is_listening) return NULL;

    // Look for a socket in SYN_RECEIVED state that has this as parent
    for (size_t i = 0; i < count; i++) {
        if (&all_sockets[i] == socket) continue;
        if (!all_sockets[i].in_use) continue;
        if (all_sockets[i].parent == socket && all_sockets[i].state == TCP_ESTABLISHED) {
            return &all_sockets[i];
        }
    }

    return NULL;
}

int tcp_socket_send(TCPSocket* socket, const uint8_t* data, size_t len)
{
    if (socket == NULL || data == NULL || len == 0) return 0;
    if (socket->state != TCP_ESTABLISHED) {
        printf("[TCP] Cannot send, socket not established (state=%s)\n", tcp_state_name(socket->state));
        return 0;
    }

    if (socket->send_buffer.size + len > TCP_SOCKET_BUFFER_SIZE) {
        printf("[TCP] Send buffer full\n");
        return 0;
    }

    if (socket->send_buffer.size == 0) {
        socket->send_buffer.start_seq = socket->send_seq;
    }

    memcpy(socket->send_buffer.data + socket->send_buffer.size, data, len);
    socket->send_buffer.size += len;

    printf("[TCP] %u bytes buffered for send (seq=%u)\n", (unsigned)len, socket->send_seq);
    return 1;
}

int tcp_socket_recv(TCPSocket* socket, uint8_t* out, size_t max_len)
{
    size_t copy_len;

    if (socket == NULL || out == NULL || max_len == 0) return 0;
    if (!socket->has_data || socket->last_payload_len == 0) return 0;

    copy_len = socket->last_payload_len < max_len ? socket->last_payload_len : max_len;
    memcpy(out, socket->last_payload, copy_len);

    // Shift buffer
    if (copy_len < socket->last_payload_len) {
        memmove(socket->last_payload, socket->last_payload + copy_len,
                socket->last_payload_len - copy_len);
    }
    socket->last_payload_len -= copy_len;

    if (socket->last_payload_len == 0) {
        socket->has_data = false;
    }

    return (int)copy_len;
}

int tcp_socket_close(TCPSocket* socket)
{
    if (socket == NULL) return 0;

    switch (socket->state) {
        case TCP_ESTABLISHED:
            socket->state = TCP_FIN_WAIT_1;
            printf("[TCP] Socket -> FIN_WAIT_1 (initiating close)\n");
            return 1;

        case TCP_CLOSE_WAIT:
            socket->state = TCP_LAST_ACK;
            printf("[TCP] Socket -> LAST_ACK\n");
            return 1;

        case TCP_CLOSED:
        case TCP_LISTEN:
            socket->state = TCP_CLOSED;
            printf("[TCP] Socket closed\n");
            return 1;

        default:
            printf("[TCP] Cannot close from state %s\n", tcp_state_name(socket->state));
            return 0;
    }
}

bool tcp_socket_has_data(const TCPSocket* socket)
{
    return socket != NULL && socket->has_data;
}

int tcp_socket_receive_segment(TCPSocket* socket, const TCPSegment* segment,
                                const IpAddress* src_ip, const IpAddress* dst_ip,
                                TCPSegment* response)
{
    if (socket == NULL || segment == NULL || src_ip == NULL || dst_ip == NULL || response == NULL) {
        return 0;
    }

    // Validate checksum
    if (!tcp_validate_checksum(segment, src_ip, dst_ip)) {
        printf("[TCP] Invalid checksum, dropping segment\n");
        return 0;
    }

    switch (socket->state) {
        case TCP_LISTEN: {
            // Received SYN → send SYN-ACK
            if (segment->flags & TCP_FLAG_SYN) {
                // Allocate a child socket
                // (This is handled externally - the listener just reports the incoming SYN)
                printf("[TCP] LISTEN: Received SYN from ");
                print_ip(src_ip);
                printf(":%u seq=%u\n", segment->src_port, segment->seq_num);

                // Prepare SYN-ACK response
                socket->recv_seq = segment->seq_num + 1;
                socket->send_seq = 2000 + (uint32_t)(segment->src_port);  // unique ISN

                tcp_init(response);
                response->src_port = segment->dst_port;
                response->dst_port = segment->src_port;
                response->seq_num = socket->send_seq;
                response->ack_num = socket->recv_seq;
                response->flags = TCP_FLAG_SYN_ACK;
                response->window = TCP_WINDOW_SIZE;

                // Compute checksum
                uint8_t raw[TCP_HEADER_SIZE];
                size_t raw_len = tcp_to_bytes(response, raw, sizeof(raw));
                if (raw_len > 0) {
                    response->checksum = tcp_compute_checksum(raw, raw_len, dst_ip, src_ip);
                }

                socket->state = TCP_SYN_RECEIVED;
                printf("[TCP] LISTEN -> SYN_RECEIVED, sending SYN-ACK (seq=%u, ack=%u)\n",
                       socket->send_seq, socket->recv_seq);
                return 1;
            }
            break;
        }

        case TCP_SYN_SENT: {
            // Received SYN-ACK → send ACK
            if ((segment->flags & TCP_FLAG_SYN) && (segment->flags & TCP_FLAG_ACK)) {
                printf("[TCP] SYN_SENT: Received SYN-ACK from ");
                print_ip(src_ip);
                printf(":%u ack=%u\n", segment->src_port, segment->ack_num);

                socket->recv_seq = segment->seq_num + 1;
                socket->send_ack = segment->ack_num;
                socket->send_seq = socket->send_ack;

                // Send ACK
                tcp_init(response);
                response->src_port = socket->local_port;
                response->dst_port = socket->remote_port;
                response->seq_num = socket->send_seq;
                response->ack_num = socket->recv_seq;
                response->flags = TCP_FLAG_ACK;
                response->window = TCP_WINDOW_SIZE;

                uint8_t raw[TCP_HEADER_SIZE];
                size_t raw_len = tcp_to_bytes(response, raw, sizeof(raw));
                if (raw_len > 0) {
                    response->checksum = tcp_compute_checksum(raw, raw_len, &socket->local_ip, &socket->remote_ip);
                }

                socket->state = TCP_ESTABLISHED;
                printf("[TCP] Socket -> ESTABLISHED!\n");
                return 1;
            }
            break;
        }

        case TCP_SYN_RECEIVED: {
            if (segment->flags & TCP_FLAG_ACK) {
                socket->send_ack = segment->ack_num;
                socket->recv_seq = segment->seq_num;
                socket->state = TCP_ESTABLISHED;
                printf("[TCP] SYN_RECEIVED: Received ACK, socket -> ESTABLISHED!\n");

                if (segment->payload_len > 0) {
                    size_t copy_len = segment->payload_len < TCP_MAX_PAYLOAD ? segment->payload_len : TCP_MAX_PAYLOAD;
                    memcpy(socket->last_payload, segment->payload, copy_len);
                    socket->last_payload_len = copy_len;
                    socket->has_data = true;
                    socket->recv_seq = segment->seq_num + segment->payload_len;
                    printf("[TCP] Received %zu bytes of data\n", copy_len);

                    tcp_init(response);
                    response->src_port = segment->dst_port;
                    response->dst_port = segment->src_port;
                    response->seq_num = socket->send_seq;
                    response->ack_num = socket->recv_seq;
                    response->flags = TCP_FLAG_ACK;
                    response->window = TCP_WINDOW_SIZE;

                    uint8_t raw[TCP_HEADER_SIZE];
                    size_t raw_len = tcp_to_bytes(response, raw, sizeof(raw));
                    if (raw_len > 0) {
                        response->checksum = tcp_compute_checksum(raw, raw_len,
                                                                  &socket->local_ip, &socket->remote_ip);
                    }
                }
                return 1;
            }
            break;
        }

        case TCP_ESTABLISHED: {
            // Data transfer or FIN
            if (segment->flags & TCP_FLAG_FIN) {
                socket->recv_seq = segment->seq_num + 1;
                printf("[TCP] ESTABLISHED: Received FIN\n");

                // Send ACK for FIN
                tcp_init(response);
                response->src_port = segment->dst_port;
                response->dst_port = segment->src_port;
                response->seq_num = socket->send_seq;
                response->ack_num = socket->recv_seq;
                response->flags = TCP_FLAG_ACK;
                response->window = TCP_WINDOW_SIZE;

                uint8_t raw[TCP_HEADER_SIZE];
                size_t raw_len = tcp_to_bytes(response, raw, sizeof(raw));
                if (raw_len > 0) {
                    response->checksum = tcp_compute_checksum(raw, raw_len,
                                                              &socket->local_ip, &socket->remote_ip);
                }

                socket->state = TCP_CLOSE_WAIT;
                printf("[TCP] ESTABLISHED -> CLOSE_WAIT\n");
                return 1;
            }

            if (segment->flags & TCP_FLAG_ACK) {
                bool had_data = false;
                if (segment->payload_len > 0) {
                    size_t copy_len = segment->payload_len < TCP_MAX_PAYLOAD ? segment->payload_len : TCP_MAX_PAYLOAD;
                    memcpy(socket->last_payload, segment->payload, copy_len);
                    socket->last_payload_len = copy_len;
                    socket->has_data = true;
                    socket->recv_seq = segment->seq_num + segment->payload_len;
                    had_data = true;
                    printf("[TCP] Received %zu bytes of data (seq=%u)\n", copy_len, segment->seq_num);
                }

                if (segment->ack_num > socket->send_ack && socket->send_buffer.size > 0) {
                    uint32_t acked = segment->ack_num - socket->send_ack;
                    if (acked > socket->send_buffer.size) {
                        acked = (uint32_t)socket->send_buffer.size;
                    }

                    if (acked > 0) {
                        memmove(socket->send_buffer.data,
                                socket->send_buffer.data + acked,
                                socket->send_buffer.size - acked);
                        socket->send_buffer.size -= acked;
                        socket->send_buffer.start_seq = segment->ack_num;
                        printf("[TCP] Send buffer: %u bytes acknowledged, %zu remaining\n",
                               acked, socket->send_buffer.size);
                    }
                }

                socket->send_ack = segment->ack_num;

                if (!had_data) {
                    return 1;
                }

                tcp_init(response);
                response->src_port = socket->local_port;
                response->dst_port = socket->remote_port;
                response->seq_num = socket->send_seq;
                response->ack_num = socket->recv_seq;
                response->flags = TCP_FLAG_ACK;
                response->window = TCP_WINDOW_SIZE;

                uint8_t raw[TCP_HEADER_SIZE];
                size_t raw_len = tcp_to_bytes(response, raw, sizeof(raw));
                if (raw_len > 0) {
                    response->checksum = tcp_compute_checksum(raw, raw_len,
                                                              &socket->local_ip, &socket->remote_ip);
                }
                return 1;
            }
            break;
        }

        case TCP_FIN_WAIT_1: {
            if (segment->flags & TCP_FLAG_FIN) {
                socket->recv_seq = segment->seq_num + 1;
                printf("[TCP] FIN_WAIT_1: Received FIN\n");

                tcp_init(response);
                response->src_port = segment->dst_port;
                response->dst_port = segment->src_port;
                response->seq_num = socket->send_seq;
                response->ack_num = socket->recv_seq;
                response->flags = TCP_FLAG_ACK;
                response->window = TCP_WINDOW_SIZE;

                uint8_t raw[TCP_HEADER_SIZE];
                size_t raw_len = tcp_to_bytes(response, raw, sizeof(raw));
                if (raw_len > 0) {
                    response->checksum = tcp_compute_checksum(raw, raw_len,
                                                              &socket->local_ip, &socket->remote_ip);
                }

                socket->state = TCP_TIME_WAIT;
                printf("[TCP] FIN_WAIT_1 -> TIME_WAIT\n");
                return 1;
            }

            if (segment->flags & TCP_FLAG_ACK) {
                socket->send_ack = segment->ack_num;
                socket->state = TCP_FIN_WAIT_2;
                printf("[TCP] FIN_WAIT_1 -> FIN_WAIT_2\n");
                return 1;
            }
            break;
        }

        case TCP_FIN_WAIT_2: {
            if (segment->flags & TCP_FLAG_FIN) {
                socket->recv_seq = segment->seq_num + 1;
                printf("[TCP] FIN_WAIT_2: Received FIN\n");

                tcp_init(response);
                response->src_port = segment->dst_port;
                response->dst_port = segment->src_port;
                response->seq_num = socket->send_seq;
                response->ack_num = socket->recv_seq;
                response->flags = TCP_FLAG_ACK;
                response->window = TCP_WINDOW_SIZE;

                uint8_t raw[TCP_HEADER_SIZE];
                size_t raw_len = tcp_to_bytes(response, raw, sizeof(raw));
                if (raw_len > 0) {
                    response->checksum = tcp_compute_checksum(raw, raw_len,
                                                              &socket->local_ip, &socket->remote_ip);
                }

                socket->state = TCP_TIME_WAIT;
                printf("[TCP] FIN_WAIT_2 -> TIME_WAIT\n");
                return 1;
            }
            break;
        }

        case TCP_LAST_ACK: {
            if (segment->flags & TCP_FLAG_ACK) {
                printf("[TCP] LAST_ACK: Received ACK, socket closed\n");
                socket->state = TCP_CLOSED;
                return 1;
            }
            break;
        }

        case TCP_TIME_WAIT:
            // Just absorb any straggling packets
            return 1;

        default:
            break;
    }

    return 0;
}
