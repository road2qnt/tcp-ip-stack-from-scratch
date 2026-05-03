#ifndef MAC_IP_H
#define MAC_IP_H

#include <stdint.h>
#include <stdbool.h>

// Class Mac Address 
typedef struct MacAddress{
    uint8_t bytes[6];
} MacAddress;
// Method - Mac Address
MacAddress mac_random();
bool mac_equal(MacAddress* mac_1, MacAddress* mac_2); 




#endif