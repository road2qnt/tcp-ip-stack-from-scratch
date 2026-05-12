#ifndef INTERFACE_H
#define INTERFACE_H

#include "mac.h"
#include <stddef.h>

#define MAX_NAME 32
#define MAX_PORT 32

// Forward Declaration
typedef struct Link Link; 
typedef struct Node Node; 
typedef struct NodeVTable NodeVTable;

typedef enum NodeType {
    NODE_UNKNOWN,
    NODE_HOST,
    NODE_SWITCH,
    NODE_ROUTER
} NodeType;

// === MAIN ===
typedef struct Interface{
    int portNumber;
    MacAddress Mac_Address;
    Link* link;
    Node* node;
} Interface;

Interface interface_init(int num, MacAddress mac);
void send(Interface* sender, const uint8_t* raw, size_t raw_len);
void receive(Interface* receiver, const uint8_t* raw, size_t raw_len);

// Abstract Class
struct NodeVTable {
    void (*receive)(Node* self, Interface* in_interface, const uint8_t* raw, size_t raw_len);
};

struct Node{
    NodeType type;
    const NodeVTable* vtable;
    char NAME[MAX_NAME];
    int NUM_INTERFACES;
    Interface interfaces[MAX_PORT];
};

void node_init(Node* node, NodeType type, int num_interfaces);
void node_init_with_macs(Node* node, NodeType type, int num_interfaces, const MacAddress* mac_addresses);
void node_set_vtable(Node* node, const NodeVTable* vtable);
Interface* node_get_interface(Node* node, int port_number);
const Interface* node_get_interface_const(const Node* node, int port_number);
void node_receive(Node* node, Interface* in_interface, const uint8_t* raw, size_t raw_len);
const char* node_type_to_string(NodeType type);

#endif
