#ifndef MAGI_SOCKET_H
#define MAGI_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../layer4/tcp_socket.h"
#include "../layer4/udp.h"
#include "../layer3/ipv4.h"

#define MAGI_SOCKET_MAX_SOCKETS 16
#define MAGI_SOCKET_BUFFER_SIZE 4096
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2

// Forward declaration
typedef struct Host Host;

// MagiSocket types
typedef enum {
    MAGI_SOCK_UNUSED,
    MAGI_SOCK_TCP,
    MAGI_SOCK_UDP
} MagiSocketType;

typedef struct MagiSocket {
    bool in_use;
    MagiSocketType sock_type;
    
    // TCP-specific
    TCPSocket* tcp_sock;  // Points to host's tcp_sockets array slot
    int tcp_slot;         // Index in host's tcp_sockets
    
    // UDP-specific
    uint16_t udp_local_port;
    
    // Bound address
    IpAddress local_ip;
    IpAddress remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    
    // Listening state
    bool is_listening;
    
    // The host that owns this socket
    Host* host;
} MagiSocket;

// MagiSocket API
int magi_socket_init_all(MagiSocket* sockets, size_t count);
void magi_socket_reset_all(void);
int magi_socket(int domain, int type);
int magi_bind(int sockfd, const IpAddress* ip, uint16_t port);
int magi_listen(int sockfd, int backlog);
int magi_accept(int sockfd, IpAddress* client_ip, uint16_t* client_port);
int magi_connect(int sockfd, const IpAddress* ip, uint16_t port);
int magi_send(int sockfd, const uint8_t* data, size_t len);
int magi_sendto(int sockfd, const uint8_t* data, size_t len,
                const IpAddress* dst_ip, uint16_t dst_port);
int magi_recv(int sockfd, uint8_t* buf, size_t bufsize);
int magi_recvfrom(int sockfd, uint8_t* buf, size_t bufsize,
                  IpAddress* src_ip, uint16_t* src_port);
int magi_close(int sockfd);

// Internal: associate socket with a host
int magi_socket_attach_host(int sockfd, Host* host);

// DNS resolution helper
int magi_resolve(const char* hostname, IpAddress* out_ip);

// HTTP helper
int magi_http_get(int sockfd, const char* hostname, const char* path, char* response, size_t resp_size);

#endif
