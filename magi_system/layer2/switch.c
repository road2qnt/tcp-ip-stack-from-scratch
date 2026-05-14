#include "switch.h"
#include "../core/packet.h"
#include <stdio.h>
#include <string.h>

static void print_mac_address(const MacAddress* mac)
{
    if (mac == NULL) {
        printf("??:??:??:??:??:??");
        return;
    }

    printf("%02X:%02X:%02X:%02X:%02X:%02X",
           mac->bytes[0], mac->bytes[1], mac->bytes[2],
           mac->bytes[3], mac->bytes[4], mac->bytes[5]);
}

static int switch_send_frame_on_port(Switch* sw, int out_port, int vlan_id, const EthernetFrame* frame)
{
    EthernetFrame outbound;
    uint8_t raw[ETHERNET_VLAN_HEADER_SIZE + ETHERNET_MAX_PAYLOAD];
    size_t raw_len;
    Interface* out_interface;

    if (sw == NULL || frame == NULL || out_port < 1 || out_port > sw->base.NUM_INTERFACES) {
        return 0;
    }

    if (!switch_port_allows_vlan(sw, out_port, vlan_id)) {
        return 0;
    }

    outbound = *frame;
    outbound.base.vtable = frame->base.vtable;
    outbound.base.type = frame->base.type;

    if (sw->port_configs[out_port - 1].mode == SWITCH_PORT_TRUNK) {
        if (!ethernet_set_vlan(&outbound, (uint16_t)vlan_id)) {
            return 0;
        }
    } else {
        ethernet_clear_vlan(&outbound);
    }

    raw_len = packet_to_bytes((Packet*)&outbound, raw, sizeof(raw));
    if (raw_len == 0) {
        return 0;
    }

    out_interface = node_get_interface(&sw->base, out_port);
    if (out_interface == NULL || out_interface->link == NULL) {
        return 0;
    }

    send(out_interface, raw, raw_len);
    return 1;
}

static int switch_flood_frame(Switch* sw, int in_port, int vlan_id, const EthernetFrame* frame)
{
    int sent = 0;

    if (sw == NULL || frame == NULL) {
        return 0;
    }

    printf("[%s] Flooding frame on VLAN %d\n", sw->base.NAME, vlan_id);

    for (int port = 1; port <= sw->base.NUM_INTERFACES; port++) {
        if (port == in_port) {
            continue;
        }

        if (switch_send_frame_on_port(sw, port, vlan_id, frame)) {
            sent++;
        }
    }

    return sent;
}

static void switch_receive(Node* self, Interface* in_interface, const uint8_t* raw, size_t raw_len)
{
    Switch* sw = (Switch*)self;
    EthernetFrame frame;
    int in_port;
    int vlan_id;
    int* out_port;

    if (sw == NULL || in_interface == NULL || raw == NULL) {
        return;
    }

    ethernet_init(&frame);
    if (!packet_from_bytes((Packet*)&frame, raw, raw_len)) {
        printf("[%s] Dropped invalid Ethernet frame\n", sw->base.NAME);
        return;
    }

    in_port = in_interface->portNumber;
    if (!switch_determine_ingress_vlan(sw, in_port, &frame, &vlan_id)) {
        printf("[%s] Dropped frame on port %d due to VLAN policy\n", sw->base.NAME, in_port);
        return;
    }

    switch_mac_table_set(&sw->mac_table, vlan_id, &frame.src_mac, in_port);

    if (ethernet_is_broadcast(&frame.dst_mac)) {
        switch_flood_frame(sw, in_port, vlan_id, &frame);
        return;
    }

    out_port = switch_mac_table_get(&sw->mac_table, vlan_id, &frame.dst_mac);
    if (out_port == NULL) {
        printf("[%s] Unknown destination ", sw->base.NAME);
        print_mac_address(&frame.dst_mac);
        printf(", flooding on VLAN %d\n", vlan_id);
        switch_flood_frame(sw, in_port, vlan_id, &frame);
        return;
    }

    if (*out_port == in_port) {
        return;
    }

    printf("[%s] Forwarding frame on VLAN %d to port %d\n", sw->base.NAME, vlan_id, *out_port);
    switch_send_frame_on_port(sw, *out_port, vlan_id, &frame);
}

static const NodeVTable SWITCH_VTABLE = {
    .receive = switch_receive
};

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
    node_set_vtable(&sw->base, &SWITCH_VTABLE);
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
