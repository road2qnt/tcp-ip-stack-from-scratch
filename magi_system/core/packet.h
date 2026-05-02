#ifndef PACKET_H
#define PACKET_H

#include "interface.h"

typedef enum PacketType{
    ETHERNET,
    ARP,
    IPV4,
    TCP,
    UDP,
    // etc, ...
} PacketType;

typedef struct Packet Packet;
typedef struct Packet{
    PacketType type;
    size_t (*to_bytes) (const Packet* self, uint8_t* out); // Hasil: Pointer ke byte dan ukurannya
};




#endif