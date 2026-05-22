#include "dns_server.h"
#include "../layer2/host.h"
#include "../core/packet.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static DNSEntry g_dns_table[DNS_MAX_ENTRIES];
static int g_dns_entry_count = 0;
static Host* g_bound_host = NULL;

int dns_server_init(void)
{
    memset(g_dns_table, 0, sizeof(g_dns_table));
    g_dns_entry_count = 0;

    // Add default entries
    uint8_t ip1_octets[] = {10, 0, 0, 5};
    uint8_t ip2_octets[] = {192, 168, 1, 10};
    uint8_t ip3_octets[] = {10, 0, 0, 1};
    IpAddress dns_ip1 = ip_init(ip1_octets, 0);
    IpAddress dns_ip2 = ip_init(ip2_octets, 0);
    IpAddress dns_ip3 = ip_init(ip3_octets, 0);

    dns_server_add_entry("www.magi.com", &dns_ip1);
    dns_server_add_entry("magi.com", &dns_ip1);
    dns_server_add_entry("www.internet.com", &dns_ip1);
    dns_server_add_entry("server.magi.com", &dns_ip2);
    dns_server_add_entry("router.magi.com", &dns_ip3);
    dns_server_add_entry("localhost", &dns_ip2);

    printf("[DNS] Initialized with %d entries\n", g_dns_entry_count);
    return 1;
}

int dns_server_add_entry(const char* hostname, const IpAddress* ip)
{
    if (hostname == NULL || ip == NULL || g_dns_entry_count >= DNS_MAX_ENTRIES) return 0;

    // Check if already exists
    for (int i = 0; i < g_dns_entry_count; i++) {
        if (strcmp(g_dns_table[i].hostname, hostname) == 0) {
            g_dns_table[i].ip = *ip;
            return 1;
        }
    }

    strncpy(g_dns_table[g_dns_entry_count].hostname, hostname, DNS_MAX_NAME - 1);
    g_dns_table[g_dns_entry_count].ip = *ip;
    g_dns_entry_count++;

    {
        char ip_buf[32];
        ip_to_string(ip, ip_buf, sizeof(ip_buf), false);
        printf("[DNS] Added entry: %s -> %s\n", hostname, ip_buf);
    }

    return 1;
}

int dns_server_resolve(const char* hostname, IpAddress* out_ip)
{
    if (hostname == NULL || out_ip == NULL) return 0;

    // Strip http:// prefix if present
    const char* h = hostname;
    if (strncmp(h, "http://", 7) == 0) h += 7;
    // Strip trailing / if present
    char clean[DNS_MAX_NAME];
    strncpy(clean, h, DNS_MAX_NAME - 1);
    clean[DNS_MAX_NAME - 1] = '\0';
    char* slash = strchr(clean, '/');
    if (slash) *slash = '\0';

    for (int i = 0; i < g_dns_entry_count; i++) {
        if (strcmp(g_dns_table[i].hostname, clean) == 0) {
            *out_ip = g_dns_table[i].ip;
            return 1;
        }
    }

    return 0;
}

// Encode a domain name into DNS format (e.g., "www.magi.com" -> \x03www\x04magi\x03com\x00)
static size_t dns_encode_name(uint8_t* out, size_t out_size, const char* name)
{
    size_t written = 0;
    const char* p = name;
    const char* dot;

    if (out == NULL || name == NULL) return 0;

    while (p && *p) {
        dot = strchr(p, '.');
        size_t label_len;
        if (dot) {
            label_len = (size_t)(dot - p);
        } else {
            label_len = strlen(p);
        }

        if (written + 1 + label_len > out_size) return 0;

        out[written++] = (uint8_t)label_len;
        memcpy(out + written, p, label_len);
        written += label_len;

        p = dot ? dot + 1 : NULL;
    }

    if (written + 1 > out_size) return 0;
    out[written++] = 0;  // Root label

    return written;
}

// Decode DNS-encoded name
// Returns number of source bytes consumed (0 on error).
// Decoded name is written to 'out'.
static size_t dns_decode_name(const uint8_t* raw, size_t max_len, char* out, size_t out_size)
{
    size_t start_pos = 0;
    size_t src_pos = 0;
    size_t dst_pos = 0;
    int jumped = 0;

    if (raw == NULL || out == NULL) return 0;

    while (src_pos < max_len) {
        uint8_t label_len = raw[src_pos];

        // Check for compression pointer (top 2 bits = 11)
        if ((label_len & 0xC0) == 0xC0) {
            if (src_pos + 2 > max_len) return 0;
            if (!jumped) {
                src_pos += 2;  // Skip past the pointer in source
            }
            src_pos = (size_t)(((label_len & 0x3F) << 8) | raw[src_pos + 1]);
            if (src_pos >= max_len) return 0;
            jumped = 1;
            continue;
        }

        src_pos++;
        if (label_len == 0) break;  // Root label

        if (dst_pos > 0 && dst_pos < out_size - 1) {
            out[dst_pos++] = '.';
        }

        if (src_pos + label_len > max_len) return 0;
        if (dst_pos + label_len >= out_size) return 0;

        memcpy(out + dst_pos, raw + src_pos, label_len);
        dst_pos += label_len;
        src_pos += label_len;
    }

    out[dst_pos] = '\0';

    // Return source bytes consumed:
    // - Uncompressed: src_pos (includes root label byte)
    // - Compressed: start_pos + 2 (just the pointer itself)
    if (jumped) return start_pos + 2;
    return src_pos;
}

static void write_u16_be(uint8_t* out, uint16_t value)
{
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)(value & 0xFF);
}

static void write_u32_be(uint8_t* out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)(value & 0xFF);
}

static uint16_t read_u16_be(const uint8_t* raw)
{
    return (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
}

int dns_create_query(uint8_t* out, size_t out_size, size_t* query_len,
                      uint16_t id, const char* hostname)
{
    size_t pos = 0;
    size_t enc_len;
    uint8_t encoded_name[DNS_MAX_NAME];

    if (out == NULL || query_len == NULL || hostname == NULL) return 0;

    enc_len = dns_encode_name(encoded_name, sizeof(encoded_name), hostname);
    if (enc_len == 0) return 0;

    // DNS Header (12 bytes)
    if (pos + 12 > out_size) return 0;
    write_u16_be(out + pos, id);       pos += 2;  // ID
    write_u16_be(out + pos, DNS_QR_QUERY | DNS_OPCODE_QUERY | DNS_RD_FLAG); pos += 2;  // Flags
    write_u16_be(out + pos, 1);        pos += 2;  // QDCOUNT = 1
    write_u16_be(out + pos, 0);        pos += 2;  // ANCOUNT = 0
    write_u16_be(out + pos, 0);        pos += 2;  // NSCOUNT = 0
    write_u16_be(out + pos, 0);        pos += 2;  // ARCOUNT = 0

    // Question
    if (pos + enc_len + 4 > out_size) return 0;
    memcpy(out + pos, encoded_name, enc_len); pos += enc_len;
    write_u16_be(out + pos, DNS_TYPE_A);       pos += 2;  // QTYPE = A
    write_u16_be(out + pos, DNS_CLASS_IN);     pos += 2;  // QCLASS = IN

    *query_len = pos;
    return 1;
}

int dns_parse_response(const uint8_t* raw, size_t raw_len, IpAddress* out_ip)
{
    DNSHeader header;
    uint16_t ancount;
    size_t pos = 12;  // After header
    char name[DNS_MAX_NAME];

    if (raw == NULL || out_ip == NULL || raw_len < 12) return 0;

    header.id = read_u16_be(raw + 0);
    header.flags = read_u16_be(raw + 2);
    header.qdcount = read_u16_be(raw + 4);
    header.ancount = read_u16_be(raw + 6);

    // Check response code
    if (header.flags & 0x000F) return 0;  // Non-zero rcode = error

    ancount = header.ancount;

    // Skip question section
    for (int i = 0; i < header.qdcount && pos < raw_len; i++) {
        size_t name_consumed = dns_decode_name(raw + pos, raw_len - pos, name, sizeof(name));
        if (name_consumed == 0) return 0;
        pos += name_consumed;
        pos += 4;  // QTYPE (2) + QCLASS (2)
    }

    // Parse answer section
    for (int i = 0; i < ancount && pos < raw_len; i++) {
        // Name (could be pointer)
        if ((raw[pos] & 0xC0) == 0xC0) {
            pos += 2;  // Compression pointer
        } else {
            size_t name_consumed = dns_decode_name(raw + pos, raw_len - pos, name, sizeof(name));
            if (name_consumed == 0) return 0;
            pos += name_consumed;
        }

        if (pos + 10 > raw_len) return 0;
        uint16_t rtype = read_u16_be(raw + pos); pos += 2;
        pos += 2; // skip CLASS
        uint32_t ttl_val = (uint32_t)read_u16_be(raw + pos) << 16 | read_u16_be(raw + pos + 2); (void)ttl_val; pos += 4;
        uint16_t rdlength = read_u16_be(raw + pos); pos += 2;

        if (rtype == DNS_TYPE_A && rdlength == 4 && pos + 4 <= raw_len) {
            out_ip->octet[0] = raw[pos];
            out_ip->octet[1] = raw[pos + 1];
            out_ip->octet[2] = raw[pos + 2];
            out_ip->octet[3] = raw[pos + 3];
            out_ip->prefix = 0;
            return 1;
        }

        pos += rdlength;
    }

    return 0;
}

int dns_server_handle_query(const uint8_t* query, size_t query_len,
                             uint8_t* response, size_t* resp_len, size_t max_resp_len)
{
    DNSHeader req_header;
    char qname[DNS_MAX_NAME];
    size_t pos = 12;
    size_t resp_pos = 0;
    int found = 0;
    IpAddress resolved;
    size_t enc_name_len;
    uint8_t enc_name[DNS_MAX_NAME];

    if (query == NULL || response == NULL || resp_len == NULL || query_len < 12) return 0;

    // Parse header
    req_header.id = read_u16_be(query + 0);
    req_header.flags = read_u16_be(query + 2);
    req_header.qdcount = read_u16_be(query + 4);

    // Decode question name
    size_t decoded = dns_decode_name(query + pos, query_len - pos, qname, sizeof(qname));
    if (decoded == 0) return 0;

    printf("[DNS] Query for: %s\n", qname);

    // Lookup in table
    found = dns_server_resolve(qname, &resolved);

    // Encode name for response
    enc_name_len = dns_encode_name(enc_name, sizeof(enc_name), qname);
    if (enc_name_len == 0) return 0;

    // Build response
    resp_pos = 0;

    // Header
    if (resp_pos + 12 > max_resp_len) return 0;
    write_u16_be(response + resp_pos, req_header.id); resp_pos += 2;  // Same ID
    uint16_t flags = DNS_QR_RESPONSE | DNS_OPCODE_QUERY | DNS_AA_FLAG | DNS_RD_FLAG | DNS_RA_FLAG;
    if (!found) flags |= DNS_RCODE_NXDOMAIN;
    write_u16_be(response + resp_pos, flags); resp_pos += 2;
    write_u16_be(response + resp_pos, 1); resp_pos += 2;  // QDCOUNT = 1
    write_u16_be(response + resp_pos, found ? 1 : 0); resp_pos += 2;  // ANCOUNT
    write_u16_be(response + resp_pos, 0); resp_pos += 2;  // NSCOUNT
    write_u16_be(response + resp_pos, 0); resp_pos += 2;  // ARCOUNT

    // Question section (echo back)
    if (resp_pos + enc_name_len + 4 > max_resp_len) return 0;
    memcpy(response + resp_pos, enc_name, enc_name_len); resp_pos += enc_name_len;
    write_u16_be(response + resp_pos, DNS_TYPE_A); resp_pos += 2;
    write_u16_be(response + resp_pos, DNS_CLASS_IN); resp_pos += 2;

    if (found) {
        // Answer section
        // Name as pointer to question (0xC00C = pointer to offset 12)
        if (resp_pos + 2 > max_resp_len) return 0;
        write_u16_be(response + resp_pos, 0xC00C); resp_pos += 2;

        if (resp_pos + 8 + 4 > max_resp_len) return 0;
        write_u16_be(response + resp_pos, DNS_TYPE_A); resp_pos += 2;  // TYPE
        write_u16_be(response + resp_pos, DNS_CLASS_IN); resp_pos += 2;  // CLASS
        write_u32_be(response + resp_pos, 300); resp_pos += 4;  // TTL = 300s
        write_u16_be(response + resp_pos, 4); resp_pos += 2;  // RDLENGTH = 4
        response[resp_pos] = resolved.octet[0]; resp_pos++;
        response[resp_pos] = resolved.octet[1]; resp_pos++;
        response[resp_pos] = resolved.octet[2]; resp_pos++;
        response[resp_pos] = resolved.octet[3]; resp_pos++;

        char ip_buf[20];
        ip_to_string(&resolved, ip_buf, sizeof(ip_buf), false);
        printf("[DNS] Resolved %s -> %s\n", qname, ip_buf);
    }

    *resp_len = resp_pos;
    return 1;
}

int dns_server_attach_host(Host* host)
{
    if (host == NULL) return 0;
    g_bound_host = host;
    if (g_dns_entry_count == 0) dns_server_init();
    return 1;
}

int dns_server_detach_host(void)
{
    g_bound_host = NULL;
    return 1;
}

Host* dns_server_get_bound_host(void)
{
    return g_bound_host;
}

static int dns_send_response(Host* host,
                              const uint8_t* response, size_t response_len,
                              const IpAddress* dst_ip, uint16_t dst_port)
{
    if (host == NULL || response == NULL || dst_ip == NULL) return 0;

    UDPDatagram udp;
    udp_init(&udp);
    udp_create(&udp, DNS_SERVER_PORT, dst_port, response, response_len);

    uint8_t udp_raw[UDP_HEADER_SIZE + UDP_MAX_PAYLOAD];
    size_t udp_len = packet_to_bytes((Packet*)&udp, udp_raw, sizeof(udp_raw));
    if (udp_len == 0) return 0;
    udp.checksum = udp_compute_checksum(udp_raw, udp_len, &host->ip_address, dst_ip);
    udp_len = packet_to_bytes((Packet*)&udp, udp_raw, sizeof(udp_raw));

    uint8_t ip_raw[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
    IPv4Packet ip_pkt;
    if (!ipv4_create(&ip_pkt, host->ip_address, *dst_ip,
                     IPV4_DEFAULT_TTL, IPV4_PROTOCOL_UDP, udp_raw, udp_len)) {
        return 0;
    }
    size_t ip_len = packet_to_bytes((Packet*)&ip_pkt, ip_raw, sizeof(ip_raw));
    if (ip_len == 0) return 0;

    return host_send_l3_packet(host, dst_ip, ETHERNET_TYPE_IPV4, ip_raw, ip_len);
}

int dns_server_dispatch(Host* host, const UDPDatagram* request,
                         const IpAddress* src_ip, uint16_t src_port)
{
    if (host == NULL || request == NULL || src_ip == NULL) return 0;
    if (g_bound_host != host) return 0;

    uint8_t response[1024];
    size_t resp_len = 0;
    if (!dns_server_handle_query(request->payload, request->payload_len,
                                  response, &resp_len, sizeof(response))) {
        return 0;
    }

    return dns_send_response(host, response, resp_len, src_ip, src_port);
}
