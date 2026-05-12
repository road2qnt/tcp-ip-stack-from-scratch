#ifndef ROUTER_H
#define ROUTER_H

#include "../core/interface.h"
#include "ipv4.h"

typedef struct RouterInterfaceAddress {
    int portNumber;
    IpAddress ip_address;
    bool has_ip;
} RouterInterfaceAddress;

typedef struct Router{
    Node base;
    RouterInterfaceAddress interface_ips[MAX_PORT];
}Router;

void router_init(Router* router, int num_interfaces);
void router_init_with_macs(Router* router, int num_interfaces, const MacAddress* mac_addresses);

#endif
