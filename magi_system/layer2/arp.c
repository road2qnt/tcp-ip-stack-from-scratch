#include "arp.h"

void map_arp_init(Map_ARP *map);
int map_arp_insert(Map_ARP *map, const IpAddress *key, const MacAddress *value);
int map_arp_remove(Map_ARP *map, const IpAddress *key);
MacAddress *map_arp_get(Map_ARP *map, const IpAddress *key);
const MacAddress *map_arp_get_const(const Map_ARP *map, const IpAddress *key);