#include "visualizer.h"
#include "../layer2/host.h"
#include "../layer2/switch.h"
#include "../layer3/router.h"

#include <stdio.h>

static void print_mac(const MacAddress *mac)
{
    printf("%02X:%02X:%02X:%02X:%02X:%02X",
           mac->bytes[0], mac->bytes[1], mac->bytes[2],
           mac->bytes[3], mac->bytes[4], mac->bytes[5]);
}

static void print_ip(const IpAddress *ip)
{
    if (ip == NULL) {
        printf("none");
        return;
    }
    printf("%d.%d.%d.%d",
           ip->octet[0], ip->octet[1], ip->octet[2], ip->octet[3]);
    if (ip->prefix > 0) {
        printf("/%d", ip->prefix);
    }
}

static void print_host_info(const Host *host)
{
    printf("  IP Address: ");
    if (host->has_ip) {
        print_ip(&host->ip_address);
        printf("\n  Gateway:     ");
        print_ip(&host->default_gateway);
    } else {
        printf("(none)");
    }
    printf("\n  ARP Cache:   %zu entries\n", host->arp_table.size);
    printf("  Pending:     %zu packets\n", host->pending_queue.size);
}

static void print_switch_info(const Switch *sw)
{
    printf("  MAC Table:   %zu entries\n", sw->mac_table.size);
    printf("  Ports:       %d\n", sw->base.NUM_INTERFACES);

    // Print non-default port configs
    int has_special = 0;
    for (int i = 0; i < sw->base.NUM_INTERFACES; i++) {
        const SwitchPortConfig *cfg = &sw->port_configs[i];
        if (cfg->mode == SWITCH_PORT_TRUNK || cfg->vlan_id != SWITCH_DEFAULT_VLAN_ID) {
            if (!has_special) {
                printf("  VLAN Configs:\n");
                has_special = 1;
            }
            printf("    Port %d: ", i + 1);
            if (cfg->mode == SWITCH_PORT_ACCESS) {
                printf("access (VLAN %d)\n", cfg->vlan_id);
            } else {
                printf("trunk\n");
            }
        }
    }
}

static void print_router_info(const Router *rtr)
{
    printf("  Interface IPs:\n");
    for (int i = 0; i < rtr->base.NUM_INTERFACES; i++) {
        printf("    Port %d: ", rtr->interface_ips[i].portNumber);
        if (rtr->interface_ips[i].has_ip) {
            print_ip(&rtr->interface_ips[i].ip_address);
        } else {
            printf("(none)");
        }
        printf("\n");
    }
}

void simulator_print_topology(const Simulator *simulator)
{
    if (simulator == NULL) return;

    printf("========================================\n");
    printf("      MAGI SYSTEM - TOPOLOGY MAP\n");
    printf("========================================\n\n");

    if (simulator->node_count == 0) {
        printf("  (No nodes in topology)\n\n");
        return;
    }

    // --- Print nodes ---
    printf("--- Nodes (%zu total) ---\n\n", simulator->node_count);

    for (size_t i = 0; i < simulator->node_count; i++) {
        const SimulatorNodeEntry *entry = &simulator->nodes[i];
        if (entry->node == NULL) continue;

        const char *type_str = node_type_to_string(entry->node->type);
        printf("[%s] %s\n", type_str, entry->name);

        // Print interfaces
        for (int p = 0; p < entry->node->NUM_INTERFACES; p++) {
            const Interface *iface = &entry->node->interfaces[p];
            printf("  Port %d: MAC=", p + 1);
            print_mac(&iface->Mac_Address);
            if (iface->link != NULL) {
                printf(" [LINKED]");
            } else {
                printf(" [UNLINKED]");
            }
            printf("\n");
        }

        // Print type-specific info
        switch (entry->type) {
            case SIM_NODE_HOST:
                print_host_info((const Host *)entry->node);
                break;
            case SIM_NODE_SWITCH:
                print_switch_info((const Switch *)entry->node);
                break;
            case SIM_NODE_ROUTER:
                print_router_info((const Router *)entry->node);
                break;
        }

        printf("\n");
    }

    // --- Print links ---
    printf("--- Links (%zu total) ---\n\n", simulator->link_count);

    if (simulator->link_count == 0) {
        printf("  (No links configured)\n\n");
    } else {
        for (size_t i = 0; i < simulator->link_count; i++) {
            const Link *l = &simulator->links[i];
            if (l->interface1 == NULL || l->interface2 == NULL) continue;

            const char *name1 = l->interface1->node ? l->interface1->node->NAME : "?";
            const char *name2 = l->interface2->node ? l->interface2->node->NAME : "?";

            printf("  %s:%d <-> %s:%d  (delay=%.0fms)\n",
                   name1, l->interface1->portNumber,
                   name2, l->interface2->portNumber,
                   l->delay);
        }
        printf("\n");
    }

    printf("========================================\n");
}
