#ifndef LINK_H
#define LINK_H

#include "interface.h"
#include "packet.h"
#include "unistd.h"

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

void init_(Link* self, Interface* interface1_, Interface* interface2_, float delay_);
// Transmit Data (Signal)
void transmit(Interface* sender, Link* link, Packet* packet);

#endif