#ifndef PACKET_H
#define PACKET_H

#include "interface.h"
typedef struct Packet{
    /*
        1. Saat di send() : Packet berubah menjadi byte
        
    */
} Packet;

// Method
void packet_receive(Interface* receiver, Packet* packet);
void packet_send(Interface* sender, Packet* packet);

#endif