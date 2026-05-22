#define _POSIX_C_SOURCE 199309L

#include "link.h"
#include "sim_clock.h"
#include <time.h>

void link_init(Link* link, Interface* interface1_, Interface* interface2_, float delay_){
    if (link == NULL) return;

    link->interface1 = NULL;
    link->interface2 = NULL;
    link->delay = delay_;
    link_link(link,interface1_,interface2_);
    return;
}

void link_unlink(Link* link){
    if (link == NULL) {
        return;
    }

    if (link->interface1 != NULL) {
        link->interface1->link = NULL;
    }

    if (link->interface2 != NULL) {
        link->interface2->link = NULL;
    }

    link->interface1 = NULL;
    link->interface2 = NULL;
}

void link_link(Link* link, Interface* int1, Interface* int2){
    if (link == NULL || int1 == NULL || int2 == NULL || int1->link!=NULL || int2->link!=NULL) {
        return;
    }

    link->interface1 = int1;
    link->interface2 = int2;
    int1->link = link;
    int2->link = link;
}


void transmit(Interface* sender, Link* link, const uint8_t* packet, size_t packet_len){
    if (sender == NULL || link == NULL || packet == NULL) {
        return;
    }

    Interface* receiver = NULL;
    if (sender == link->interface1) {
        receiver = link->interface2;
    } else if (sender == link->interface2) {
        receiver = link->interface1;
    } else {
        return;
    }

    if (receiver == NULL) {
        return;
    }

    if (sim_clock_realtime_enabled() && link->delay > 0) {
        if (sim_clock_enqueue(sender, receiver, link, packet, packet_len)) {
            return;
        }
    }

    if (link->delay > 0 && !sim_clock_realtime_enabled()){
        struct timespec request;
        request.tv_sec = (time_t)(link->delay / 1000);
        request.tv_nsec = (long)((link->delay - (request.tv_sec * 1000)) * 1000000);
        nanosleep(&request, NULL);
    }

    receive(receiver, packet, packet_len);
}
