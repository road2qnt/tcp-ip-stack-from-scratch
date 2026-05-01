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


// Class IP Address
typedef struct IpAddress{
    uint8_t octet[4];
    uint8_t prefix;
} IpAddress;
// Method - IP Address
IpAddress ip_init(uint8_t* octet_, uint8_t prefix_);
bool ip_equal(IpAddress* ip_1, IpAddress* ip_2);

#endif