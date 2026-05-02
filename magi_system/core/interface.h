#ifndef INTEFACE_H
#define INTERFACE_H

#include "mac_ip.h"
#include "../dataStructure/map.h"

#define MAX_NAME 32
#define MAX_PORT 32

// Forward Declaration
typedef struct Link Link; 
typedef struct Node Node; 

// === MAIN ===
typedef struct Interface{
    int portNumber;
    MacAddress Mac_Address;
    IpAddress ip_address;
    IpAddress default_gateway;

    // Tambahan
    bool has_ip; 
    Link* link;
    Node* node;
} Interface;

Interface interface_init(int num, MacAddress mac);
void send(Interface* sender, uint8_t raw); // bytes
void receive(Interface* receiver, uint8_t raw);

// Abstract Class
typedef struct Node{
    char NAME[MAX_NAME];
    int NUM_INTERFACES;
    Interface* interfaces[MAX_PORT];
}Node;

Node node_init(int num, Interface* interface);

// Objek yang memakai interface
typedef struct Host{
    Node base;
}Host;

typedef struct Switch{
    Node base;
}Switch;

typedef struct Router{
    Node base;
}Router;

// P.S. (Reminder)
// 1. Map : buat MAC Table

#endif

