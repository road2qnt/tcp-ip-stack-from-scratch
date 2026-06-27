#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#include <stddef.h>

typedef enum PacketType{
    ETHERNET,
    ARP,
    ICMP,
    IPV4,
    TCP,
    UDP,
} PacketType;

typedef struct Packet Packet;
typedef struct PacketVTable PacketVTable;

struct PacketVTable{
    size_t (*to_bytes) (const Packet* self, uint8_t* out, size_t out_size); // Hasil: Pointer ke byte dan ukurannya
    int (*from_bytes) (Packet* self, const uint8_t *raw, size_t raw_len);
};

struct Packet{
    const PacketVTable *vtable;
    PacketType type;
};

size_t packet_to_bytes(const Packet* self, uint8_t* out, size_t out_size);
int packet_from_bytes (Packet* self, const uint8_t *raw, size_t raw_len);

#endif