#include "link.h"

Link link_init(Interface* interface1_, Interface* interface2_, float delay_){
    Link L;
    L.interface1 = interface1_;
    L.interface2 = interface2_;
    L.delay = delay_;
    return L;
}

void transmit(Interface* sender, Link* link, uint8_t* packet){
    Interface* receiver = (sender == link->interface1) ? link->interface2 : link->interface1;

    if (link->delay>0){
        usleep(link->delay);
    }

    // Receive 
    receive(receiver, packet);
}