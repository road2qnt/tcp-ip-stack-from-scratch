#ifndef HOST_H
#define HOST_H

#include "../core/interface.h"
#include "../layer3/ipv4.h"
#include "arp.h"

// Definisi Host (inheritance dari node)
typedef struct Host{
    Node base;
    IpAddress ip_address;
    IpAddress default_gateway;
    bool has_ip;
    // Tambahan untuk Host (ARP Table (IP -> MAC))
    Map_ARP arp_table;
}Host;

void host_init(Host* host, int num_interfaces);
void host_init_with_macs(Host* host, int num_interfaces, const MacAddress* mac_addresses);
// ARP Dapet dari switch (packet)

#endif
