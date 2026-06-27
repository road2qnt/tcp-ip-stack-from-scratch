#define _POSIX_C_SOURCE 199309L

#include "sim_clock.h"
#include "link.h"
#include <string.h>
#include <time.h>

static bool g_realtime_enabled = false;
static InFlightPacket g_in_flight[SIM_INFLIGHT_MAX];
static struct timespec g_t0;
static bool g_t0_initialized = false;

/* Pause / speed state. Sim time is reported as
 *   monotonic_ms_since_t0() - g_paused_accum_ms
 * while running. While paused it is frozen at g_paused_at_sim_ms. */
static double g_speed = 1.0;
static bool   g_paused = false;
static double g_paused_at_sim_ms = 0.0;
static double g_paused_accum_ms  = 0.0;

static void ensure_t0(void)
{
    if (!g_t0_initialized) {
        clock_gettime(CLOCK_MONOTONIC, &g_t0);
        g_t0_initialized = true;
    }
}

static double monotonic_ms_since_t0(void)
{
    ensure_t0();
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double sec = (double)(now.tv_sec - g_t0.tv_sec);
    double nsec = (double)(now.tv_nsec - g_t0.tv_nsec);
    return sec * 1000.0 + nsec / 1e6;
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

void sim_clock_set_speed(double factor)
{
    if (factor < 0.05) factor = 0.05;
    if (factor > 64.0) factor = 64.0;
    g_speed = factor;
}

double sim_clock_get_speed(void)
{
    return g_speed;
}

void sim_clock_set_paused(bool paused)
{
    if (paused && !g_paused) {
        /* Freeze the reported sim time at the current moment. */
        g_paused_at_sim_ms = sim_clock_now_ms();
        g_paused = true;
    } else if (!paused && g_paused) {
        /* On resume, shift the accumulator so that now_ms stays continuous. */
        double real_now = monotonic_ms_since_t0();
        g_paused_accum_ms = real_now - g_paused_at_sim_ms;
        g_paused = false;
    }
}

bool sim_clock_is_paused(void)
{
    return g_paused;
}

void sim_clock_step(double advance_ms)
{
    /* Step only does something while paused: bumps the frozen sim time. */
    if (g_paused && advance_ms > 0.0) {
        g_paused_at_sim_ms += advance_ms;
    }
}

double sim_clock_now_ms(void)
{
    if (g_paused) {
        return g_paused_at_sim_ms;
    }
    return monotonic_ms_since_t0() - g_paused_accum_ms;
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
            /* Faster speed shrinks effective delay so packets visibly fly faster. */
            double effective = (g_speed > 0.0) ? ((double)delay / g_speed) : (double)delay;
            p->deliver_at_ms = p->launch_at_ms + effective;
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
