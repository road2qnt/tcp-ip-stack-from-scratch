#include "switch.h"
#include <string.h>

static int switch_mac_table_find_index(const SwitchMacTable* table, int vlan_id, const MacAddress* mac)
{
    if (table == NULL || mac == NULL) {
        return -1;
    }

    for (size_t i = 0; i < table->size; i++) {
        if (table->entries[i].vlan_id == vlan_id &&
            mac_equal((MacAddress*)&table->entries[i].mac, (MacAddress*)mac)) {
            return (int)i;
        }
    }

    return -1;
}

void switch_init(Switch* sw, int num_interfaces)
{
    switch_init_with_macs(sw, num_interfaces, NULL);
}

void switch_init_with_macs(Switch* sw, int num_interfaces, const MacAddress* mac_addresses)
{
    if (sw == NULL) {
        return;
    }

    node_init_with_macs(&sw->base, NODE_SWITCH, num_interfaces, mac_addresses);
    switch_init_port_configs(sw, SWITCH_DEFAULT_VLAN_ID);
    switch_mac_table_init(&sw->mac_table);
}

void switch_mac_table_init(SwitchMacTable* table)
{
    if (table == NULL) {
        return;
    }

    table->size = 0;
}

int switch_mac_table_set(SwitchMacTable* table, int vlan_id, const MacAddress* mac, int port_number)
{
    int index;

    if (table == NULL || mac == NULL || vlan_id < 1 || port_number < 1) {
        return 0;
    }

    index = switch_mac_table_find_index(table, vlan_id, mac);
    if (index >= 0) {
        table->entries[index].port_number = port_number;
        return 1;
    }

    if (table->size >= SWITCH_MAC_TABLE_MAX_ENTRIES) {
        return 0;
    }

    table->entries[table->size].vlan_id = vlan_id;
    table->entries[table->size].mac = *mac;
    table->entries[table->size].port_number = port_number;
    table->size++;

    return 1;
}

int switch_mac_table_remove(SwitchMacTable* table, int vlan_id, const MacAddress* mac)
{
    int index;

    if (table == NULL || mac == NULL) {
        return 0;
    }

    index = switch_mac_table_find_index(table, vlan_id, mac);
    if (index < 0) {
        return 0;
    }

    for (size_t i = (size_t)index; i + 1 < table->size; i++) {
        table->entries[i] = table->entries[i + 1];
    }
    table->size--;

    return 1;
}

int* switch_mac_table_get(SwitchMacTable* table, int vlan_id, const MacAddress* mac)
{
    int index = switch_mac_table_find_index(table, vlan_id, mac);

    if (index < 0) {
        return NULL;
    }

    return &table->entries[index].port_number;
}

const int* switch_mac_table_get_const(const SwitchMacTable* table, int vlan_id, const MacAddress* mac)
{
    int index = switch_mac_table_find_index(table, vlan_id, mac);

    if (index < 0) {
        return NULL;
    }

    return &table->entries[index].port_number;
}

void switch_init_port_configs(Switch* sw, int default_vlan_id)
{
    if (sw == NULL) {
        return;
    }

    if (default_vlan_id < 1) {
        default_vlan_id = SWITCH_DEFAULT_VLAN_ID;
    }

    for (int i = 0; i < MAX_PORT; i++) {
        sw->port_configs[i].mode = SWITCH_PORT_ACCESS;
        sw->port_configs[i].vlan_id = default_vlan_id;
    }
}

int switch_set_access_port(Switch* sw, int port_number, int vlan_id)
{
    if (sw == NULL || port_number < 1 || port_number > sw->base.NUM_INTERFACES || vlan_id < 1) {
        return 0;
    }

    sw->port_configs[port_number - 1].mode = SWITCH_PORT_ACCESS;
    sw->port_configs[port_number - 1].vlan_id = vlan_id;
    return 1;
}

int switch_set_trunk_port(Switch* sw, int port_number)
{
    if (sw == NULL || port_number < 1 || port_number > sw->base.NUM_INTERFACES) {
        return 0;
    }

    sw->port_configs[port_number - 1].mode = SWITCH_PORT_TRUNK;
    sw->port_configs[port_number - 1].vlan_id = 0;
    return 1;
}

bool switch_port_allows_vlan(const Switch* sw, int port_number, int vlan_id)
{
    const SwitchPortConfig* config;

    if (sw == NULL || port_number < 1 || port_number > sw->base.NUM_INTERFACES || vlan_id < 1) {
        return false;
    }

    config = &sw->port_configs[port_number - 1];
    if (config->mode == SWITCH_PORT_TRUNK) {
        return true;
    }

    return config->vlan_id == vlan_id;
}

int switch_determine_ingress_vlan(const Switch* sw, int in_port, const EthernetFrame* frame, int* out_vlan_id)
{
    const SwitchPortConfig* config;

    if (sw == NULL || frame == NULL || out_vlan_id == NULL || in_port < 1 || in_port > sw->base.NUM_INTERFACES) {
        return 0;
    }

    config = &sw->port_configs[in_port - 1];
    if (config->mode == SWITCH_PORT_ACCESS) {
        if (frame->has_vlan) {
            return 0;
        }
        *out_vlan_id = config->vlan_id;
        return *out_vlan_id >= 1;
    }

    if (!frame->has_vlan || frame->vlan_id < 1) {
        return 0;
    }

    *out_vlan_id = (int)frame->vlan_id;
    return 1;
}

int switch_learn_from_frame(Switch* sw, int in_port, const EthernetFrame* frame)
{
    int vlan_id;

    if (sw == NULL || frame == NULL) {
        return 0;
    }

    if (!switch_determine_ingress_vlan(sw, in_port, frame, &vlan_id)) {
        return 0;
    }

    return switch_mac_table_set(&sw->mac_table, vlan_id, &frame->src_mac, in_port);
}
