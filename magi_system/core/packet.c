#include "packet.h"

size_t packet_to_bytes(const Packet *packet,uint8_t *out,size_t out_size){
    if (packet == NULL || packet->vtable == NULL || packet->vtable->to_bytes == NULL) {
        return 0;
    }

    return packet->vtable->to_bytes(packet, out, out_size);
}

int packet_from_bytes(Packet *packet,const uint8_t *raw,size_t raw_len){
    if (packet == NULL || packet->vtable == NULL || packet->vtable->from_bytes == NULL) {
        return 0;
    }

    return packet->vtable->from_bytes(packet, raw, raw_len);
}
