#ifndef IPV4_H
#define IPV4_H

#include "stdint.h"
#include "stdbool.h"

// Class IP Address
typedef struct IpAddress{
    uint8_t octet[4];
    uint8_t prefix;
} IpAddress;
// Method - IP Address
IpAddress ip_init(uint8_t* octet_, uint8_t prefix_);
bool ip_equal(IpAddress* ip_1, IpAddress* ip_2);

#endif