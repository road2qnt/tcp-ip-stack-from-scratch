#ifndef SWITCH_H
#define SWITCH_H

#include "../core/interface.h"
#include "ethernet.h"

#include <stdbool.h>
#include <stddef.h>

#define SWITCH_DEFAULT_VLAN_ID 1
#define SWITCH_MAC_TABLE_MAX_ENTRIES 256

typedef enum SwitchPortMode {
    SWITCH_PORT_ACCESS,
    SWITCH_PORT_TRUNK
} SwitchPortMode;

typedef struct SwitchPortConfig {
    SwitchPortMode mode;
    int vlan_id;
} SwitchPortConfig;

typedef struct SwitchMacEntry {
    int vlan_id;
    MacAddress mac;
    int port_number;
} SwitchMacEntry;

typedef struct SwitchMacTable {
    SwitchMacEntry entries[SWITCH_MAC_TABLE_MAX_ENTRIES];
    size_t size;
} SwitchMacTable;

typedef struct Switch{
    Node base;
    SwitchPortConfig port_configs[MAX_PORT];
    SwitchMacTable mac_table;
}Switch;

void switch_init(Switch* sw, int num_interfaces);
void switch_init_with_macs(Switch* sw, int num_interfaces, const MacAddress* mac_addresses);

void switch_mac_table_init(SwitchMacTable* table);
int switch_mac_table_set(SwitchMacTable* table, int vlan_id, const MacAddress* mac, int port_number);
int switch_mac_table_remove(SwitchMacTable* table, int vlan_id, const MacAddress* mac);
int* switch_mac_table_get(SwitchMacTable* table, int vlan_id, const MacAddress* mac);
const int* switch_mac_table_get_const(const SwitchMacTable* table, int vlan_id, const MacAddress* mac);

void switch_init_port_configs(Switch* sw, int default_vlan_id);
int switch_set_access_port(Switch* sw, int port_number, int vlan_id);
int switch_set_trunk_port(Switch* sw, int port_number);
bool switch_port_allows_vlan(const Switch* sw, int port_number, int vlan_id);
int switch_determine_ingress_vlan(const Switch* sw, int in_port, const EthernetFrame* frame, int* out_vlan_id);
int switch_learn_from_frame(Switch* sw, int in_port, const EthernetFrame* frame);

#endif
