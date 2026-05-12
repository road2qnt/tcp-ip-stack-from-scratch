#include "host.h"

void host_init(Host* host, int num_interfaces)
{
    host_init_with_macs(host, num_interfaces, NULL);
}

void host_init_with_macs(Host* host, int num_interfaces, const MacAddress* mac_addresses)
{
    if (host == NULL) {
        return;
    }

    node_init_with_macs(&host->base, NODE_HOST, num_interfaces, mac_addresses);
    host->has_ip = false;
    host->arp_table.size = 0;
}
