#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "core/interface.h"
#include "core/packet.h"
#include "core/link.h"
#include "core/mac.h"
#include "dataStructure/map.h"

#include <stddef.h>

#define SIMULATOR_MAX_NODES 128
#define SIMULATOR_MAX_LINKS 256
#define SIMULATOR_MAX_INTERFACES 4096

typedef enum SimulatorNodeType {
    SIM_NODE_HOST,
    SIM_NODE_SWITCH,
    SIM_NODE_ROUTER
} SimulatorNodeType;

typedef struct SimulatorNodeEntry {
    char name[MAX_NAME];
    SimulatorNodeType type;
    Node *node;
} SimulatorNodeEntry;

typedef struct Simulator{
    SimulatorNodeEntry nodes[SIMULATOR_MAX_NODES];
    size_t node_count;

    Link links[SIMULATOR_MAX_LINKS];
    size_t link_count;

    Interface* interfaces[SIMULATOR_MAX_INTERFACES];
    size_t interface_count;
} Simulator;

// Constructor & Destructor
void simulator_init(Simulator *simulator);
void simulator_clear(Simulator *simulator);

// Method
int simulator_create_node(Simulator *simulator,const char *type,const char *name,int requested_ports);
int simulator_link(Simulator *simulator,const char *endpoint_a,const char *endpoint_b,float delay_ms);
int simulator_unlink(Simulator *simulator,const char *endpoint_a,const char *endpoint_b);

// Remove a specific node by name (also removes associated links)
int simulator_remove_node(Simulator *simulator, const char *name);

// Utility function (used by loader too)
int simulator_find_node(const Simulator *simulator, const char *name);

#endif 
