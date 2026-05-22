#include "mac.h"
#include <stdlib.h>
#include <stdio.h>

MacAddress mac_random(){
    MacAddress mac;
    for (int i=0;i<6;i++){
        mac.bytes[i] = (uint8_t) rand();
    }
    return mac;
}

bool mac_equal(MacAddress* mac_1, MacAddress* mac_2){
    for (int i=0;i<6;i++){
        if (mac_1->bytes[i]!=mac_2->bytes[i]){
            return false;
        }
    }
    return true;
}

void mac_to_string(const MacAddress* mac, char* out, size_t out_size){
    if (out == NULL || out_size == 0) return;
    if (mac == NULL) {
        snprintf(out, out_size, "??:??:??:??:??:??");
        return;
    }
    snprintf(out, out_size, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac->bytes[0], mac->bytes[1], mac->bytes[2],
             mac->bytes[3], mac->bytes[4], mac->bytes[5]);
}
