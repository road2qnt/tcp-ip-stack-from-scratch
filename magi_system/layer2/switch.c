#include "switch.h"

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
    map_mac_init(&sw->mac_table);
}
