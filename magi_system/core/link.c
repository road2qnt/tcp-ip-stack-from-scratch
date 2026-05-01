#include "link.h"

void transmit(Interface* sender, Link* link, Packet* packet){
    Interface* receiver = (sender == link->interface1) ? link->interface2 : link->interface1;

    if (link->delay>0){
        usleep(link->delay);
    }

    // Receive dan Send
}