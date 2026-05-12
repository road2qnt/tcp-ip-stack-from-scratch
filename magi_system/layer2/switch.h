#ifndef SWITCH_H
#define SWITCH_H

#include "../core/interface.h"
#include "../dataStructure/map.h"

typedef struct Switch{
    Node base;
    // Tambahan untuk Switch (MAC Table (MAC -> Port (Interface)))
    Map_MAC mac_table;
}Switch;

void switch_init(Switch* sw, int num_interfaces);
void switch_init_with_macs(Switch* sw, int num_interfaces, const MacAddress* mac_addresses);
#endif
