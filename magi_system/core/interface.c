#include "interface.h"
#include "link.h"

static void node_default_receive(Node* self, Interface* in_interface, const uint8_t* raw, size_t raw_len){
    (void)self;
    (void)in_interface;
    (void)raw;
    (void)raw_len;
}

static const NodeVTable NODE_DEFAULT_VTABLE = {
    .receive = node_default_receive
};

// Interface
Interface interface_init(int num, MacAddress mac){
    Interface inter;
    inter.portNumber = num;
    inter.Mac_Address = mac;
    inter.link = NULL;
    inter.node = NULL;
    return inter;
}

void send(Interface* sender, const uint8_t* raw, size_t raw_len){
    if (sender == NULL || sender->link == NULL || raw == NULL) {
        return;
    }

    transmit(sender, sender->link, raw, raw_len);
}

void receive(Interface* receiver, const uint8_t* raw, size_t raw_len){
    if (receiver == NULL) {
        return;
    }

    node_receive(receiver->node, receiver, raw, raw_len);
}

static int normalize_interface_count(int num_interfaces){
    if (num_interfaces < 0) {
        return 0;
    }

    if (num_interfaces > MAX_PORT) {
        return MAX_PORT;
    }

    return num_interfaces;
}

// Node
void node_init(Node* node, NodeType type, int num_interfaces){
    node_init_with_macs(node, type, num_interfaces, NULL);
}

// [!] Belum cek untuk semua MAC Address di Simulator
void node_init_with_macs(Node* node, NodeType type, int num_interfaces, const MacAddress* mac_addresses){
    if (node == NULL) {
        return;
    }

    num_interfaces = normalize_interface_count(num_interfaces);

    node->type = type;
    node->vtable = &NODE_DEFAULT_VTABLE;
    node->NUM_INTERFACES = num_interfaces;
    node->NAME[0] = '\0';

    for (int i=0;i<MAX_PORT;i++){
        MacAddress mac = (mac_addresses != NULL && i < num_interfaces) ? mac_addresses[i] : mac_random();
        node->interfaces[i] = interface_init(i + 1, mac);
        node->interfaces[i].node = i < num_interfaces ? node : NULL;
    }
}

void node_set_vtable(Node* node, const NodeVTable* vtable){
    if (node == NULL) {
        return;
    }

    node->vtable = vtable == NULL ? &NODE_DEFAULT_VTABLE : vtable;
}

Interface* node_get_interface(Node* node, int port_number){
    if (node == NULL || port_number < 1 || port_number > node->NUM_INTERFACES) {
        return NULL;
    }

    return &node->interfaces[port_number - 1];
}

const Interface* node_get_interface_const(const Node* node, int port_number){
    if (node == NULL || port_number < 1 || port_number > node->NUM_INTERFACES) {
        return NULL;
    }

    return &node->interfaces[port_number - 1];
}

void node_receive(Node* node, Interface* in_interface, const uint8_t* raw, size_t raw_len){
    if (node == NULL || node->vtable == NULL || node->vtable->receive == NULL) {
        return;
    }

    node->vtable->receive(node, in_interface, raw, raw_len);
}

const char* node_type_to_string(NodeType type){
    switch (type) {
        case NODE_HOST:
            return "host";
        case NODE_SWITCH:
            return "switch";
        case NODE_ROUTER:
            return "router";
        case NODE_UNKNOWN:
        default:
            return "unknown";
    }
}
