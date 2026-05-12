#include "mac.h"
#include <stdlib.h>

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
