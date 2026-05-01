#include "mac_ip.h"

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

IpAddress ip_init(uint8_t* octet_, uint8_t prefix_){
    IpAddress IP;
    for (int i=0;i<4;i++){
        IP.octet[i] = octet_[i];
    }
    IP.prefix = prefix_;
    return IP;
}

bool ip_equal(IpAddress* ip_1, IpAddress* ip_2){
    for (int i=0;i<4;i++){
        if (ip_1->octet[i]!=ip_2->octet[i]){
            return false;
        }
    }

    if (ip_1->prefix!=ip_2->prefix) return false;

    return true;
}