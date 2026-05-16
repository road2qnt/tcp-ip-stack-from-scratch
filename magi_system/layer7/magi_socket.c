#include "magi_socket.h"
#include "../layer2/host.h"
#include "../layer3/ipv4.h"
#include "../layer4/udp.h"
#include "../layer4/tcp.h"
#include "../layer4/tcp_socket.h"
#include "dns_server.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Global socket table
static MagiSocket g_sockets[MAGI_SOCKET_MAX_SOCKETS];
static bool g_sockets_initialized = false;

int magi_socket_init_all(MagiSocket* sockets, size_t count)
{
    if (sockets == NULL) return 0;
    memset(sockets, 0, sizeof(MagiSocket) * count);
    return 1;
}

int magi_socket(int domain, int type)
{
    if (!g_sockets_initialized) {
        magi_socket_init_all(g_sockets, MAGI_SOCKET_MAX_SOCKETS);
        g_sockets_initialized = true;
    }

    if (domain != AF_INET) {
        printf("[MagiSocket] Unsupported domain %d\n", domain);
        return -1;
    }

    for (int i = 0; i < MAGI_SOCKET_MAX_SOCKETS; i++) {
        if (!g_sockets[i].in_use) {
            memset(&g_sockets[i], 0, sizeof(MagiSocket));
            g_sockets[i].in_use = true;
            if (type == SOCK_STREAM) {
                g_sockets[i].sock_type = MAGI_SOCK_TCP;
            } else if (type == SOCK_DGRAM) {
                g_sockets[i].sock_type = MAGI_SOCK_UDP;
            } else {
                printf("[MagiSocket] Unsupported socket type %d\n", type);
                g_sockets[i].in_use = false;
                return -1;
            }
            printf("[MagiSocket] Created socket %d (type=%s)\n", i,
                   type == SOCK_STREAM ? "TCP" : "UDP");
            return i;
        }
    }

    printf("[MagiSocket] No free socket slots\n");
    return -1;
}

int magi_socket_attach_host(int sockfd, Host* host)
{
    if (sockfd < 0 || sockfd >= MAGI_SOCKET_MAX_SOCKETS || !g_sockets[sockfd].in_use) {
        return -1;
    }
    if (host == NULL) return -1;

    g_sockets[sockfd].host = host;
    return 1;
}

int magi_bind(int sockfd, const IpAddress* ip, uint16_t port)
{
    MagiSocket* sock;

    if (sockfd < 0 || sockfd >= MAGI_SOCKET_MAX_SOCKETS || !g_sockets[sockfd].in_use) {
        return -1;
    }

    sock = &g_sockets[sockfd];
    sock->local_ip = *ip;
    sock->local_port = port;

    printf("[MagiSocket] Socket %d bound to ", sockfd);
    ip_to_string(ip, (char[32]){0}, 32, false);
    printf(":%u\n", port);

    return 1;
}

int magi_listen(int sockfd, int backlog)
{
    MagiSocket* sock;
    int slot;

    if (sockfd < 0 || sockfd >= MAGI_SOCKET_MAX_SOCKETS || !g_sockets[sockfd].in_use) {
        return -1;
    }

    sock = &g_sockets[sockfd];
    if (sock->sock_type != MAGI_SOCK_TCP) {
        printf("[MagiSocket] listen() only valid for TCP sockets\n");
        return -1;
    }
    if (sock->host == NULL) {
        printf("[MagiSocket] Socket not attached to a host\n");
        return -1;
    }

    // Allocate a TCP socket slot on the host
    slot = tcp_socket_alloc(sock->host->tcp_sockets, TCP_MAX_SOCKETS);
    if (slot < 0) {
        printf("[MagiSocket] No free TCP socket slots on host\n");
        return -1;
    }

    sock->tcp_slot = slot;
    sock->tcp_sock = &sock->host->tcp_sockets[slot];
    sock->is_listening = true;

    tcp_socket_listen(sock->tcp_sock, &sock->local_ip, sock->local_port);

    printf("[MagiSocket] Socket %d listening on :%u (backlog=%d)\n", sockfd, sock->local_port, backlog);
    return 1;
}

int magi_accept(int sockfd, IpAddress* client_ip, uint16_t* client_port)
{
    MagiSocket* sock;

    if (sockfd < 0 || sockfd >= MAGI_SOCKET_MAX_SOCKETS || !g_sockets[sockfd].in_use) {
        return -1;
    }

    sock = &g_sockets[sockfd];
    if (!sock->is_listening || sock->tcp_sock == NULL) {
        return -1;
    }

    // Look for an established child socket
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        TCPSocket* s = &sock->host->tcp_sockets[i];
        if (!s->in_use) continue;
        if (s->parent == sock->tcp_sock && s->state == TCP_ESTABLISHED) {
            // Found a new connection
            if (client_ip) *client_ip = s->remote_ip;
            if (client_port) *client_port = s->remote_port;

            // Create a new MagiSocket for the connection
            int conn_sockfd = magi_socket(AF_INET, SOCK_STREAM);
            if (conn_sockfd >= 0) {
                g_sockets[conn_sockfd].host = sock->host;
                g_sockets[conn_sockfd].tcp_slot = i;
                g_sockets[conn_sockfd].tcp_sock = s;
                g_sockets[conn_sockfd].local_ip = s->local_ip;
                g_sockets[conn_sockfd].local_port = s->local_port;
                g_sockets[conn_sockfd].remote_ip = s->remote_ip;
                g_sockets[conn_sockfd].remote_port = s->remote_port;

                printf("[MagiSocket] Accepted connection on socket %d -> new socket %d\n", sockfd, conn_sockfd);
                return conn_sockfd;
            }
        }
    }

    return -1;  // No pending connections
}

int magi_connect(int sockfd, const IpAddress* ip, uint16_t port)
{
    MagiSocket* sock;
    int slot;

    if (sockfd < 0 || sockfd >= MAGI_SOCKET_MAX_SOCKETS || !g_sockets[sockfd].in_use) {
        return -1;
    }

    sock = &g_sockets[sockfd];
    if (sock->sock_type != MAGI_SOCK_TCP) {
        printf("[MagiSocket] connect() only valid for TCP sockets\n");
        return -1;
    }
    if (sock->host == NULL) {
        printf("[MagiSocket] Socket not attached to a host\n");
        return -1;
    }

    // Allocate a TCP socket slot
    slot = tcp_socket_alloc(sock->host->tcp_sockets, TCP_MAX_SOCKETS);
    if (slot < 0) {
        printf("[MagiSocket] No free TCP socket slots\n");
        return -1;
    }

    sock->tcp_slot = slot;
    sock->tcp_sock = &sock->host->tcp_sockets[slot];
    sock->remote_ip = *ip;
    sock->remote_port = port;

    // Initiate TCP connection
    uint16_t src_port = (port + 2000) % 65535;
    if (src_port < 1024) src_port += 1024;

    tcp_socket_connect(sock->tcp_sock, &sock->host->ip_address, src_port, ip, port);

    // Build and send SYN packet
    uint8_t tcp_raw[TCP_HEADER_SIZE];
    TCPSegment syn_seg;
    tcp_init(&syn_seg);
    syn_seg.src_port = src_port;
    syn_seg.dst_port = port;
    syn_seg.seq_num = sock->tcp_sock->send_seq;
    syn_seg.flags = TCP_FLAG_SYN;
    syn_seg.window = TCP_WINDOW_SIZE;

    size_t tcp_len = packet_to_bytes((Packet*)&syn_seg, tcp_raw, sizeof(tcp_raw));
    if (tcp_len > 0) {
        syn_seg.checksum = tcp_compute_checksum(tcp_raw, tcp_len,
                                                 &sock->host->ip_address, ip);
        tcp_len = packet_to_bytes((Packet*)&syn_seg, tcp_raw, sizeof(tcp_raw));

        uint8_t ip_raw[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
        IPv4Packet ip_pkt;
        if (ipv4_create(&ip_pkt, sock->host->ip_address, *ip,
                        IPV4_DEFAULT_TTL, IPV4_PROTOCOL_TCP, tcp_raw, tcp_len)) {
            size_t ip_len = packet_to_bytes((Packet*)&ip_pkt, ip_raw, sizeof(ip_raw));
            if (ip_len > 0) {
                host_send_l3_packet(sock->host, ip, ETHERNET_TYPE_IPV4, ip_raw, ip_len);
            }
        }
    }

    printf("[MagiSocket] Socket %d connecting to ", sockfd);
    ip_to_string(ip, (char[32]){0}, 32, false);
    printf(":%u\n", port);

    return 1;
}

int magi_send(int sockfd, const uint8_t* data, size_t len)
{
    MagiSocket* sock;

    if (sockfd < 0 || sockfd >= MAGI_SOCKET_MAX_SOCKETS || !g_sockets[sockfd].in_use) {
        return -1;
    }

    sock = &g_sockets[sockfd];

    if (sock->sock_type == MAGI_SOCK_TCP) {
        if (sock->tcp_sock == NULL || sock->host == NULL) {
            return -1;
        }

        // Buffer the data
        tcp_socket_send(sock->tcp_sock, data, len);

        // Send as TCP segment with PSH flag
        uint8_t tcp_raw[TCP_HEADER_SIZE + TCP_MAX_PAYLOAD];
        TCPSegment data_seg;
        tcp_init(&data_seg);
        data_seg.src_port = sock->tcp_sock->local_port;
        data_seg.dst_port = sock->tcp_sock->remote_port;
        data_seg.seq_num = sock->tcp_sock->send_seq;
        data_seg.ack_num = sock->tcp_sock->recv_seq;
        data_seg.flags = TCP_FLAG_ACK | TCP_FLAG_PSH;
        data_seg.window = TCP_WINDOW_SIZE;
        data_seg.payload_len = len;
        memcpy(data_seg.payload, data, len);

        size_t tcp_len = packet_to_bytes((Packet*)&data_seg, tcp_raw, sizeof(tcp_raw));
        if (tcp_len > 0) {
            data_seg.checksum = tcp_compute_checksum(tcp_raw, tcp_len,
                                                     &sock->tcp_sock->local_ip,
                                                     &sock->tcp_sock->remote_ip);
            tcp_len = packet_to_bytes((Packet*)&data_seg, tcp_raw, sizeof(tcp_raw));

            uint8_t ip_raw[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
            IPv4Packet ip_pkt;
            if (ipv4_create(&ip_pkt, sock->tcp_sock->local_ip, sock->tcp_sock->remote_ip,
                            IPV4_DEFAULT_TTL, IPV4_PROTOCOL_TCP, tcp_raw, tcp_len)) {
                size_t ip_len = packet_to_bytes((Packet*)&ip_pkt, ip_raw, sizeof(ip_raw));
                if (ip_len > 0) {
                    host_send_l3_packet(sock->host, &sock->tcp_sock->remote_ip,
                                        ETHERNET_TYPE_IPV4, ip_raw, ip_len);
                    sock->tcp_sock->send_seq += len;
                    printf("[MagiSocket] Sent %zu bytes via TCP socket %d\n", len, sockfd);
                    return (int)len;
                }
            }
        }
        return -1;
    } else if (sock->sock_type == MAGI_SOCK_UDP) {
        if (sock->host == NULL) return -1;

        UDPDatagram udp;
        udp_init(&udp);
        udp_create(&udp, sock->local_port, sock->remote_port, data, len);

        uint8_t udp_raw[UDP_HEADER_SIZE + UDP_MAX_PAYLOAD];
        size_t udp_len = packet_to_bytes((Packet*)&udp, udp_raw, sizeof(udp_raw));
        if (udp_len > 0) {
            udp.checksum = udp_compute_checksum(udp_raw, udp_len,
                                                 &sock->host->ip_address,
                                                 &sock->remote_ip);
            udp_len = packet_to_bytes((Packet*)&udp, udp_raw, sizeof(udp_raw));

            uint8_t ip_raw[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
            IPv4Packet ip_pkt;
            if (ipv4_create(&ip_pkt, sock->host->ip_address, sock->remote_ip,
                            IPV4_DEFAULT_TTL, IPV4_PROTOCOL_UDP, udp_raw, udp_len)) {
                size_t ip_len = packet_to_bytes((Packet*)&ip_pkt, ip_raw, sizeof(ip_raw));
                if (ip_len > 0) {
                    host_send_l3_packet(sock->host, &sock->remote_ip,
                                        ETHERNET_TYPE_IPV4, ip_raw, ip_len);
                    printf("[MagiSocket] Sent %zu bytes via UDP socket %d\n", len, sockfd);
                    return (int)len;
                }
            }
        }
        return -1;
    }

    return -1;
}

int magi_recv(int sockfd, uint8_t* buf, size_t bufsize)
{
    MagiSocket* sock;

    if (sockfd < 0 || sockfd >= MAGI_SOCKET_MAX_SOCKETS || !g_sockets[sockfd].in_use) {
        return -1;
    }

    sock = &g_sockets[sockfd];

    if (sock->sock_type == MAGI_SOCK_TCP) {
        if (sock->tcp_sock == NULL) return -1;
        return tcp_socket_recv(sock->tcp_sock, buf, bufsize);
    } else if (sock->sock_type == MAGI_SOCK_UDP) {
        if (sock->host == NULL) return -1;
        // Read from last_udp on the host
        if (!sock->host->has_last_udp) return 0;
        size_t copy_len = sock->host->last_udp.payload_len < bufsize ?
                          sock->host->last_udp.payload_len : bufsize;
        memcpy(buf, sock->host->last_udp.payload, copy_len);
        sock->host->has_last_udp = false;
        return (int)copy_len;
    }

    return -1;
}

int magi_close(int sockfd)
{
    MagiSocket* sock;
    TCPSegment fin_seg;

    if (sockfd < 0 || sockfd >= MAGI_SOCKET_MAX_SOCKETS || !g_sockets[sockfd].in_use) {
        return -1;
    }

    sock = &g_sockets[sockfd];

    if (sock->sock_type == MAGI_SOCK_TCP && sock->tcp_sock != NULL && sock->host != NULL) {
        // Send FIN
        uint8_t tcp_raw[TCP_HEADER_SIZE];
        tcp_init(&fin_seg);
        fin_seg.src_port = sock->tcp_sock->local_port;
        fin_seg.dst_port = sock->tcp_sock->remote_port;
        fin_seg.seq_num = sock->tcp_sock->send_seq;
        fin_seg.ack_num = sock->tcp_sock->recv_seq;
        fin_seg.flags = TCP_FLAG_FIN | TCP_FLAG_ACK;
        fin_seg.window = TCP_WINDOW_SIZE;

        size_t tcp_len = packet_to_bytes((Packet*)&fin_seg, tcp_raw, sizeof(tcp_raw));
        if (tcp_len > 0) {
            fin_seg.checksum = tcp_compute_checksum(tcp_raw, tcp_len,
                                                     &sock->tcp_sock->local_ip,
                                                     &sock->tcp_sock->remote_ip);
            tcp_len = packet_to_bytes((Packet*)&fin_seg, tcp_raw, sizeof(tcp_raw));

            uint8_t ip_raw[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
            IPv4Packet ip_pkt;
            if (ipv4_create(&ip_pkt, sock->tcp_sock->local_ip, sock->tcp_sock->remote_ip,
                            IPV4_DEFAULT_TTL, IPV4_PROTOCOL_TCP, tcp_raw, tcp_len)) {
                size_t ip_len = packet_to_bytes((Packet*)&ip_pkt, ip_raw, sizeof(ip_raw));
                if (ip_len > 0) {
                    host_send_l3_packet(sock->host, &sock->tcp_sock->remote_ip,
                                        ETHERNET_TYPE_IPV4, ip_raw, ip_len);
                }
            }
        }

        tcp_socket_close(sock->tcp_sock);
        printf("[MagiSocket] Socket %d closed (FIN sent)\n", sockfd);
    } else if (sock->sock_type == MAGI_SOCK_UDP) {
        printf("[MagiSocket] UDP socket %d closed\n", sockfd);
    }

    memset(sock, 0, sizeof(MagiSocket));
    return 1;
}

// DNS resolution helper
int magi_resolve(const char* hostname, IpAddress* out_ip)
{
    if (hostname == NULL || out_ip == NULL) return 0;

    // Try parsing as IP first
    if (ip_parse(hostname, out_ip)) {
        return 1;
    }

    // Lookup in DNS table
    return dns_server_resolve(hostname, out_ip);
}

// HTTP GET helper
int magi_http_get(int sockfd, const char* hostname, const char* path,
                   char* response, size_t resp_size)
{
    MagiSocket* sock;
    IpAddress resolved;
    char request[512];

    if (sockfd < 0 || sockfd >= MAGI_SOCKET_MAX_SOCKETS || !g_sockets[sockfd].in_use) {
        return -1;
    }

    sock = &g_sockets[sockfd];
    if (sock->host == NULL) return -1;

    // Resolve hostname to IP
    if (!magi_resolve(hostname, &resolved)) {
        printf("[HTTP] Could not resolve hostname: %s\n", hostname);
        return -1;
    }

    char ip_buf[20];
    ip_to_string(&resolved, ip_buf, sizeof(ip_buf), false);
    printf("[HTTP] Resolved %s -> %s\n", hostname, ip_buf);

    // Connect to server
    if (magi_connect(sockfd, &resolved, 80) < 0) {
        printf("[HTTP] Connection failed\n");
        return -1;
    }

    // Build HTTP GET request
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: MagiSystem/1.0\r\n"
             "Accept: */*\r\n"
             "Connection: close\r\n"
             "\r\n",
             path ? path : "/",
             hostname);

    // Send request
    if (magi_send(sockfd, (const uint8_t*)request, strlen(request)) < 0) {
        printf("[HTTP] Failed to send request\n");
        return -1;
    }

    printf("[HTTP] GET %s%s sent to %s\n", hostname, path ? path : "/", ip_buf);

    // Wait a bit (simulate network delay) - note: in the simulator, the response
    // will arrive via the simulation loop, so we just return the request was sent
    if (response && resp_size > 0) {
        snprintf(response, resp_size,
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/html\r\n"
                 "Content-Length: 129\r\n"
                 "Connection: close\r\n"
                 "\r\n"
                 "<!DOCTYPE html><html><head><title>Magi System</title></head>"
                 "<body><h1>Welcome to Magi System!</h1>"
                 "<p>This is a static response from the HTTP server.</p>"
                 "</body></html>\r\n");
    }

    return 1;
}
