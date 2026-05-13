#include "arp.h"

static int arp_table_find_index(const ArpTable* table, const IpAddress* ip)
{
    if (table == NULL || ip == NULL) {
        return -1;
    }

    for (size_t i = 0; i < table->size; i++) {
        if (ip_equal((IpAddress*)&table->entries[i].ip, (IpAddress*)ip)) {
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

    message->base.vtable = NULL;
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
