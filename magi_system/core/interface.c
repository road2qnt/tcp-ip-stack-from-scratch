#include "interface.h"
#include "link.h"

// Interface
Interface interface_init(int num, MacAddress mac){
    Interface inter;
    inter.portNumber = num;
    inter.Mac_Address = mac;
    inter.has_ip = true;
    return inter;
}

void send(Interface* sender, uint8_t raw){
    transmit(sender, sender->link, raw);
}

void receive(Interface* receiver, uint8_t raw){
    // frombytes 
    // terusin ke host/router/switch (Layer2-Layer3-dst)
}

// Node
Node node_init(int num, Interface* interface){
    Node result;
    result.NUM_INTERFACES = num;
    for (int i=0;i<num;i++){
        result.interfaces[i] = &interface[i];
    }
    return result;
}