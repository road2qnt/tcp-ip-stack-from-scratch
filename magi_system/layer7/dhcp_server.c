#include "dhcp_server.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// DHCP server state
static DHCPClient g_clients[DHCP_MAX_CLIENTS];
static int g_next_ip_octet = 20;  // Start allocating from .20
static IpAddress g_server_ip;
static bool g_server_ip_set = false;

// Helper: write 32-bit in network byte order
static void write_u32_be(uint8_t* out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)(value & 0xFF);
}

static uint32_t read_u32_be(const uint8_t* raw)
{
    return ((uint32_t)raw[0] << 24) |
           ((uint32_t)raw[1] << 16) |
           ((uint32_t)raw[2] << 8) |
           (uint32_t)raw[3];
}

static uint16_t read_u16_be(const uint8_t* raw)
{
    return (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
}

static void write_u16_be(uint8_t* out, uint16_t value)
{
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)(value & 0xFF);
}

void dhcp_server_set_ip(const IpAddress* ip)
{
    if (ip) {
        g_server_ip = *ip;
        g_server_ip_set = true;
    }
}

int dhcp_server_init(void)
{
    memset(g_clients, 0, sizeof(g_clients));
    g_next_ip_octet = 20;
    return 1;
}

int dhcp_get_msg_type(const DHCPMessage* msg)
{
    if (msg == NULL) return -1;

    // Parse options to find DHCP message type (option 53)
    size_t offset = 0;
    while (offset < msg->options_len) {
        uint8_t tag = msg->options[offset];
        if (tag == DHCP_OPTION_END) break;
        if (tag == 0) { offset++; continue; }  // Padding

        uint8_t len = msg->options[offset + 1];
        if (tag == DHCP_OPTION_MSG_TYPE && len == 1) {
            return msg->options[offset + 2];
        }
        offset += 2 + len;
    }

    return -1;
}

// Find a free IP address
static int dhcp_allocate_ip(IpAddress* out_ip, const uint8_t* chaddr)
{
    if (out_ip == NULL) return 0;

    // Use the server IP as a base (assume /24 network)
    *out_ip = g_server_ip;
    out_ip->octet[3] = (uint8_t)g_next_ip_octet;
    g_next_ip_octet++;

    // Check for duplicates in a /24 range
    while (g_next_ip_octet < 255) {
        bool duplicate = false;
        for (int i = 0; i < DHCP_MAX_CLIENTS; i++) {
            if (g_clients[i].in_use &&
                ip_octets_equal_public(&g_clients[i].allocated_ip, out_ip)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) break;
        g_next_ip_octet++;
        out_ip->octet[3] = (uint8_t)g_next_ip_octet;
    }

    if (g_next_ip_octet >= 254) return 0;

    // Store client
    int slot = -1;
    for (int i = 0; i < DHCP_MAX_CLIENTS; i++) {
        if (!g_clients[i].in_use) {
            slot = i;
            break;
        }
    }

    if (slot >= 0) {
        g_clients[slot].in_use = true;
        g_clients[slot].allocated_ip = *out_ip;
        g_clients[slot].lease_time = 86400;  // 24 hours
        if (chaddr) {
            memcpy(g_clients[slot].chaddr, chaddr, 6);
        }
    }

    return 1;
}

// Add DHCP option to message
static int dhcp_add_option(DHCPMessage* msg, uint8_t tag, const uint8_t* data, uint8_t len)
{
    size_t offset;

    if (msg == NULL) return 0;
    offset = msg->options_len;
    if (offset + 2 + len > DHCP_OPTIONS_SIZE) return 0;

    msg->options[offset] = tag;
    msg->options[offset + 1] = len;
    if (len > 0 && data != NULL) {
        memcpy(msg->options + offset + 2, data, len);
    }
    msg->options_len = offset + 2 + len;

    return 1;
}

static void dhcp_finish_options(DHCPMessage* msg)
{
    if (msg->options_len < DHCP_OPTIONS_SIZE) {
        msg->options[msg->options_len] = DHCP_OPTION_END;
        msg->options_len++;
    }
}

int dhcp_server_handle_discover(const DHCPMessage* discover, DHCPMessage* offer)
{
    IpAddress offered_ip;
    uint8_t subnet_mask[] = {255, 255, 255, 0};
    uint8_t lease_time[4];
    uint8_t router_ip[4];
    uint8_t server_id[4];
    uint8_t msg_type = DHCP_OFFER;

    if (discover == NULL || offer == NULL || !g_server_ip_set) return 0;

    printf("[DHCP] Received DISCOVER from MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
           discover->chaddr[0], discover->chaddr[1], discover->chaddr[2],
           discover->chaddr[3], discover->chaddr[4], discover->chaddr[5]);

    // Allocate an IP address
    if (!dhcp_allocate_ip(&offered_ip, discover->chaddr)) return 0;

    // Build OFFER message
    memset(offer, 0, sizeof(DHCPMessage));
    offer->op = DHCP_BOOTREPLY;
    offer->htype = 1;
    offer->hlen = 6;
    offer->hops = 0;
    offer->xid = discover->xid;
    offer->yiaddr = offered_ip;
    offer->siaddr = g_server_ip;
    memcpy(offer->chaddr, discover->chaddr, 16);
    offer->magic_cookie = DHCP_MAGIC_COOKIE;
    offer->options_len = 0;

    // Add options
    write_u32_be(lease_time, 86400);
    dhcp_add_option(offer, DHCP_OPTION_MSG_TYPE, &msg_type, 1);
    memcpy(server_id, g_server_ip.octet, 4);
    dhcp_add_option(offer, DHCP_OPTION_SERVER_ID, server_id, 4);
    dhcp_add_option(offer, DHCP_OPTION_SUBNET_MASK, subnet_mask, 4);
    memcpy(router_ip, g_server_ip.octet, 4);
    router_ip[3] = 1;
    dhcp_add_option(offer, DHCP_OPTION_ROUTER, router_ip, 4);
    dhcp_add_option(offer, DHCP_OPTION_LEASE_TIME, lease_time, 4);
    dhcp_finish_options(offer);

    printf("[DHCP] Offering IP ");
    ip_to_string(&offered_ip, (char[32]){0}, 32, false);
    printf(" to client\n");

    return 1;
}

int dhcp_server_handle_request(const DHCPMessage* request, DHCPMessage* ack)
{
    uint8_t msg_type = DHCP_ACK;
    uint8_t lease_time[4];
    uint8_t subnet_mask[] = {255, 255, 255, 0};
    uint8_t server_id[4];
    uint8_t router_ip[4];
    IpAddress requested_ip;

    if (request == NULL || ack == NULL || !g_server_ip_set) return 0;

    printf("[DHCP] Received REQUEST from MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
           request->chaddr[0], request->chaddr[1], request->chaddr[2],
           request->chaddr[3], request->chaddr[4], request->chaddr[5]);

    // The yiaddr field has the requested IP (or can be in option 50)
    requested_ip = request->ciaddr;
    if (requested_ip.octet[0] == 0 && requested_ip.octet[3] == 0) {
        requested_ip = request->yiaddr;  // Fallback to yiaddr
    }

    // Build ACK
    memset(ack, 0, sizeof(DHCPMessage));
    ack->op = DHCP_BOOTREPLY;
    ack->htype = 1;
    ack->hlen = 6;
    ack->hops = 0;
    ack->xid = request->xid;
    ack->yiaddr = requested_ip;
    ack->siaddr = g_server_ip;
    memcpy(ack->chaddr, request->chaddr, 16);
    ack->magic_cookie = DHCP_MAGIC_COOKIE;
    ack->options_len = 0;

    write_u32_be(lease_time, 86400);
    dhcp_add_option(ack, DHCP_OPTION_MSG_TYPE, &msg_type, 1);
    memcpy(server_id, g_server_ip.octet, 4);
    dhcp_add_option(ack, DHCP_OPTION_SERVER_ID, server_id, 4);
    dhcp_add_option(ack, DHCP_OPTION_SUBNET_MASK, subnet_mask, 4);
    memcpy(router_ip, g_server_ip.octet, 4);
    router_ip[3] = 1;
    dhcp_add_option(ack, DHCP_OPTION_ROUTER, router_ip, 4);
    dhcp_add_option(ack, DHCP_OPTION_LEASE_TIME, lease_time, 4);
    dhcp_finish_options(ack);

    // Update client record
    for (int i = 0; i < DHCP_MAX_CLIENTS; i++) {
        if (g_clients[i].in_use &&
            memcmp(g_clients[i].chaddr, request->chaddr, 6) == 0) {
            g_clients[i].xid = request->xid;
            g_clients[i].allocated_ip = requested_ip;
            break;
        }
    }

    char ip_buf[20];
    ip_to_string(&requested_ip, ip_buf, sizeof(ip_buf), false);
    printf("[DHCP] Acknowledged IP %s to client\n", ip_buf);

    return 1;
}

int dhcp_get_lease(const uint8_t* mac, IpAddress* out_ip)
{
    if (mac == NULL || out_ip == NULL) return 0;

    for (int i = 0; i < DHCP_MAX_CLIENTS; i++) {
        if (g_clients[i].in_use && memcmp(g_clients[i].chaddr, mac, 6) == 0) {
            *out_ip = g_clients[i].allocated_ip;
            return 1;
        }
    }

    return 0;
}

int dhcp_create_discover(DHCPMessage* msg, uint32_t xid, const uint8_t* mac)
{
    if (msg == NULL || mac == NULL) return 0;

    memset(msg, 0, sizeof(DHCPMessage));
    msg->op = DHCP_BOOTREQUEST;
    msg->htype = 1;
    msg->hlen = 6;
    msg->xid = xid;
    memcpy(msg->chaddr, mac, 16);
    msg->magic_cookie = DHCP_MAGIC_COOKIE;
    msg->options_len = 0;

    uint8_t msg_type = DHCP_DISCOVER;
    uint8_t param_list[] = {1, 3, 6, 15, 51};  // Subnet, Router, DNS, Domain, Lease

    dhcp_add_option(msg, DHCP_OPTION_MSG_TYPE, &msg_type, 1);
    dhcp_add_option(msg, DHCP_OPTION_REQUEST_LIST, param_list, 5);
    dhcp_add_option(msg, DHCP_OPTION_HOSTNAME, (const uint8_t*)"magi-host", 9);
    dhcp_finish_options(msg);

    return 1;
}

int dhcp_create_request(DHCPMessage* msg, uint32_t xid, const uint8_t* mac,
                         const IpAddress* requested_ip, const IpAddress* server_ip)
{
    uint8_t req_ip[4];
    uint8_t srv_id[4];

    if (msg == NULL || mac == NULL || requested_ip == NULL || server_ip == NULL) return 0;

    memset(msg, 0, sizeof(DHCPMessage));
    msg->op = DHCP_BOOTREQUEST;
    msg->htype = 1;
    msg->hlen = 6;
    msg->xid = xid;
    msg->ciaddr = *requested_ip;
    memcpy(msg->chaddr, mac, 16);
    msg->magic_cookie = DHCP_MAGIC_COOKIE;
    msg->options_len = 0;

    uint8_t msg_type = DHCP_REQUEST;
    dhcp_add_option(msg, DHCP_OPTION_MSG_TYPE, &msg_type, 1);

    memcpy(req_ip, requested_ip->octet, 4);
    dhcp_add_option(msg, 50, req_ip, 4);  // Requested IP address

    memcpy(srv_id, server_ip->octet, 4);
    dhcp_add_option(msg, DHCP_OPTION_SERVER_ID, srv_id, 4);

    dhcp_finish_options(msg);

    return 1;
}

int dhcp_message_to_bytes(const DHCPMessage* msg, uint8_t* out, size_t out_size)
{
    size_t total_len;

    if (msg == NULL || out == NULL) return 0;

    total_len = DHCP_HEADER_SIZE + msg->options_len;
    if (out_size < total_len) return 0;

    memset(out, 0, total_len);
    out[0] = msg->op;
    out[1] = msg->htype;
    out[2] = msg->hlen;
    out[3] = msg->hops;
    write_u32_be(out + 4, msg->xid);
    write_u16_be(out + 8, msg->secs);
    write_u16_be(out + 10, msg->flags);
    memcpy(out + 12, msg->ciaddr.octet, 4);    // ciaddr
    memcpy(out + 16, msg->yiaddr.octet, 4);     // yiaddr
    memcpy(out + 20, msg->siaddr.octet, 4);     // siaddr
    memcpy(out + 24, msg->giaddr.octet, 4);     // giaddr
    memcpy(out + 28, msg->chaddr, 16);
    memcpy(out + 44, msg->sname, 64);
    memcpy(out + 108, msg->file, 128);
    write_u32_be(out + 236, DHCP_MAGIC_COOKIE);
    memcpy(out + 240, msg->options, msg->options_len);

    return (int)total_len;
}

int dhcp_message_from_bytes(DHCPMessage* msg, const uint8_t* raw, size_t raw_len)
{
    if (msg == NULL || raw == NULL || raw_len < DHCP_HEADER_SIZE) return 0;

    memset(msg, 0, sizeof(DHCPMessage));
    msg->op = raw[0];
    msg->htype = raw[1];
    msg->hlen = raw[2];
    msg->hops = raw[3];
    msg->xid = read_u32_be(raw + 4);
    msg->secs = read_u16_be(raw + 8);
    msg->flags = read_u16_be(raw + 10);
    memcpy(msg->ciaddr.octet, raw + 12, 4);
    memcpy(msg->yiaddr.octet, raw + 16, 4);
    memcpy(msg->siaddr.octet, raw + 20, 4);
    memcpy(msg->giaddr.octet, raw + 24, 4);
    memcpy(msg->chaddr, raw + 28, 16);
    memcpy(msg->sname, raw + 44, 64);
    memcpy(msg->file, raw + 108, 128);
    msg->magic_cookie = read_u32_be(raw + 236);

    msg->options_len = raw_len - DHCP_HEADER_SIZE;
    if (msg->options_len > DHCP_OPTIONS_SIZE) msg->options_len = DHCP_OPTIONS_SIZE;
    memcpy(msg->options, raw + 240, msg->options_len);

    return 1;
}
