#ifndef SIM_CLOCK_H
#define SIM_CLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "interface.h"

#define SIM_INFLIGHT_MAX 256
#define SIM_INFLIGHT_PAYLOAD_MAX 2048

typedef struct Link Link;

typedef struct InFlightPacket {
    bool active;
    Interface* sender;
    Interface* receiver;
    Link* link;
    double launch_at_ms;
    double deliver_at_ms;
    uint16_t ethertype;
    uint8_t l4_protocol;
    size_t len;
    uint8_t bytes[SIM_INFLIGHT_PAYLOAD_MAX];
} InFlightPacket;

void sim_clock_set_realtime(bool on);
bool sim_clock_realtime_enabled(void);

void   sim_clock_set_speed(double factor);
double sim_clock_get_speed(void);

void   sim_clock_set_paused(bool paused);
bool   sim_clock_is_paused(void);
void   sim_clock_step(double advance_ms);

double sim_clock_now_ms(void);

int sim_clock_enqueue(Interface* sender, Interface* receiver, Link* link,
                      const uint8_t* bytes, size_t len);

void sim_clock_tick(double now_ms);

void sim_clock_clear(void);

size_t sim_clock_in_flight_count(void);
const InFlightPacket* sim_clock_in_flight_at(size_t idx);

#endif
