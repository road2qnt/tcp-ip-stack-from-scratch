#include "simulator.h"
#include "layer2/host.h"
#include "layer2/switch.h"
#include "layer3/router.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== HELPER FUNCTIONS ====================

int simulator_find_node(const Simulator *simulator, const char *name)
{
    if (simulator == NULL || name == NULL) return -1;

    for (size_t i = 0; i < simulator->node_count; i++) {
        if (strcmp(simulator->nodes[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static SimulatorNodeType parse_node_type(const char *type_str)
{
    if (strcmp(type_str, "host") == 0) return SIM_NODE_HOST;
    if (strcmp(type_str, "switch") == 0) return SIM_NODE_SWITCH;
    if (strcmp(type_str, "router") == 0) return SIM_NODE_ROUTER;
    return (SimulatorNodeType)-1;
}

static int parse_endpoint(const char *endpoint, char *out_name, size_t name_size, int *out_port)
{
    const char *colon = strchr(endpoint, ':');
    size_t name_len;

    if (colon == NULL) {
        // No colon: whole string is device name, port defaults to 1
        name_len = strlen(endpoint);
        if (name_len >= name_size) name_len = name_size - 1;
        strncpy(out_name, endpoint, name_len);
        out_name[name_len] = '\0';
        *out_port = 1;
    } else {
        name_len = (size_t)(colon - endpoint);
        if (name_len >= name_size) name_len = name_size - 1;
        strncpy(out_name, endpoint, name_len);
        out_name[name_len] = '\0';
        *out_port = atoi(colon + 1);
    }

    return 1;
}

static int endpoint_to_interface(Simulator *simulator, const char *endpoint,
                                  Node **out_node, Interface **out_iface)
{
    char name[MAX_NAME];
    int port;

    if (out_node) *out_node = NULL;
    if (out_iface) *out_iface = NULL;

    if (!parse_endpoint(endpoint, name, sizeof(name), &port)) return 0;

    int idx = simulator_find_node(simulator, name);
    if (idx < 0) return 0;

    Node *node = simulator->nodes[idx].node;
    if (node == NULL) return 0;

    Interface *iface = node_get_interface(node, port);
    if (iface == NULL) return 0;

    if (out_node) *out_node = node;
    if (out_iface) *out_iface = iface;
    return 1;
}

// ==================== CONSTRUCTOR & DESTRUCTOR ====================

void simulator_init(Simulator *simulator)
{
    if (simulator == NULL) return;

    simulator->node_count = 0;
    simulator->link_count = 0;
    simulator->interface_count = 0;
}

void simulator_clear(Simulator *simulator)
{
    if (simulator == NULL) return;

    // Unlink all links first
    for (size_t i = 0; i < simulator->link_count; i++) {
        link_unlink(&simulator->links[i]);
    }
    simulator->link_count = 0;

    // Free all allocated nodes
    for (size_t i = 0; i < simulator->node_count; i++) {
        if (simulator->nodes[i].node != NULL) {
            free(simulator->nodes[i].node);
            simulator->nodes[i].node = NULL;
        }
    }
    simulator->node_count = 0;

    // Clear interfaces
    for (size_t i = 0; i < simulator->interface_count; i++) {
        simulator->interfaces[i] = NULL;
    }
    simulator->interface_count = 0;
}

// ==================== NODE CREATION ====================

int simulator_create_node(Simulator *simulator, const char *type, const char *name, int requested_ports)
{
    if (simulator == NULL || type == NULL || name == NULL) return 0;

    // Check for duplicate name
    if (simulator_find_node(simulator, name) >= 0) return 0;

    // Check capacity
    if (simulator->node_count >= SIMULATOR_MAX_NODES) return 0;

    SimulatorNodeType node_type = parse_node_type(type);
    if (node_type == (SimulatorNodeType)-1) return 0;

    // Determine number of ports
    int num_ports = requested_ports > 0 ? requested_ports : 1;
    if (num_ports > MAX_PORT) num_ports = MAX_PORT;

    // Allocate the appropriate node type
    Node *node = NULL;

    switch (node_type) {
        case SIM_NODE_HOST: {
            Host *host = (Host *)calloc(1, sizeof(Host));
            if (host == NULL) return 0;
            host_init(host, num_ports);
            strncpy(host->base.NAME, name, MAX_NAME - 1);
            host->base.NAME[MAX_NAME - 1] = '\0';
            node = &host->base;
            break;
        }
        case SIM_NODE_SWITCH: {
            Switch *sw = (Switch *)calloc(1, sizeof(Switch));
            if (sw == NULL) return 0;
            switch_init(sw, num_ports);
            strncpy(sw->base.NAME, name, MAX_NAME - 1);
            sw->base.NAME[MAX_NAME - 1] = '\0';
            node = &sw->base;
            break;
        }
        case SIM_NODE_ROUTER: {
            Router *router = (Router *)calloc(1, sizeof(Router));
            if (router == NULL) return 0;
            router_init(router, num_ports);
            strncpy(router->base.NAME, name, MAX_NAME - 1);
            router->base.NAME[MAX_NAME - 1] = '\0';
            node = &router->base;
            break;
        }
        default:
            return 0;
    }

    if (node == NULL) return 0;

    // Register interfaces
    for (int i = 0; i < num_ports; i++) {
        if (simulator->interface_count < SIMULATOR_MAX_INTERFACES) {
            simulator->interfaces[simulator->interface_count++] = &node->interfaces[i];
        }
    }

    // Add to node list
    SimulatorNodeEntry *entry = &simulator->nodes[simulator->node_count++];
    strncpy(entry->name, name, MAX_NAME - 1);
    entry->name[MAX_NAME - 1] = '\0';
    entry->type = node_type;
    entry->node = node;

    return 1;
}

// ==================== LINK MANAGEMENT ====================

int simulator_link(Simulator *simulator, const char *endpoint_a, const char *endpoint_b, float delay_ms)
{
    if (simulator == NULL || endpoint_a == NULL || endpoint_b == NULL) return 0;
    if (simulator->link_count >= SIMULATOR_MAX_LINKS) return 0;
    if (delay_ms < 0) delay_ms = 0;

    Node *node_a = NULL, *node_b = NULL;
    Interface *iface_a = NULL, *iface_b = NULL;

    if (!endpoint_to_interface(simulator, endpoint_a, &node_a, &iface_a)) return 0;
    if (!endpoint_to_interface(simulator, endpoint_b, &node_b, &iface_b)) return 0;

    // Cannot link a node to itself
    if (iface_a == iface_b) return 0;

    // Check if either interface already has a link
    if (iface_a->link != NULL || iface_b->link != NULL) return 0;

    // Check for duplicate link
    for (size_t i = 0; i < simulator->link_count; i++) {
        Link *l = &simulator->links[i];
        if ((l->interface1 == iface_a && l->interface2 == iface_b) ||
            (l->interface1 == iface_b && l->interface2 == iface_a)) {
            return 0;
        }
    }

    Link *link = &simulator->links[simulator->link_count++];
    link_init(link, iface_a, iface_b, delay_ms);

    return 1;
}

int simulator_unlink(Simulator *simulator, const char *endpoint_a, const char *endpoint_b)
{
    if (simulator == NULL || endpoint_a == NULL || endpoint_b == NULL) return 0;

    Node *node_a = NULL, *node_b = NULL;
    Interface *iface_a = NULL, *iface_b = NULL;

    if (!endpoint_to_interface(simulator, endpoint_a, &node_a, &iface_a)) return 0;
    if (!endpoint_to_interface(simulator, endpoint_b, &node_b, &iface_b)) return 0;

    // Find and remove the link
    for (size_t i = 0; i < simulator->link_count; i++) {
        Link *l = &simulator->links[i];
        if ((l->interface1 == iface_a && l->interface2 == iface_b) ||
            (l->interface1 == iface_b && l->interface2 == iface_a)) {
            link_unlink(l);

            // Shift remaining links
            for (size_t j = i; j + 1 < simulator->link_count; j++) {
                simulator->links[j] = simulator->links[j + 1];
            }
            simulator->link_count--;

            return 1;
        }
    }

    return 0;
}

// ==================== NODE REMOVAL ====================

int simulator_remove_node(Simulator *simulator, const char *name)
{
    if (simulator == NULL || name == NULL) return 0;

    int idx = simulator_find_node(simulator, name);
    if (idx < 0) return 0;

    Node *node = simulator->nodes[idx].node;
    if (node == NULL) return 0;

    // Unlink all interfaces of this node
    for (int p = 0; p < node->NUM_INTERFACES; p++) {
        Interface *iface = &node->interfaces[p];
        if (iface->link != NULL) {
            // Find the link in simulator's link list and remove it
            for (size_t i = 0; i < simulator->link_count; i++) {
                Link *l = &simulator->links[i];
                if (l->interface1 == iface || l->interface2 == iface) {
                    link_unlink(l);
                    // Shift remaining links
                    for (size_t j = i; j + 1 < simulator->link_count; j++) {
                        simulator->links[j] = simulator->links[j + 1];
                    }
                    simulator->link_count--;
                    break;
                }
            }
        }
    }

    // Remove interfaces from interface array
    for (int p = 0; p < node->NUM_INTERFACES; p++) {
        Interface *iface = &node->interfaces[p];
        for (size_t i = 0; i < simulator->interface_count; i++) {
            if (simulator->interfaces[i] == iface) {
                for (size_t j = i; j + 1 < simulator->interface_count; j++) {
                    simulator->interfaces[j] = simulator->interfaces[j + 1];
                }
                simulator->interface_count--;
                break;
            }
        }
    }

    // Free the node memory
    free(node);

    // Shift remaining nodes
    for (size_t i = (size_t)idx; i + 1 < simulator->node_count; i++) {
        simulator->nodes[i] = simulator->nodes[i + 1];
    }
    simulator->node_count--;

    return 1;
}
