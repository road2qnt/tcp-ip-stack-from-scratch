#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../layer3/ipv4.h"
#include "../layer4/udp.h"

typedef struct Host Host;

#define DNS_SERVER_PORT 53
#define DNS_MAX_NAME 256
#define DNS_MAX_ENTRIES 32

// DNS header flags
#define DNS_QR_QUERY    0x0000
#define DNS_QR_RESPONSE 0x8000
#define DNS_OPCODE_QUERY 0x0000
#define DNS_AA_FLAG     0x0400  // Authoritative Answer
#define DNS_RD_FLAG     0x0100  // Recursion Desired
#define DNS_RA_FLAG     0x0080  // Recursion Available

// DNS response codes
#define DNS_RCODE_NOERROR  0
#define DNS_RCODE_NXDOMAIN 3

// DNS resource record types
#define DNS_TYPE_A     1   // Host address
#define DNS_TYPE_NS    2   // Name server
#define DNS_TYPE_CNAME 5   // Canonical name
#define DNS_TYPE_AAAA 28   // IPv6 address

// DNS class
#define DNS_CLASS_IN   1   // Internet

typedef struct __attribute__((packed)) DNSHeader {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;  // Question count
    uint16_t ancount;  // Answer count
    uint16_t nscount;  // Authority count
    uint16_t arcount;  // Additional count
} DNSHeader;

typedef struct __attribute__((packed)) DNSQuestion {
    char qname[DNS_MAX_NAME];
    size_t qname_len;  // Encoded length in bytes
    uint16_t qtype;
    uint16_t qclass;
} DNSQuestion;

typedef struct __attribute__((packed)) DNSResourceRecord {
    char name[DNS_MAX_NAME];
    size_t name_len;    // Encoded length
    uint16_t type;
    uint16_t rclass;
    uint32_t ttl;
    uint16_t rdlength;
    uint8_t rdata[16];  // Enough for IPv4 address
} DNSResourceRecord;

typedef struct DNSMessage {
    DNSHeader header;
    DNSQuestion question;
    DNSResourceRecord answer;
} DNSMessage;

// DNS entry for static table
typedef struct DNSEntry {
    char hostname[DNS_MAX_NAME];
    IpAddress ip;
} DNSEntry;

// Initialize DNS server with static entries
int dns_server_init(void);

// Add a DNS entry
int dns_server_add_entry(const char* hostname, const IpAddress* ip);

// Resolve hostname to IP address
int dns_server_resolve(const char* hostname, IpAddress* out_ip);

// Handle incoming DNS query
// Returns the response message length or 0
int dns_server_handle_query(const uint8_t* query, size_t query_len,
                             uint8_t* response, size_t* resp_len, size_t max_resp_len);

// Create a DNS query for a hostname
int dns_create_query(uint8_t* out, size_t out_size, size_t* query_len,
                      uint16_t id, const char* hostname);

// Parse DNS response to extract IP
int dns_parse_response(const uint8_t* raw, size_t raw_len, IpAddress* out_ip);

int dns_server_attach_host(Host* host);
int dns_server_detach_host(void);
Host* dns_server_get_bound_host(void);
int dns_server_dispatch(Host* host, const UDPDatagram* request, const IpAddress* src_ip, uint16_t src_port);

#endif
