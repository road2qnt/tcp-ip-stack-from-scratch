#include "loader.h"
#include "json.h"

#include "../layer2/host.h"
#include "../layer2/switch.h"
#include "../layer3/router.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ==================== IP ADDRESS PARSING HELPER ====================

// Parse "192.168.1.10/24" into IpAddress struct
static int parse_ip_address(const char *str, IpAddress *ip)
{
    if (str == NULL || ip == NULL) return 0;

    unsigned int octets[4];
    int prefix;
    int count = sscanf(str, "%u.%u.%u.%u/%d", &octets[0], &octets[1], &octets[2], &octets[3], &prefix);

    if (count != 5) {
        // Try without prefix
        count = sscanf(str, "%u.%u.%u.%u", &octets[0], &octets[1], &octets[2], &octets[3]);
        if (count != 4) return 0;
        prefix = 0;
    }

    for (int i = 0; i < 4; i++) {
        if (octets[i] > 255) return 0;
        ip->octet[i] = (uint8_t)octets[i];
    }
    ip->prefix = (uint8_t)prefix;
    return 1;
}

// Format IpAddress back to "192.168.1.10/24" string
static void ip_address_to_string(const IpAddress *ip, char *out, size_t out_size)
{
    if (ip->prefix > 0) {
        snprintf(out, out_size, "%d.%d.%d.%d/%d",
                 ip->octet[0], ip->octet[1], ip->octet[2], ip->octet[3],
                 ip->prefix);
    } else {
        snprintf(out, out_size, "%d.%d.%d.%d",
                 ip->octet[0], ip->octet[1], ip->octet[2], ip->octet[3]);
    }
}

// ==================== SIMULATOR SAVE ====================

int simulator_save(Simulator *simulator, const char *filename)
{
    if (simulator == NULL || filename == NULL) return 0;

    FILE *f = fopen(filename, "w");
    if (f == NULL) {
        perror("Failed to open file for writing");
        return 0;
    }

    // Build JSON
    // Top level object
    JsonValue *root = json_create_object();

    // --- hosts ---
    JsonValue *hosts_arr = json_create_array();
    for (size_t i = 0; i < simulator->node_count; i++) {
        if (simulator->nodes[i].type != SIM_NODE_HOST) continue;

        Host *host = (Host *)simulator->nodes[i].node;
        JsonValue *h = json_create_object();

        json_object_add(h, "name", json_create_string(simulator->nodes[i].name));

        if (host->has_ip) {
            char ip_str[64];
            ip_address_to_string(&host->ip_address, ip_str, sizeof(ip_str));
            json_object_add(h, "ip_address", json_create_string(ip_str));

            char gw_str[64];
            ip_address_to_string(&host->default_gateway, gw_str, sizeof(gw_str));
            json_object_add(h, "default_gateway", json_create_string(gw_str));
        }

        json_array_add(hosts_arr, h);
    }
    json_object_add(root, "hosts", hosts_arr);

    // --- switches ---
    JsonValue *switches_arr = json_create_array();
    for (size_t i = 0; i < simulator->node_count; i++) {
        if (simulator->nodes[i].type != SIM_NODE_SWITCH) continue;

        Switch *sw = (Switch *)simulator->nodes[i].node;
        JsonValue *s = json_create_object();

        json_object_add(s, "name", json_create_string(simulator->nodes[i].name));
        json_object_add(s, "num_ports", json_create_number((double)sw->base.NUM_INTERFACES));

        // VLAN configs (only non-default)
        JsonValue *vlans_arr = json_create_array();
        for (int p = 0; p < sw->base.NUM_INTERFACES; p++) {
            const SwitchPortConfig *cfg = &sw->port_configs[p];

            if (cfg->mode == SWITCH_PORT_ACCESS && cfg->vlan_id == SWITCH_DEFAULT_VLAN_ID) {
                // Skip default config
                continue;
            }

            JsonValue *v = json_create_object();
            json_object_add(v, "port", json_create_number((double)(p + 1)));

            if (cfg->mode == SWITCH_PORT_ACCESS) {
                json_object_add(v, "mode", json_create_string("access"));
                json_object_add(v, "vlan_id", json_create_number((double)cfg->vlan_id));
            } else {
                json_object_add(v, "mode", json_create_string("trunk"));
            }

            json_array_add(vlans_arr, v);
        }
        json_object_add(s, "vlans", vlans_arr);

        json_array_add(switches_arr, s);
    }
    json_object_add(root, "switches", switches_arr);

    // --- routers ---
    JsonValue *routers_arr = json_create_array();
    for (size_t i = 0; i < simulator->node_count; i++) {
        if (simulator->nodes[i].type != SIM_NODE_ROUTER) continue;

        Router *rtr = (Router *)simulator->nodes[i].node;
        JsonValue *r = json_create_object();

        json_object_add(r, "name", json_create_string(simulator->nodes[i].name));

        // Interfaces (only ones with IP)
        JsonValue *intfs_arr = json_create_array();
        for (int p = 0; p < rtr->base.NUM_INTERFACES; p++) {
            if (!rtr->interface_ips[p].has_ip) continue;

            JsonValue *inf = json_create_object();
            json_object_add(inf, "port", json_create_number((double)(p + 1)));

            char ip_str[64];
            ip_address_to_string(&rtr->interface_ips[p].ip_address, ip_str, sizeof(ip_str));
            json_object_add(inf, "ip_address", json_create_string(ip_str));

            json_array_add(intfs_arr, inf);
        }
        json_object_add(r, "interfaces", intfs_arr);

        // Routing table - currently empty (will be implemented in M2)
        JsonValue *rt_arr = json_create_array();
        json_object_add(r, "routing_table", rt_arr);

        json_array_add(routers_arr, r);
    }
    json_object_add(root, "routers", routers_arr);

    // --- links ---
    JsonValue *links_arr = json_create_array();
    for (size_t i = 0; i < simulator->link_count; i++) {
        Link *lk = &simulator->links[i];
        if (lk->interface1 == NULL || lk->interface2 == NULL) continue;

        JsonValue *l = json_create_object();

        // Build endpoint strings "NodeName:Port"
        char ep1[128], ep2[128];
        snprintf(ep1, sizeof(ep1), "%s:%d", lk->interface1->node->NAME, lk->interface1->portNumber);
        snprintf(ep2, sizeof(ep2), "%s:%d", lk->interface2->node->NAME, lk->interface2->portNumber);

        JsonValue *eps_arr = json_create_array();
        json_array_add(eps_arr, json_create_string(ep1));
        json_array_add(eps_arr, json_create_string(ep2));
        json_object_add(l, "endpoints", eps_arr);

        json_object_add(l, "delay", json_create_number((double)lk->delay));

        json_array_add(links_arr, l);
    }
    json_object_add(root, "links", links_arr);

    // Write JSON to file
    json_write(f, root, 0);
    fprintf(f, "\n");

    json_free(root);
    fclose(f);

    return 1;
}

// ==================== SIMULATOR LOAD ====================

int simulator_load(Simulator *simulator, const char *filename)
{
    if (simulator == NULL || filename == NULL) return 0;

    FILE *f = fopen(filename, "r");
    if (f == NULL) {
        perror("Failed to open file for reading");
        return 0;
    }

    // Read entire file into memory
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if (file_size <= 0) {
        fclose(f);
        return 0;
    }

    fseek(f, 0, SEEK_SET);
    char *content = (char *)malloc((size_t)file_size + 1);
    if (content == NULL) {
        fclose(f);
        return 0;
    }

    size_t bytes_read = fread(content, 1, (size_t)file_size, f);
    content[bytes_read] = '\0';
    fclose(f);

    // Parse JSON
    JsonValue *root = json_parse(content);
    free(content);

    if (root == NULL || root->type != JSON_OBJECT) {
        json_free(root);
        return 0;
    }

    // Clear current simulator state
    simulator_clear(simulator);

    // --- Load hosts ---
    JsonValue *hosts_arr = json_object_get(root, "hosts");
    if (hosts_arr != NULL && hosts_arr->type == JSON_ARRAY) {
        for (size_t i = 0; i < hosts_arr->data.array.count; i++) {
            JsonValue *h = json_array_get(hosts_arr, i);
            if (h == NULL || h->type != JSON_OBJECT) continue;

            JsonValue *name_val = json_object_get(h, "name");
            if (name_val == NULL || name_val->type != JSON_STRING) continue;

            const char *host_name = name_val->data.string;
            int ports = 1;

            if (!simulator_create_node(simulator, "host", host_name, ports)) {
                json_free(root);
                return 0;
            }

            // Find the host node we just created
            int idx = simulator_find_node(simulator, host_name);
            if (idx < 0) continue;
            Host *host = (Host *)simulator->nodes[idx].node;

            // Set IP address if provided
            JsonValue *ip_val = json_object_get(h, "ip_address");
            JsonValue *gw_val = json_object_get(h, "default_gateway");
            if (ip_val != NULL && ip_val->type == JSON_STRING) {
                if (parse_ip_address(ip_val->data.string, &host->ip_address)) {
                    host->has_ip = true;
                }
            }
            if (gw_val != NULL && gw_val->type == JSON_STRING && host->has_ip) {
                parse_ip_address(gw_val->data.string, &host->default_gateway);
            }
        }
    }

    // --- Load switches ---
    JsonValue *switches_arr = json_object_get(root, "switches");
    if (switches_arr != NULL && switches_arr->type == JSON_ARRAY) {
        for (size_t i = 0; i < switches_arr->data.array.count; i++) {
            JsonValue *s = json_array_get(switches_arr, i);
            if (s == NULL || s->type != JSON_OBJECT) continue;

            JsonValue *name_val = json_object_get(s, "name");
            if (name_val == NULL || name_val->type != JSON_STRING) continue;

            const char *sw_name = name_val->data.string;

            // Get number of ports
            JsonValue *ports_val = json_object_get(s, "num_ports");
            int num_ports = ports_val != NULL && ports_val->type == JSON_NUMBER
                          ? (int)ports_val->data.number : 24;

            if (!simulator_create_node(simulator, "switch", sw_name, num_ports)) {
                json_free(root);
                return 0;
            }

            int idx = simulator_find_node(simulator, sw_name);
            if (idx < 0) continue;
            Switch *sw = (Switch *)simulator->nodes[idx].node;

            // Apply VLAN configurations
            JsonValue *vlans_arr = json_object_get(s, "vlans");
            if (vlans_arr != NULL && vlans_arr->type == JSON_ARRAY) {
                for (size_t j = 0; j < vlans_arr->data.array.count; j++) {
                    JsonValue *v = json_array_get(vlans_arr, j);
                    if (v == NULL || v->type != JSON_OBJECT) continue;

                    JsonValue *port_val = json_object_get(v, "port");
                    JsonValue *mode_val = json_object_get(v, "mode");
                    if (port_val == NULL || port_val->type != JSON_NUMBER ||
                        mode_val == NULL || mode_val->type != JSON_STRING) continue;

                    int port = (int)port_val->data.number;
                    const char *mode = mode_val->data.string;

                    if (strcmp(mode, "access") == 0) {
                        JsonValue *vlan_id_val = json_object_get(v, "vlan_id");
                        int vlan_id = vlan_id_val != NULL && vlan_id_val->type == JSON_NUMBER
                                    ? (int)vlan_id_val->data.number : SWITCH_DEFAULT_VLAN_ID;
                        switch_set_access_port(sw, port, vlan_id);
                    } else if (strcmp(mode, "trunk") == 0) {
                        switch_set_trunk_port(sw, port);
                    }
                }
            }
        }
    }

    // --- Load routers ---
    JsonValue *routers_arr = json_object_get(root, "routers");
    if (routers_arr != NULL && routers_arr->type == JSON_ARRAY) {
        for (size_t i = 0; i < routers_arr->data.array.count; i++) {
            JsonValue *r = json_array_get(routers_arr, i);
            if (r == NULL || r->type != JSON_OBJECT) continue;

            JsonValue *name_val = json_object_get(r, "name");
            if (name_val == NULL || name_val->type != JSON_STRING) continue;

            const char *rtr_name = name_val->data.string;
            int num_ports = 8; // default

            if (!simulator_create_node(simulator, "router", rtr_name, num_ports)) {
                json_free(root);
                return 0;
            }

            int idx = simulator_find_node(simulator, rtr_name);
            if (idx < 0) continue;
            Router *rtr = (Router *)simulator->nodes[idx].node;

            // Set interface IPs
            JsonValue *intfs_arr = json_object_get(r, "interfaces");
            if (intfs_arr != NULL && intfs_arr->type == JSON_ARRAY) {
                for (size_t j = 0; j < intfs_arr->data.array.count; j++) {
                    JsonValue *inf = json_array_get(intfs_arr, j);
                    if (inf == NULL || inf->type != JSON_OBJECT) continue;

                    JsonValue *port_val = json_object_get(inf, "port");
                    JsonValue *ip_val = json_object_get(inf, "ip_address");
                    if (port_val == NULL || port_val->type != JSON_NUMBER ||
                        ip_val == NULL || ip_val->type != JSON_STRING) continue;

                    int port = (int)port_val->data.number;
                    if (port < 1 || port > rtr->base.NUM_INTERFACES) continue;

                    if (parse_ip_address(ip_val->data.string, &rtr->interface_ips[port - 1].ip_address)) {
                        rtr->interface_ips[port - 1].has_ip = true;
                        rtr->interface_ips[port - 1].portNumber = port;
                    }
                }
            }

            // Note: routing table entries will be loaded when routing table struct is added in M2
        }
    }

    // --- Load links ---
    JsonValue *links_arr = json_object_get(root, "links");
    if (links_arr != NULL && links_arr->type == JSON_ARRAY) {
        for (size_t i = 0; i < links_arr->data.array.count; i++) {
            JsonValue *l = json_array_get(links_arr, i);
            if (l == NULL || l->type != JSON_OBJECT) continue;

            JsonValue *eps_val = json_object_get(l, "endpoints");
            JsonValue *delay_val = json_object_get(l, "delay");
            if (eps_val == NULL || eps_val->type != JSON_ARRAY ||
                eps_val->data.array.count < 2) continue;

            JsonValue *ep1 = json_array_get(eps_val, 0);
            JsonValue *ep2 = json_array_get(eps_val, 1);
            if (ep1 == NULL || ep1->type != JSON_STRING ||
                ep2 == NULL || ep2->type != JSON_STRING) continue;

            float delay = delay_val != NULL && delay_val->type == JSON_NUMBER
                        ? (float)delay_val->data.number : 0.0f;

            if (!simulator_link(simulator, ep1->data.string, ep2->data.string, delay)) {
                // Continue trying other links even if one fails
                continue;
            }
        }
    }

    json_free(root);
    return 1;
}
