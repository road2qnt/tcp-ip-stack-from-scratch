#ifndef INTEFACE_H
#define INTERFACE_H

#include "mac_ip.h"

#define MAX_NAME 32

// === MAIN ===
typedef struct Interface{
    int portNumber;
    char Name[MAX_NAME];
    MacAddress Mac_Address;
} Interface;


// Abstract Class
typedef struct Node{
    char NAME[MAX_NAME];
}Node;

// Objek yang memakai interface
typedef struct Host{
    Node base;
    Interface* interface;
    IpAddress ip_address;
    IpAddress default_gateway;
}Host;

typedef struct Switch{
    Node base;
    Interface interface[10]; // [!] Masih HardCode - Nanti diubah
}Switch;

typedef struct Router{
    Node base;
    Interface interface[10];
}Router;

#endif

