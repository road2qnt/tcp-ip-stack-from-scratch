#ifndef DHCP_SERVER_H
#define DHCP_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../layer3/ipv4.h"
#include "../layer4/udp.h"

typedef struct Host Host;

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_HEADER_SIZE 240
#define DHCP_OPTIONS_SIZE 60

// DHCP message opcodes
#define DHCP_BOOTREQUEST 1
#define DHCP_BOOTREPLY 2

// DHCP message types
#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5
#define DHCP_NAK      6
#define DHCP_RELEASE  7

// DHCP option tags
#define DHCP_OPTION_SUBNET_MASK   1
#define DHCP_OPTION_ROUTER        3
#define DHCP_OPTION_DNS           6
#define DHCP_OPTION_HOSTNAME     12
#define DHCP_OPTION_DOMAIN_NAME  15
#define DHCP_OPTION_LEASE_TIME   51
#define DHCP_OPTION_MSG_TYPE     53
#define DHCP_OPTION_SERVER_ID    54
#define DHCP_OPTION_REQUEST_LIST 55
#define DHCP_OPTION_END         255

// Magic cookie
#define DHCP_MAGIC_COOKIE 0x63825363

#define DHCP_MAX_CLIENTS 16

typedef struct __attribute__((packed)) DHCPMessage {
    uint8_t op;          // 1 = BOOTREQUEST, 2 = BOOTREPLY
    uint8_t htype;       // Hardware address type (1 = Ethernet)
    uint8_t hlen;        // Hardware address length (6 for MAC)
    uint8_t hops;        // Hop count
    uint32_t xid;        // Transaction ID
    uint16_t secs;       // Seconds elapsed
    uint16_t flags;      // Flags
    IpAddress ciaddr;    // Client IP address
    IpAddress yiaddr;    // Your IP address
    IpAddress siaddr;    // Server IP address
    IpAddress giaddr;    // Gateway IP address (relay agent)
    uint8_t chaddr[16];  // Client hardware address
    uint8_t sname[64];   // Server host name (optional)
    uint8_t file[128];   // Boot file name (optional)
    uint32_t magic_cookie;
    uint8_t options[DHCP_OPTIONS_SIZE];
    size_t options_len;
} DHCPMessage;

typedef struct DHCPClient {
    bool in_use;
    uint8_t chaddr[16];
    IpAddress allocated_ip;
    uint32_t lease_time;
    uint32_t xid;
} DHCPClient;

// Initialize the DHCP server for a host
int dhcp_server_init(void);

// Handle incoming DHCP message on port 67
// Returns response length or 0 if no response
int dhcp_server_handle_message(const DHCPMessage* request, DHCPMessage* response);

// Process DHCP Discover -> Offer
int dhcp_server_handle_discover(const DHCPMessage* discover, DHCPMessage* offer);

// Process DHCP Request -> Ack
int dhcp_server_handle_request(const DHCPMessage* request, DHCPMessage* ack);

// Create a DHCP Discover message (client side)
int dhcp_create_discover(DHCPMessage* msg, uint32_t xid, const uint8_t* mac);

// Create a DHCP Request message (client side)
int dhcp_create_request(DHCPMessage* msg, uint32_t xid, const uint8_t* mac,
                         const IpAddress* requested_ip, const IpAddress* server_ip);

// Parse DHCP message type from options
int dhcp_get_msg_type(const DHCPMessage* msg);

// Serialization helpers
int dhcp_message_to_bytes(const DHCPMessage* msg, uint8_t* out, size_t out_size);
int dhcp_message_from_bytes(DHCPMessage* msg, const uint8_t* raw, size_t raw_len);

// Get allocated IP for a client
int dhcp_get_lease(const uint8_t* mac, IpAddress* out_ip);

// Get server's IP address (the host running the DHCP server)
void dhcp_server_set_ip(const IpAddress* ip);

int dhcp_server_attach_host(Host* host);
int dhcp_server_detach_host(void);
Host* dhcp_server_get_bound_host(void);
int dhcp_server_dispatch(Host* host, const UDPDatagram* request, const IpAddress* src_ip);

#endif
