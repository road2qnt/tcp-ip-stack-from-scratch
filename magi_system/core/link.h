#ifndef LINK_H
#define LINK_H

#include "interface.h"
#include "packet.h"

typedef struct Link {
    /*
        1. Data delay untuk simulasi real.
        2. Panggil send () di interface pengirim dan receive() di interface penerima
        3. Menyimpan interface2 yang dihubungkan
    */
    // Node interface 1
    // Node interface 2
    Interface* interface1;
    Interface* interface2;
    float delay;
}Link;

// Method

void link_init(Link* link, Interface* interface1_, Interface* interface2_, float delay_);
// Transmit Data (Signal)
void transmit(Interface* sender, Link* link, const uint8_t* packet, size_t packet_len);

// Link 
void link_unlink(Link* link);
void link_link(Link* link, Interface* int1, Interface* int2);
#endif
