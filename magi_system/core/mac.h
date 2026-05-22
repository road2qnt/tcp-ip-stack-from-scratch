#ifndef MAC_H
#define MAC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Class Mac Address 
typedef struct MacAddress{
    uint8_t bytes[6];
} MacAddress;
// Method - Mac Address
MacAddress mac_random();
bool mac_equal(MacAddress* mac_1, MacAddress* mac_2); 
void mac_to_string(const MacAddress* mac, char* out, size_t out_size);

#endif