#define _POSIX_C_SOURCE 199309L

#include "sim_clock.h"
#include "link.h"
#include <string.h>
#include <time.h>

static bool g_realtime_enabled = false;
static InFlightPacket g_in_flight[SIM_INFLIGHT_MAX];
static struct timespec g_t0;
static bool g_t0_initialized = false;

static void ensure_t0(void)
{
    if (!g_t0_initialized) {
        clock_gettime(CLOCK_MONOTONIC, &g_t0);
        g_t0_initialized = true;
    }
}

void sim_clock_set_realtime(bool on)
{
    g_realtime_enabled = on;
    if (on) {
        ensure_t0();
    }
}

bool sim_clock_realtime_enabled(void)
{
    return g_realtime_enabled;
}

double sim_clock_now_ms(void)
{
    ensure_t0();
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double sec = (double)(now.tv_sec - g_t0.tv_sec);
    double nsec = (double)(now.tv_nsec - g_t0.tv_nsec);
    return sec * 1000.0 + nsec / 1e6;
}

static void parse_protocol_hint(InFlightPacket* p)
{
    p->ethertype = 0;
    p->l4_protocol = 0;
    if (p->len < 14) return;
    p->ethertype = ((uint16_t)p->bytes[12] << 8) | (uint16_t)p->bytes[13];
    if (p->ethertype == 0x0800 && p->len >= 14 + 10) {
        p->l4_protocol = p->bytes[14 + 9];
    }
}

int sim_clock_enqueue(Interface* sender, Interface* receiver, Link* link,
                      const uint8_t* bytes, size_t len)
{
    if (!g_realtime_enabled) return 0;
    if (sender == NULL || receiver == NULL || link == NULL || bytes == NULL) return 0;
    if (len == 0 || len > SIM_INFLIGHT_PAYLOAD_MAX) return 0;

    for (size_t i = 0; i < SIM_INFLIGHT_MAX; i++) {
        if (!g_in_flight[i].active) {
            InFlightPacket* p = &g_in_flight[i];
            p->active = true;
            p->sender = sender;
            p->receiver = receiver;
            p->link = link;
            p->launch_at_ms = sim_clock_now_ms();
            float delay = link->delay;
            if (delay < 0.0f) delay = 0.0f;
            p->deliver_at_ms = p->launch_at_ms + (double)delay;
            p->len = len;
            memcpy(p->bytes, bytes, len);
            parse_protocol_hint(p);
            return 1;
        }
    }
    return 0;
}

void sim_clock_tick(double now_ms)
{
    for (size_t i = 0; i < SIM_INFLIGHT_MAX; i++) {
        if (!g_in_flight[i].active) continue;
        if (g_in_flight[i].deliver_at_ms <= now_ms) {
            InFlightPacket pkt = g_in_flight[i];
            g_in_flight[i].active = false;
            receive(pkt.receiver, pkt.bytes, pkt.len);
        }
    }
}

void sim_clock_clear(void)
{
    for (size_t i = 0; i < SIM_INFLIGHT_MAX; i++) {
        g_in_flight[i].active = false;
    }
}

size_t sim_clock_in_flight_count(void)
{
    size_t n = 0;
    for (size_t i = 0; i < SIM_INFLIGHT_MAX; i++) {
        if (g_in_flight[i].active) n++;
    }
    return n;
}

const InFlightPacket* sim_clock_in_flight_at(size_t idx)
{
    size_t n = 0;
    for (size_t i = 0; i < SIM_INFLIGHT_MAX; i++) {
        if (g_in_flight[i].active) {
            if (n == idx) return &g_in_flight[i];
            n++;
        }
    }
    return NULL;
}
