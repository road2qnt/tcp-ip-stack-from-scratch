#include "router.h"

void router_init(Router* router, int num_interfaces)
{
    router_init_with_macs(router, num_interfaces, NULL);
}

void router_init_with_macs(Router* router, int num_interfaces, const MacAddress* mac_addresses)
{
    if (router == NULL) {
        return;
    }

    node_init_with_macs(&router->base, NODE_ROUTER, num_interfaces, mac_addresses);

    for (int i = 0; i < MAX_PORT; i++) {
        router->interface_ips[i].portNumber = i + 1;
        router->interface_ips[i].has_ip = false;
    }
}
