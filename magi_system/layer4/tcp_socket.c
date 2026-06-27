#include "tcp_socket.h"
#include <string.h>
#include <stdio.h>

static void print_ip(const IpAddress* ip)
{
    printf("%u.%u.%u.%u", ip->octet[0], ip->octet[1], ip->octet[2], ip->octet[3]);
}

static void tcp_prepare_ack(const TCPSocket* socket, TCPSegment* response)
{
    uint8_t raw[TCP_HEADER_SIZE];
    size_t raw_len;

    tcp_init(response);
    response->src_port = socket->local_port;
    response->dst_port = socket->remote_port;
    response->seq_num = socket->send_seq;
    response->ack_num = socket->recv_seq;
    response->flags = TCP_FLAG_ACK;
    response->window = TCP_WINDOW_SIZE;

    raw_len = tcp_to_bytes(response, raw, sizeof(raw));
    if (raw_len > 0) {
        response->checksum = tcp_compute_checksum(raw, raw_len,
                                                  &socket->local_ip, &socket->remote_ip);
    }
}

static int tcp_buffer_payload(TCPSocket* socket, uint32_t seq_num,
                              const uint8_t* payload, size_t payload_len)
{
    TCPReceiveBuffer* buffer;
    size_t offset;
    size_t new_count = 0;
    size_t ready = 0;

    if (payload_len == 0) return 1;

    buffer = &socket->recv_buffer;

    if (seq_num < socket->recv_seq) {
        uint32_t already_received = socket->recv_seq - seq_num;
        if (already_received >= payload_len) {
            return 1;
        }
        payload += already_received;
        payload_len -= already_received;
        seq_num += already_received;
    }

    offset = (size_t)(seq_num - socket->recv_seq);
    if (offset >= TCP_SOCKET_BUFFER_SIZE ||
        payload_len > TCP_SOCKET_BUFFER_SIZE - offset) {
        return 0;
    }

    for (size_t i = 0; i < payload_len; i++) {
        if (!buffer->pending_valid[offset + i]) {
            new_count++;
        }
    }

    if (buffer->size + buffer->pending_count + new_count > TCP_SOCKET_BUFFER_SIZE) {
        return 0;
    }

    for (size_t i = 0; i < payload_len; i++) {
        size_t pos = offset + i;
        if (!buffer->pending_valid[pos]) {
            buffer->pending[pos] = payload[i];
            buffer->pending_valid[pos] = true;
            buffer->pending_count++;
        }
    }

    while (ready < TCP_SOCKET_BUFFER_SIZE && buffer->pending_valid[ready]) {
        ready++;
    }

    if (ready > 0) {
        memcpy(buffer->data + buffer->size, buffer->pending, ready);
        buffer->size += ready;
        socket->recv_seq += (uint32_t)ready;
        buffer->pending_count -= ready;

        memmove(buffer->pending, buffer->pending + ready,
                TCP_SOCKET_BUFFER_SIZE - ready);
        memmove(buffer->pending_valid, buffer->pending_valid + ready,
                (TCP_SOCKET_BUFFER_SIZE - ready) * sizeof(buffer->pending_valid[0]));
        memset(buffer->pending + TCP_SOCKET_BUFFER_SIZE - ready, 0, ready);
        memset(buffer->pending_valid + TCP_SOCKET_BUFFER_SIZE - ready, 0,
               ready * sizeof(buffer->pending_valid[0]));
    }

    socket->has_data = buffer->size > 0;
    return 1;
}

static void tcp_acknowledge_data(TCPSocket* socket, uint32_t ack_num)
{
    uint32_t acked;

    if (ack_num <= socket->send_ack || ack_num > socket->send_seq) {
        return;
    }

    acked = ack_num - socket->send_ack;
    if (acked > socket->send_buffer.size) {
        acked = (uint32_t)socket->send_buffer.size;
    }

    if (acked > 0) {
        memmove(socket->send_buffer.data,
                socket->send_buffer.data + acked,
                socket->send_buffer.size - acked);
        socket->send_buffer.size -= acked;
        socket->send_buffer.start_seq = ack_num;
        printf("[TCP] Send buffer: %u bytes acknowledged, %zu remaining\n",
               acked, socket->send_buffer.size);
    }

    socket->send_ack = ack_num;
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

    /*
     * TIME_WAIT protects an old tuple. If the bounded socket table is full,
     * a subsequent connection can reuse its storage after selecting a new
     * source port, rather than making the simulator permanently unusable.
     */
    for (size_t i = 0; i < count; i++) {
        if (sockets[i].in_use && !sockets[i].is_listening &&
            sockets[i].state == TCP_TIME_WAIT) {
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
        if (sockets[i].state == TCP_CLOSED) continue;
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

uint16_t tcp_socket_select_source_port(const TCPSocket* sockets, size_t count,
                                       const IpAddress* remote_ip, uint16_t remote_port,
                                       uint16_t preferred_port)
{
    uint16_t candidate = preferred_port >= 1024 ? preferred_port : 49152;

    if (sockets == NULL || remote_ip == NULL) return 0;

    for (uint32_t attempt = 0; attempt <= 65535u - 1024u; attempt++) {
        bool used = false;

        for (size_t i = 0; i < count; i++) {
            if (!sockets[i].in_use || sockets[i].state == TCP_CLOSED) continue;
            if (sockets[i].local_port == candidate &&
                sockets[i].remote_port == remote_port &&
                ip_octets_equal_public(&sockets[i].remote_ip, remote_ip)) {
                used = true;
                break;
            }
        }

        if (!used) return candidate;
        candidate = candidate == 65535 ? 1024 : (uint16_t)(candidate + 1);
    }

    return 0;
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
    if (!socket->has_data || socket->recv_buffer.size == 0) return 0;

    copy_len = socket->recv_buffer.size < max_len ? socket->recv_buffer.size : max_len;
    memcpy(out, socket->recv_buffer.data, copy_len);

    if (copy_len < socket->recv_buffer.size) {
        memmove(socket->recv_buffer.data, socket->recv_buffer.data + copy_len,
                socket->recv_buffer.size - copy_len);
    }
    socket->recv_buffer.size -= copy_len;
    socket->has_data = socket->recv_buffer.size > 0;

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
            // Received a connection-opening SYN without an invalid control mix.
            if ((segment->flags & TCP_FLAG_SYN) &&
                !(segment->flags & (TCP_FLAG_ACK | TCP_FLAG_FIN | TCP_FLAG_RST))) {
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
            // A SYN consumes one local sequence number and must be acknowledged.
            if ((segment->flags & TCP_FLAG_SYN) && (segment->flags & TCP_FLAG_ACK) &&
                !(segment->flags & (TCP_FLAG_FIN | TCP_FLAG_RST)) &&
                segment->ack_num == socket->send_seq + 1u) {
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
            if ((segment->flags & TCP_FLAG_ACK) &&
                !(segment->flags & (TCP_FLAG_SYN | TCP_FLAG_FIN | TCP_FLAG_RST)) &&
                segment->ack_num == socket->send_seq + 1u &&
                segment->seq_num == socket->recv_seq) {
                socket->send_ack = segment->ack_num;
                socket->send_seq = segment->ack_num;
                socket->state = TCP_ESTABLISHED;
                printf("[TCP] SYN_RECEIVED: Received ACK, socket -> ESTABLISHED!\n");

                if (segment->payload_len > 0) {
                    uint32_t previous_recv_seq = socket->recv_seq;
                    if (tcp_buffer_payload(socket, segment->seq_num,
                                           segment->payload, segment->payload_len)) {
                        printf("[TCP] Received %u bytes of contiguous data\n",
                               socket->recv_seq - previous_recv_seq);
                    } else {
                        printf("[TCP] Receive buffer full or segment outside window\n");
                    }
                    tcp_prepare_ack(socket, response);
                }
                return 1;
            }
            break;
        }

        case TCP_ESTABLISHED: {
            bool has_payload = segment->payload_len > 0;

            if (segment->flags & TCP_FLAG_ACK) {
                tcp_acknowledge_data(socket, segment->ack_num);
            }

            if (has_payload) {
                uint32_t previous_recv_seq = socket->recv_seq;
                if (tcp_buffer_payload(socket, segment->seq_num,
                                       segment->payload, segment->payload_len)) {
                    if (socket->recv_seq != previous_recv_seq) {
                        printf("[TCP] Received %u bytes of contiguous data (seq=%u)\n",
                               socket->recv_seq - previous_recv_seq, segment->seq_num);
                    } else {
                        printf("[TCP] Buffered out-of-order data (seq=%u)\n", segment->seq_num);
                    }
                } else {
                    printf("[TCP] Receive buffer full or segment outside window\n");
                }
            }

            if (segment->flags & TCP_FLAG_FIN) {
                uint32_t fin_seq = segment->seq_num + (uint32_t)segment->payload_len;
                if (fin_seq != socket->recv_seq) {
                    tcp_prepare_ack(socket, response);
                    return 1;
                }
                socket->recv_seq++;
                printf("[TCP] ESTABLISHED: Received FIN\n");

                tcp_prepare_ack(socket, response);
                socket->state = TCP_CLOSE_WAIT;
                printf("[TCP] ESTABLISHED -> CLOSE_WAIT\n");
                return 1;
            }

            if (has_payload) {
                tcp_prepare_ack(socket, response);
                return 1;
            }

            if (segment->flags & TCP_FLAG_ACK) {
                return 1;
            }
            break;
        }

        case TCP_FIN_WAIT_1: {
            bool has_payload = segment->payload_len > 0;
            bool fin_acked = (segment->flags & TCP_FLAG_ACK) &&
                              segment->ack_num == socket->send_seq;

            if (has_payload) {
                uint32_t previous_recv_seq = socket->recv_seq;
                if (tcp_buffer_payload(socket, segment->seq_num,
                                       segment->payload, segment->payload_len) &&
                    socket->recv_seq != previous_recv_seq) {
                    printf("[TCP] FIN_WAIT_1: Received %u bytes of data\n",
                           socket->recv_seq - previous_recv_seq);
                }
            }

            if (segment->flags & TCP_FLAG_FIN) {
                uint32_t fin_seq = segment->seq_num + (uint32_t)segment->payload_len;
                if (fin_seq != socket->recv_seq) {
                    tcp_prepare_ack(socket, response);
                    return 1;
                }
                socket->recv_seq++;
                printf("[TCP] FIN_WAIT_1: Received FIN\n");

                tcp_prepare_ack(socket, response);
                socket->state = fin_acked ? TCP_TIME_WAIT : TCP_CLOSING;
                printf("[TCP] FIN_WAIT_1 -> %s\n", tcp_state_name(socket->state));
                return 1;
            }

            if (has_payload) {
                tcp_prepare_ack(socket, response);
            }

            if (fin_acked) {
                socket->send_ack = segment->ack_num;
                socket->state = TCP_FIN_WAIT_2;
                printf("[TCP] FIN_WAIT_1 -> FIN_WAIT_2\n");
                return 1;
            }

            if (has_payload) {
                return 1;
            }
            break;
        }

        case TCP_FIN_WAIT_2: {
            bool has_payload = segment->payload_len > 0;

            if (has_payload) {
                uint32_t previous_recv_seq = socket->recv_seq;
                if (tcp_buffer_payload(socket, segment->seq_num,
                                       segment->payload, segment->payload_len) &&
                    socket->recv_seq != previous_recv_seq) {
                    printf("[TCP] FIN_WAIT_2: Received %u bytes of data\n",
                           socket->recv_seq - previous_recv_seq);
                }
            }

            if (segment->flags & TCP_FLAG_FIN) {
                uint32_t fin_seq = segment->seq_num + (uint32_t)segment->payload_len;
                if (fin_seq != socket->recv_seq) {
                    tcp_prepare_ack(socket, response);
                    return 1;
                }
                socket->recv_seq++;
                printf("[TCP] FIN_WAIT_2: Received FIN\n");

                tcp_prepare_ack(socket, response);
                socket->state = TCP_TIME_WAIT;
                printf("[TCP] FIN_WAIT_2 -> TIME_WAIT\n");
                return 1;
            }

            if (has_payload) {
                tcp_prepare_ack(socket, response);
                return 1;
            }
            break;
        }

        case TCP_CLOSING: {
            if ((segment->flags & TCP_FLAG_ACK) &&
                segment->ack_num == socket->send_seq) {
                socket->send_ack = segment->ack_num;
                socket->state = TCP_TIME_WAIT;
                printf("[TCP] CLOSING -> TIME_WAIT\n");
                return 1;
            }
            break;
        }

        case TCP_LAST_ACK: {
            if ((segment->flags & TCP_FLAG_ACK) &&
                segment->ack_num == socket->send_seq) {
                printf("[TCP] LAST_ACK: Received ACK, socket closed\n");
                tcp_socket_free(socket);
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
