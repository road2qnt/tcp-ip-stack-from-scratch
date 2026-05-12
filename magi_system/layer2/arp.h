// Definisi ARP Table
// ARP: Isi nya IP -> Mac
#ifndef ARP_H
#define ARP_H

#include "../core/mac.h"
#include "../dataStructure/map.h"
#include "../layer3/ipv4.h"

// Map (ARP Table)
typedef struct Map_ARP {
    size_t size;
    IpAddress keys[MAP_MAX_ENTRIES]; // IP Address
    MacAddress values[MAP_MAX_ENTRIES]; // Mac Address
} Map_ARP;

void map_arp_init(Map_ARP *map);
int map_arp_insert(Map_ARP *map, const IpAddress *key, const MacAddress *value);
int map_arp_remove(Map_ARP *map, const IpAddress *key);
MacAddress *map_arp_get(Map_ARP *map, const IpAddress *key);
const MacAddress *map_arp_get_const(const Map_ARP *map, const IpAddress *key);

#endif
