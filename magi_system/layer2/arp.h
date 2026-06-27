// Definisi ARP Table
// ARP: Isi nya IP -> Mac
#ifndef ARP_H
#define ARP_H

#include "../core/mac.h"
#include "../core/packet.h"
#include "../layer3/ipv4.h"

#include <stddef.h>
#include <stdint.h>

#define ARP_TABLE_MAX_ENTRIES 100

typedef enum ArpOpcode {
    ARP_OPCODE_REQUEST = 1,
    ARP_OPCODE_REPLY = 2
} ArpOpcode;

typedef struct ArpTableEntry {
    IpAddress ip;
    MacAddress mac;
} ArpTableEntry;

// Map (ARP Table)
typedef struct ArpTable {
    size_t size;
    ArpTableEntry entries[ARP_TABLE_MAX_ENTRIES];
} ArpTable;

// ARP Message (Packet)
typedef struct ARPMessage {
    Packet base;
    /*
    [!] Isi ARP yang antara dimasukkan/tidak: 
    Hardware Type, Protocol Type, Hardware Length, Protocol Length 
    */
    ArpOpcode opcode;
    MacAddress sender_mac;
    IpAddress sender_ip;
    MacAddress target_mac;
    IpAddress target_ip;
} ARPMessage;

void arp_table_init(ArpTable* table);
int arp_table_set(ArpTable* table, const IpAddress* ip, const MacAddress* mac);
int arp_table_remove(ArpTable* table, const IpAddress* ip);
MacAddress* arp_table_get(ArpTable* table, const IpAddress* ip);
const MacAddress* arp_table_get_const(const ArpTable* table, const IpAddress* ip);

void arp_message_init(ARPMessage* message);
void arp_message_init_request(ARPMessage* message,MacAddress sender_mac,IpAddress sender_ip,IpAddress target_ip);
void arp_message_init_reply(ARPMessage* message,MacAddress sender_mac,IpAddress sender_ip,MacAddress target_mac,IpAddress target_ip);

#endif
