#include "arp.h"

#include <string.h>

#define ARP_SERIALIZED_SIZE 28
#define ARP_HARDWARE_ETHERNET 1
#define ARP_PROTOCOL_IPV4 0x0800
#define ARP_HARDWARE_LEN 6
#define ARP_PROTOCOL_LEN 4

static uint16_t read_u16_be(const uint8_t* raw)
{
    return (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
}

static void write_u16_be(uint8_t* out, uint16_t value)
{
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)(value & 0xFF);
}

static int ip_octets_equal(const IpAddress* a, const IpAddress* b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }

    return memcmp(a->octet, b->octet, 4) == 0;
}

static size_t arp_message_to_bytes(const Packet* self, uint8_t* out, size_t out_size)
{
    const ARPMessage* message = (const ARPMessage*)self;
    size_t offset = 0;

    if (message == NULL || out == NULL || out_size < ARP_SERIALIZED_SIZE) {
        return 0;
    }

    write_u16_be(out + offset, ARP_HARDWARE_ETHERNET);
    offset += 2;
    write_u16_be(out + offset, ARP_PROTOCOL_IPV4);
    offset += 2;
    out[offset++] = ARP_HARDWARE_LEN;
    out[offset++] = ARP_PROTOCOL_LEN;
    write_u16_be(out + offset, (uint16_t)message->opcode);
    offset += 2;
    memcpy(out + offset, message->sender_mac.bytes, 6);
    offset += 6;
    memcpy(out + offset, message->sender_ip.octet, 4);
    offset += 4;
    memcpy(out + offset, message->target_mac.bytes, 6);
    offset += 6;
    memcpy(out + offset, message->target_ip.octet, 4);
    offset += 4;

    return offset;
}

static int arp_message_from_bytes(Packet* self, const uint8_t* raw, size_t raw_len)
{
    ARPMessage* message = (ARPMessage*)self;
    size_t offset = 0;
    uint16_t hardware_type;
    uint16_t protocol_type;
    uint16_t opcode;

    if (message == NULL || raw == NULL || raw_len < ARP_SERIALIZED_SIZE) {
        return 0;
    }

    hardware_type = read_u16_be(raw + offset);
    offset += 2;
    protocol_type = read_u16_be(raw + offset);
    offset += 2;

    if (hardware_type != ARP_HARDWARE_ETHERNET ||
        protocol_type != ARP_PROTOCOL_IPV4 ||
        raw[offset] != ARP_HARDWARE_LEN ||
        raw[offset + 1] != ARP_PROTOCOL_LEN) {
        return 0;
    }

    offset += 2;
    opcode = read_u16_be(raw + offset);
    offset += 2;
    if (opcode != ARP_OPCODE_REQUEST && opcode != ARP_OPCODE_REPLY) {
        return 0;
    }

    message->opcode = (ArpOpcode)opcode;
    memcpy(message->sender_mac.bytes, raw + offset, 6);
    offset += 6;
    memcpy(message->sender_ip.octet, raw + offset, 4);
    message->sender_ip.prefix = 0;
    offset += 4;
    memcpy(message->target_mac.bytes, raw + offset, 6);
    offset += 6;
    memcpy(message->target_ip.octet, raw + offset, 4);
    message->target_ip.prefix = 0;

    return 1;
}

static const PacketVTable ARP_MESSAGE_VTABLE = {
    .to_bytes = arp_message_to_bytes,
    .from_bytes = arp_message_from_bytes
};

static int arp_table_find_index(const ArpTable* table, const IpAddress* ip)
{
    if (table == NULL || ip == NULL) {
        return -1;
    }

    for (size_t i = 0; i < table->size; i++) {
        if (ip_octets_equal(&table->entries[i].ip, ip)) {
            return (int)i;
        }
    }

    return -1;
}

void arp_table_init(ArpTable* table)
{
    if (table == NULL) {
        return;
    }

    table->size = 0;
}

int arp_table_set(ArpTable* table, const IpAddress* ip, const MacAddress* mac)
{
    int index;

    if (table == NULL || ip == NULL || mac == NULL) {
        return 0;
    }

    index = arp_table_find_index(table, ip);
    if (index >= 0) {
        table->entries[index].mac = *mac;
        return 1;
    }

    if (table->size >= ARP_TABLE_MAX_ENTRIES) {
        return 0;
    }

    table->entries[table->size].ip = *ip;
    table->entries[table->size].mac = *mac;
    table->size++;

    return 1;
}

int arp_table_remove(ArpTable* table, const IpAddress* ip)
{
    int index;

    if (table == NULL || ip == NULL) {
        return 0;
    }

    index = arp_table_find_index(table, ip);
    if (index < 0) {
        return 0;
    }

    for (size_t i = (size_t)index; i + 1 < table->size; i++) {
        table->entries[i] = table->entries[i + 1];
    }
    table->size--;

    return 1;
}

MacAddress* arp_table_get(ArpTable* table, const IpAddress* ip)
{
    int index = arp_table_find_index(table, ip);

    if (index < 0) {
        return NULL;
    }

    return &table->entries[index].mac;
}

const MacAddress* arp_table_get_const(const ArpTable* table, const IpAddress* ip)
{
    int index = arp_table_find_index(table, ip);

    if (index < 0) {
        return NULL;
    }

    return &table->entries[index].mac;
}

void arp_message_init(ARPMessage* message)
{
    if (message == NULL) {
        return;
    }

    message->base.vtable = &ARP_MESSAGE_VTABLE;
    message->base.type = ARP;
    message->opcode = ARP_OPCODE_REQUEST;
}

void arp_message_init_request(
    ARPMessage* message,
    MacAddress sender_mac,
    IpAddress sender_ip,
    IpAddress target_ip
)
{
    MacAddress empty_mac = {0};

    arp_message_init(message);
    if (message == NULL) {
        return;
    }

    message->opcode = ARP_OPCODE_REQUEST;
    message->sender_mac = sender_mac;
    message->sender_ip = sender_ip;
    message->target_mac = empty_mac;
    message->target_ip = target_ip;
}

void arp_message_init_reply(
    ARPMessage* message,
    MacAddress sender_mac,
    IpAddress sender_ip,
    MacAddress target_mac,
    IpAddress target_ip
)
{
    arp_message_init(message);
    if (message == NULL) {
        return;
    }

    message->opcode = ARP_OPCODE_REPLY;
    message->sender_mac = sender_mac;
    message->sender_ip = sender_ip;
    message->target_mac = target_mac;
    message->target_ip = target_ip;
}
