#include "app.h"
#include "font.h"

#include "../layer2/host.h"
#include "../layer2/switch.h"
#include "../layer3/router.h"
#include "../core/sim_clock.h"
#include "../core/link.h"
#include "../utils/cli.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ======================== CONFIG ======================== */

#define WINDOW_TITLE  "Magi System - GUI"
#define WINDOW_W      1280
#define WINDOW_H      720
#define FONT_W        8
#define FONT_H_PX     12
#define CHAR_SPACE    1
#define LINE_SPACE    2
#define NODE_W        120
#define NODE_H        48
#define NODE_RADIUS   8
#define PANEL_W       320
#define LEGEND_X      10
#define FPS_CAP       30

#define LOG_H         200
#define INPUT_H       30
#define TOPO_H        (WINDOW_H - LOG_H - INPUT_H)

/* Toolbar (clickable playback controls) */
#define TOOLBAR_Y     24
#define TOOLBAR_H     32
#define TOPO_TOP_Y    (TOOLBAR_Y + TOOLBAR_H + 28)  /* below toolbar + legend */

/* Speed slider domain (log scale): 0.0625x .. 16x, default 1x */
#define SPEED_LOG_MIN -4.0
#define SPEED_LOG_MAX  4.0

/* Events panel (lives in lower half of the right panel) */
#define EVENT_RING_CAP   512
#define EVENT_SUMMARY_LEN 80
#define EVENT_ROW_H       28

#define LOG_LINES     400
#define LOG_LINE_LEN  256
#define INPUT_BUF_LEN 240

/* Colors as uint32_t RGBA */
#define COL_BG        0x1A1A2EFFu
#define COL_PANEL     0x16213EF0u
#define COL_HOST      0x4CAF50FFu
#define COL_SWITCH    0x2196F3FFu
#define COL_ROUTER    0xFF9800FFu
#define COL_LINK      0xAAAAAA80u
#define COL_LINK_HI   0x00FF88CCu
#define COL_TEXT      0xFFFFFFFFu
#define COL_TEXT_DIM  0x88AACCFFu
#define COL_SEL       0xFFD700FFu
#define COL_HOVER     0xFFFF0080u
#define COL_TITLE     0x00DDFFFFFF
#define COL_LEGEND    0x334455CCu

#define COL_PKT_ARP   0x55AAFFFFu
#define COL_PKT_ICMP  0xFF5555FFu
#define COL_PKT_TCP   0x66FF66FFu
#define COL_PKT_UDP   0xFFEE55FFu
#define COL_PKT_OTHER 0xCCCCCCFFu
#define COL_FLASH     0xFFFF66FFu
#define COL_LOG_BG    0x0A0F1FF0u
#define COL_LOG_TEXT  0xCCDDEEFFu
#define COL_LOG_BORDER 0x335577FFu
#define COL_INPUT_BG  0x101830FFu
#define COL_PROMPT    0x66FFAAFFu
#define COL_CARET     0xFFFFFFFFu

#define PKT_RADIUS    6
#define FLASH_MS      350.0

/* Extract RGBA components */
#define COL_R(c)  (((c) >> 24) & 0xFF)
#define COL_G(c)  (((c) >> 16) & 0xFF)
#define COL_B(c)  (((c) >> 8) & 0xFF)
#define COL_A(c)  ((c) & 0xFF)

/* ======================== STRUCTS ======================== */

typedef struct {
    int x, y;
    int w, h;
    int index;
    bool hovered;
} GNode;

typedef struct {
    int n1, n2;
    float delay;
    int port1, port2;
} GLink;

/* Toolbar widgets */
typedef enum {
    WIDGET_NONE = 0,
    WIDGET_BTN_PLAYPAUSE,
    WIDGET_BTN_STEP,
    WIDGET_BTN_RESET,
    WIDGET_SLIDER_SPEED,
} WidgetId;

typedef struct {
    SDL_Rect bounds;
    bool     hovered;
    bool     pressed;
} Widget;

/* Event log classifications. Entries are appended whenever a log line
 * is captured and classified; clicking one in the right-panel events list
 * flashes the involved node and pauses the sim (replay-style). */
typedef enum {
    EV_NONE = 0,
    EV_ARP_TX,
    EV_ARP_RX,
    EV_ICMP_REQ,
    EV_ICMP_REPLY,
    EV_FORWARD_L2,
    EV_FLOOD,
    EV_DROP,
    EV_ROUTE,
    EV_TCP,
    EV_DHCP,
    EV_DNS,
    EV_HTTP,
    EV_GENERIC,
} EventKind;

typedef struct {
    EventKind kind;
    double    sim_time_ms;
    int       log_idx;       /* slot index in log_lines ring at time of capture */
    int       node_idx;      /* primary involved node, or -1 */
    char      summary[EVENT_SUMMARY_LEN];
} EventEntry;

typedef struct {
    Simulator* sim;
    SDL_Window* window;
    SDL_Renderer* renderer;

    GNode gnodes[SIMULATOR_MAX_NODES];
    size_t gnode_count;
    GLink glinks[SIMULATOR_MAX_LINKS];
    size_t glink_count;

    int selected_idx;
    int hover_idx;
    bool running;
    bool needs_layout;

    double flash_at_ms[SIMULATOR_MAX_NODES];
    size_t prev_in_flight_count;

    char log_lines[LOG_LINES][LOG_LINE_LEN];
    int  log_head;
    int  log_count;
    int  log_scroll;
    char log_partial[LOG_LINE_LEN];
    size_t log_partial_len;

    char input_buf[INPUT_BUF_LEN];
    size_t input_len;

    char history[32][INPUT_BUF_LEN];
    int  history_count;
    int  history_idx;

    int  pipe_r;
    int  pipe_w;
    int  orig_stdout;
    int  orig_stderr;

    /* Toolbar widgets (hit boxes filled in render-time) */
    Widget   widget_play;
    Widget   widget_step;
    Widget   widget_reset;
    Widget   widget_slider;
    int      slider_dragging;   /* 1 while user holds the slider handle */
    WidgetId hover_widget;

    /* Event panel state */
    EventEntry events[EVENT_RING_CAP];
    int        events_head;
    int        events_count;
    int        selected_event;     /* absolute event id (across rolls), -1 if none */
    int        first_event_id;     /* absolute id of oldest entry currently in ring */
    int        events_scroll;      /* lines hidden from top */
    int        events_hover_row;   /* row hovered in current frame, -1 if none */
    SDL_Rect   events_panel_rect;  /* updated each render for hit testing */

    int      detail_scroll;        /* lines hidden from top of detail panel */
    SDL_Rect detail_panel_rect;    /* updated each render for hit testing */
} AppState;

/* ======================== FONT RENDERING ======================== */

static void draw_char(SDL_Renderer* r, int x, int y, uint8_t ch,
                      uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca)
{
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t* glyph = font8x12_basic[ch - 32];

    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    for (int row = 0; row < FONT_H_PX; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            if (bits & (0x80 >> col)) {
                SDL_RenderDrawPoint(r, x + col, y + row);
            }
        }
    }
}

static int draw_string(SDL_Renderer* r, int x, int y,
                       const char* str,
                       uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca)
{
    int ox = x;
    while (*str) {
        draw_char(r, x, y, (uint8_t)*str, cr, cg, cb, ca);
        x += FONT_W + CHAR_SPACE;
        str++;
    }
    return x - ox;
}

static int count_wrapped_lines(const char* str, int max_width)
{
    int lines = 1;
    int cx = 0;
    while (*str) {
        if (*str == '\n') {
            lines++;
            cx = 0;
            str++;
            continue;
        }
        int cw = FONT_W + CHAR_SPACE;
        if (cx + cw > max_width && cx > 0) {
            lines++;
            cx = 0;
        }
        cx += cw;
        str++;
    }
    return lines;
}

static int draw_string_wrapped(SDL_Renderer* r, int x, int y,
                               const char* str, int max_width,
                               uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca)
{
    int cx = x;
    int cy = y;
    int lines = 0;
    const char* p = str;

    while (*p) {
        if (*p == '\n') {
            cy += FONT_H_PX + LINE_SPACE;
            cx = x;
            lines++;
            p++;
            continue;
        }
        int cw = FONT_W + CHAR_SPACE;
        if (cx + cw > x + max_width && cx > x) {
            cy += FONT_H_PX + LINE_SPACE;
            cx = x;
            lines++;
        }
        draw_char(r, cx, cy, (uint8_t)*p, cr, cg, cb, ca);
        cx += cw;
        p++;
    }
    return lines + 1;
}

/* ======================== DRAWING HELPERS ======================== */

static void set_col(SDL_Renderer* r, uint32_t c)
{
    SDL_SetRenderDrawColor(r, COL_R(c), COL_G(c), COL_B(c), COL_A(c));
}

static void fill_rounded_rect(SDL_Renderer* r, int x, int y, int w, int h,
                              int rad, uint32_t color)
{
    set_col(r, color);

    if (rad <= 0) {
        SDL_Rect rect = {x, y, w, h};
        SDL_RenderFillRect(r, &rect);
        return;
    }

    /* Center rectangle */
    SDL_Rect center = {x + rad, y, w - 2 * rad, h};
    SDL_RenderFillRect(r, &center);

    /* Left vertical bar */
    SDL_Rect left = {x, y + rad, rad, h - 2 * rad};
    SDL_RenderFillRect(r, &left);

    /* Right vertical bar */
    SDL_Rect right = {x + w - rad, y + rad, rad, h - 2 * rad};
    SDL_RenderFillRect(r, &right);

    /* Four rounded corners */
    for (int dy = -rad; dy <= rad; dy++) {
        for (int dx = -rad; dx <= rad; dx++) {
            if (dx * dx + dy * dy <= rad * rad) {
                SDL_RenderDrawPoint(r, x + rad + dx, y + rad + dy);      /* TL */
                SDL_RenderDrawPoint(r, x + w - rad - 1 + dx, y + rad + dy);  /* TR */
                SDL_RenderDrawPoint(r, x + rad + dx, y + h - rad - 1 + dy);  /* BL */
                SDL_RenderDrawPoint(r, x + w - rad - 1 + dx, y + h - rad - 1 + dy); /* BR */
            }
        }
    }
}

static void draw_thick_line(SDL_Renderer* r, int x1, int y1, int x2, int y2,
                            uint32_t color)
{
    set_col(r, color);
    SDL_RenderDrawLine(r, x1, y1, x2, y2);
    SDL_RenderDrawLine(r, x1 + 1, y1, x2 + 1, y2);
    SDL_RenderDrawLine(r, x1 - 1, y1, x2 - 1, y2);
}

static void fill_circle(SDL_Renderer* r, int cx, int cy, int rad, uint32_t color)
{
    set_col(r, color);
    for (int dy = -rad; dy <= rad; dy++) {
        for (int dx = -rad; dx <= rad; dx++) {
            if (dx * dx + dy * dy <= rad * rad) {
                SDL_RenderDrawPoint(r, cx + dx, cy + dy);
            }
        }
    }
}

/* ======================== TOOLBAR + EVENTS HELPERS ======================== */

static bool point_in_rect(int x, int y, const SDL_Rect* r)
{
    return x >= r->x && x < r->x + r->w &&
           y >= r->y && y < r->y + r->h;
}

/* Map speed in (0.0625..16) -> ratio in [0,1] on a log2 axis. */
static double speed_to_ratio(double sp)
{
    if (sp <= 0.0) return 0.0;
    double t = log(sp) / log(2.0);
    if (t < SPEED_LOG_MIN) t = SPEED_LOG_MIN;
    if (t > SPEED_LOG_MAX) t = SPEED_LOG_MAX;
    return (t - SPEED_LOG_MIN) / (SPEED_LOG_MAX - SPEED_LOG_MIN);
}

static double ratio_to_speed(double r)
{
    if (r < 0.0) r = 0.0;
    if (r > 1.0) r = 1.0;
    double t = SPEED_LOG_MIN + r * (SPEED_LOG_MAX - SPEED_LOG_MIN);
    return pow(2.0, t);
}

/* Snap to a friendly preset speed if very close. */
static double snap_speed(double sp)
{
    static const double presets[] = {
        0.0625, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0
    };
    for (size_t i = 0; i < sizeof(presets)/sizeof(presets[0]); i++) {
        double diff = fabs(presets[i] - sp) / presets[i];
        if (diff < 0.08) return presets[i];
    }
    return sp;
}

static uint32_t event_color(EventKind k)
{
    switch (k) {
        case EV_ARP_TX:
        case EV_ARP_RX:      return COL_PKT_ARP;
        case EV_ICMP_REQ:
        case EV_ICMP_REPLY:  return COL_PKT_ICMP;
        case EV_FORWARD_L2:  return 0x88CCFFFFu;
        case EV_FLOOD:       return 0xFFCC66FFu;
        case EV_DROP:        return 0xFF6644FFu;
        case EV_ROUTE:       return COL_ROUTER;
        case EV_TCP:         return COL_PKT_TCP;
        case EV_DHCP:        return 0xCC99FFFFu;
        case EV_DNS:         return 0xFF99CCFFu;
        case EV_HTTP:        return 0x99FFCCFFu;
        default:             return COL_TEXT_DIM;
    }
}

static const char* event_short_label(EventKind k)
{
    switch (k) {
        case EV_ARP_TX:      return "ARP TX";
        case EV_ARP_RX:      return "ARP RX";
        case EV_ICMP_REQ:    return "ICMP>";
        case EV_ICMP_REPLY:  return "ICMP<";
        case EV_FORWARD_L2:  return "L2 FWD";
        case EV_FLOOD:       return "FLOOD";
        case EV_DROP:        return "DROP";
        case EV_ROUTE:       return "ROUTE";
        case EV_TCP:         return "TCP";
        case EV_DHCP:        return "DHCP";
        case EV_DNS:         return "DNS";
        case EV_HTTP:        return "HTTP";
        default:             return "INFO";
    }
}

static void render_button(SDL_Renderer* r, const Widget* w, const char* label,
                          uint32_t base, uint32_t hover, uint32_t active)
{
    uint32_t fill = base;
    if (w->pressed)      fill = active;
    else if (w->hovered) fill = hover;

    fill_rounded_rect(r, w->bounds.x, w->bounds.y, w->bounds.w, w->bounds.h, 4, fill);
    SDL_SetRenderDrawColor(r, 0xFF, 0xFF, 0xFF, 0x66);
    SDL_RenderDrawRect(r, &w->bounds);

    int pw = (int)strlen(label) * (FONT_W + CHAR_SPACE);
    int tx = w->bounds.x + (w->bounds.w - pw) / 2;
    int ty = w->bounds.y + (w->bounds.h - FONT_H_PX) / 2;
    draw_string(r, tx, ty, label, 0xFF, 0xFF, 0xFF, 0xFF);
}

static uint32_t pkt_color_for(uint16_t ethertype, uint8_t l4_protocol)
{
    if (ethertype == 0x0806) return COL_PKT_ARP;
    if (ethertype == 0x0800) {
        if (l4_protocol == 1) return COL_PKT_ICMP;
        if (l4_protocol == 6) return COL_PKT_TCP;
        if (l4_protocol == 17) return COL_PKT_UDP;
    }
    return COL_PKT_OTHER;
}

static const char* pkt_label_for(uint16_t ethertype, uint8_t l4_protocol)
{
    if (ethertype == 0x0806) return "ARP";
    if (ethertype == 0x0800) {
        if (l4_protocol == 1) return "ICMP";
        if (l4_protocol == 6) return "TCP";
        if (l4_protocol == 17) return "UDP";
        return "IP";
    }
    return "?";
}

/* ======================== LOG / INPUT / STDOUT CAPTURE ======================== */

static int find_node_by_name(AppState* s, const char* name)
{
    for (size_t i = 0; i < s->sim->node_count; i++) {
        if (strcmp(s->sim->nodes[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static EventKind classify_event(AppState* s, const char* line,
                                 int* node_out, char* summary, size_t cap)
{
    *node_out = -1;
    summary[0] = '\0';
    if (line == NULL || line[0] == '\0') return EV_NONE;

    const char* rest = line;
    if (line[0] == '[') {
        const char* end = strchr(line, ']');
        if (end != NULL && end - line < 32) {
            char name[32];
            size_t nlen = (size_t)(end - line - 1);
            if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
            memcpy(name, line + 1, nlen);
            name[nlen] = '\0';
            *node_out = find_node_by_name(s, name);
            rest = end + 1;
            while (*rest == ' ') rest++;
        }
    }

    EventKind kind = EV_GENERIC;
    if      (strstr(rest, "Sending ARP Request"))                kind = EV_ARP_TX;
    else if (strstr(rest, "ARP Request received"))               kind = EV_ARP_RX;
    else if (strstr(rest, "ARP Reply received"))                 kind = EV_ARP_RX;
    else if (strstr(rest, "ICMP Echo Request"))                  kind = EV_ICMP_REQ;
    else if (strstr(rest, "ICMP Echo Reply"))                    kind = EV_ICMP_REPLY;
    else if (strstr(line, "Reply from"))                         kind = EV_ICMP_REPLY;
    else if (strstr(rest, "Flooding"))                           kind = EV_FLOOD;
    else if (strstr(rest, "Dropped") || strstr(rest, "drop"))    kind = EV_DROP;
    else if (strstr(rest, "Forwarding frame"))                   kind = EV_FORWARD_L2;
    else if (strstr(rest, "Forwarding IPv4") ||
             strstr(rest, "Routing"))                            kind = EV_ROUTE;
    else if (strncmp(rest, "[TCP]", 5) == 0 ||
             strstr(rest, "TCP]") || strstr(rest, "Socket"))     kind = EV_TCP;
    else if (strstr(rest, "DHCP"))                               kind = EV_DHCP;
    else if (strstr(rest, "DNS"))                                kind = EV_DNS;
    else if (strstr(rest, "HTTP"))                               kind = EV_HTTP;

    snprintf(summary, cap, "%s", rest);
    size_t L = strlen(summary);
    while (L > 0 && (summary[L-1] == '\n' || summary[L-1] == '\r')) summary[--L] = '\0';
    return kind;
}

static void event_push(AppState* s, const char* line, int log_slot)
{
    int node_idx = -1;
    char summary[EVENT_SUMMARY_LEN];
    EventKind kind = classify_event(s, line, &node_idx, summary, sizeof(summary));
    if (kind == EV_NONE) return;

    if (kind == EV_GENERIC) {
        if (line[0] == '=' || strncmp(line, "magi>", 5) == 0 ||
            strstr(line, "MAGI SYSTEM") != NULL ||
            strstr(line, "Commands:") != NULL) {
            return;
        }
    }

    int idx = s->events_head;
    EventEntry* e = &s->events[idx];
    e->kind = kind;
    e->sim_time_ms = sim_clock_now_ms();
    e->log_idx = log_slot;
    e->node_idx = node_idx;
    snprintf(e->summary, EVENT_SUMMARY_LEN, "%s", summary);

    s->events_head = (s->events_head + 1) % EVENT_RING_CAP;
    if (s->events_count < EVENT_RING_CAP) {
        s->events_count++;
    } else {
        s->first_event_id++;
    }
}

static void log_append_line(AppState* s, const char* line)
{
    int idx = s->log_head;
    snprintf(s->log_lines[idx], LOG_LINE_LEN, "%s", line);
    s->log_head = (s->log_head + 1) % LOG_LINES;
    if (s->log_count < LOG_LINES) s->log_count++;
    event_push(s, line, idx);
}

static void log_append_chars(AppState* s, const char* buf, ssize_t n)
{
    for (ssize_t i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n') {
            s->log_partial[s->log_partial_len] = '\0';
            log_append_line(s, s->log_partial);
            s->log_partial_len = 0;
        } else if (c == '\r') {
            continue;
        } else if (c == '\t') {
            for (int t = 0; t < 4 && s->log_partial_len < LOG_LINE_LEN - 1; t++) {
                s->log_partial[s->log_partial_len++] = ' ';
            }
        } else if (s->log_partial_len < LOG_LINE_LEN - 1) {
            s->log_partial[s->log_partial_len++] = c;
        } else {
            s->log_partial[s->log_partial_len] = '\0';
            log_append_line(s, s->log_partial);
            s->log_partial_len = 0;
            s->log_partial[s->log_partial_len++] = c;
        }
    }
}

static void capture_stdout_init(AppState* s)
{
    int p[2];
    s->pipe_r = s->pipe_w = -1;
    s->orig_stdout = -1;
    s->orig_stderr = -1;
    if (pipe(p) < 0) return;
    s->pipe_r = p[0];
    s->pipe_w = p[1];
    fcntl(s->pipe_r, F_SETFL, O_NONBLOCK);
    s->orig_stdout = dup(STDOUT_FILENO);
    s->orig_stderr = dup(STDERR_FILENO);
    dup2(s->pipe_w, STDOUT_FILENO);
    dup2(s->pipe_w, STDERR_FILENO);
    setvbuf(stdout, NULL, _IOLBF, 4096);
    setvbuf(stderr, NULL, _IOLBF, 4096);
}

static void capture_stdout_drain(AppState* s)
{
    if (s->pipe_r < 0) return;
    fflush(stdout);
    fflush(stderr);
    char buf[4096];
    while (1) {
        ssize_t n = read(s->pipe_r, buf, sizeof(buf));
        if (n <= 0) break;
        log_append_chars(s, buf, n);
        if (s->orig_stdout >= 0) {
            (void)!write(s->orig_stdout, buf, (size_t)n);
        }
    }
}

static void capture_stdout_restore(AppState* s)
{
    fflush(stdout);
    fflush(stderr);
    if (s->orig_stdout >= 0) {
        dup2(s->orig_stdout, STDOUT_FILENO);
        close(s->orig_stdout);
        s->orig_stdout = -1;
    }
    if (s->orig_stderr >= 0) {
        dup2(s->orig_stderr, STDERR_FILENO);
        close(s->orig_stderr);
        s->orig_stderr = -1;
    }
    if (s->pipe_r >= 0) { close(s->pipe_r); s->pipe_r = -1; }
    if (s->pipe_w >= 0) { close(s->pipe_w); s->pipe_w = -1; }
}

static void history_push(AppState* s, const char* line)
{
    if (line[0] == '\0') return;
    int slot = s->history_count % 32;
    snprintf(s->history[slot], INPUT_BUF_LEN, "%s", line);
    s->history_count++;
    s->history_idx = s->history_count;
}

static void history_recall(AppState* s, int delta)
{
    if (s->history_count == 0) return;
    int new_idx = s->history_idx + delta;
    int oldest = (s->history_count > 32) ? (s->history_count - 32) : 0;
    if (new_idx < oldest) new_idx = oldest;
    if (new_idx > s->history_count) new_idx = s->history_count;
    s->history_idx = new_idx;
    if (new_idx == s->history_count) {
        s->input_buf[0] = '\0';
        s->input_len = 0;
    } else {
        snprintf(s->input_buf, INPUT_BUF_LEN, "%s", s->history[new_idx % 32]);
        s->input_len = strlen(s->input_buf);
    }
}

static void cli_dispatch(AppState* s)
{
    if (s->input_len == 0) return;
    history_push(s, s->input_buf);

    char prompt_line[INPUT_BUF_LEN + 8];
    snprintf(prompt_line, sizeof(prompt_line), "magi> %s", s->input_buf);
    log_append_line(s, prompt_line);

    char cmd[INPUT_BUF_LEN];
    snprintf(cmd, sizeof(cmd), "%s", s->input_buf);

    bool keep_running = process(cmd);
    if (!keep_running) {
        s->running = false;
    }

    capture_stdout_drain(s);

    s->needs_layout = true;
    s->input_buf[0] = '\0';
    s->input_len = 0;
    s->log_scroll = 0;
}

/* ======================== LAYOUT ENGINE ======================== */

static void layout_nodes(AppState* state)
{
    Simulator* sim = state->sim;
    if (sim->node_count == 0) return;

    /* Populate gnodes */
    state->gnode_count = sim->node_count;
    for (size_t i = 0; i < sim->node_count; i++) {
        state->gnodes[i].index = (int)i;
        state->gnodes[i].w = NODE_W;
        state->gnodes[i].h = NODE_H;
    }

    /* Build glinks */
    state->glink_count = 0;
    for (size_t i = 0; i < sim->link_count; i++) {
        const Link* link = &sim->links[i];
        if (!link->interface1 || !link->interface2) continue;

        int n1 = -1, n2 = -1;
        int p1 = link->interface1->portNumber;
        int p2 = link->interface2->portNumber;

        for (size_t j = 0; j < sim->node_count; j++) {
            if (sim->nodes[j].node == link->interface1->node) n1 = (int)j;
            if (sim->nodes[j].node == link->interface2->node) n2 = (int)j;
        }

        if (n1 >= 0 && n2 >= 0 && state->glink_count < SIMULATOR_MAX_LINKS) {
            state->glinks[state->glink_count].n1 = n1;
            state->glinks[state->glink_count].n2 = n2;
            state->glinks[state->glink_count].delay = link->delay;
            state->glinks[state->glink_count].port1 = p1;
            state->glinks[state->glink_count].port2 = p2;
            state->glink_count++;
        }
    }

    /* Count connections per node */
    size_t degree[SIMULATOR_MAX_NODES] = {0};
    for (size_t i = 0; i < state->glink_count; i++) {
        degree[state->glinks[i].n1]++;
        degree[state->glinks[i].n2]++;
    }

    /* Find max degree node (hub/center) */
    size_t center_idx = 0;
    size_t max_deg = 0;
    for (size_t i = 0; i < state->gnode_count; i++) {
        if (degree[i] > max_deg) {
            max_deg = degree[i];
            center_idx = i;
        }
    }

    int cx = (WINDOW_W - PANEL_W) / 2;
    int cy = TOPO_H / 2 + 40;
    int radius = (state->gnode_count <= 1) ? 0 :
                 (int)(fmin(WINDOW_W - PANEL_W, TOPO_H) * 0.32);
    if (radius < 80) radius = 80;
    if (radius > 280) radius = 280;

    /* Place center node */
    state->gnodes[center_idx].x = cx;
    state->gnodes[center_idx].y = cy;

    /* Place others in a circle */
    int placed = 1;
    double angle_step = (state->gnode_count > 1)
        ? (2.0 * M_PI) / (state->gnode_count - 1) : 0.0;

    for (size_t i = 0; i < state->gnode_count; i++) {
        if (i == center_idx) continue;
        double a = -M_PI / 2.0 + angle_step * placed;
        int nx = cx + (int)(radius * cos(a));
        int ny = cy + (int)(radius * sin(a));

        if (nx + NODE_W / 2 > WINDOW_W - PANEL_W - 20)
            nx = WINDOW_W - PANEL_W - 20 - NODE_W / 2;
        if (nx - NODE_W / 2 < 10) nx = 10 + NODE_W / 2;
        if (ny + NODE_H / 2 > TOPO_H - 10) ny = TOPO_H - 10 - NODE_H / 2;
        if (ny - NODE_H / 2 < 50) ny = 50 + NODE_H / 2;

        state->gnodes[i].x = nx;
        state->gnodes[i].y = ny;
        placed++;
    }

    state->needs_layout = false;
}

static int node_idx_for_interface(AppState* state, const Interface* iface)
{
    if (iface == NULL || iface->node == NULL) return -1;
    for (size_t i = 0; i < state->sim->node_count; i++) {
        if (state->sim->nodes[i].node == iface->node) return (int)i;
    }
    return -1;
}

/* ======================== HIT TESTING ======================== */

static int hit_test_node(AppState* state, int mx, int my)
{
    for (size_t i = 0; i < state->gnode_count; i++) {
        const GNode* n = &state->gnodes[i];
        int hw = n->w / 2, hh = n->h / 2;
        if (mx >= n->x - hw && mx <= n->x + hw &&
            my >= n->y - hh && my <= n->y + hh) {
            return (int)i;
        }
    }
    return -1;
}

/* ======================== NODE INFO ======================== */

static uint32_t node_color(SimulatorNodeType type)
{
    switch (type) {
        case SIM_NODE_HOST:   return COL_HOST;
        case SIM_NODE_SWITCH: return COL_SWITCH;
        case SIM_NODE_ROUTER: return COL_ROUTER;
        default:              return 0x888888FFu;
    }
}

static const char* node_type_label(SimulatorNodeType type)
{
    switch (type) {
        case SIM_NODE_HOST:   return "Host";
        case SIM_NODE_SWITCH: return "Switch";
        case SIM_NODE_ROUTER: return "Router";
        default:              return "Unknown";
    }
}

static void append(char* buf, size_t bufsz, const char* fmt, ...)
{
    size_t cur = strlen(buf);
    if (cur >= bufsz - 1) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf + cur, bufsz - cur, fmt, args);
    va_end(args);
}

static const char* get_node_detail(AppState* state, int node_idx)
{
    static char buf[8192];
    buf[0] = '\0';

    if (node_idx < 0 || node_idx >= (int)state->sim->node_count) return "";

    const SimulatorNodeEntry* entry = &state->sim->nodes[node_idx];
    const Node* node = entry->node;

    append(buf, sizeof(buf), "Name: %s\n", entry->name);
    append(buf, sizeof(buf), "Type: %s\n", node_type_label(entry->type));
    append(buf, sizeof(buf), "Interfaces: %d\n", node->NUM_INTERFACES);

    for (int i = 0; i < node->NUM_INTERFACES; i++) {
        const Interface* iface = &node->interfaces[i];
        append(buf, sizeof(buf),
               "  Port %d: %02X:%02X:%02X:%02X:%02X:%02X %s\n",
               i + 1,
               iface->Mac_Address.bytes[0], iface->Mac_Address.bytes[1],
               iface->Mac_Address.bytes[2], iface->Mac_Address.bytes[3],
               iface->Mac_Address.bytes[4], iface->Mac_Address.bytes[5],
               iface->link ? "[UP]" : "[DOWN]");
    }

    switch (entry->type) {
        case SIM_NODE_HOST: {
            const Host* h = (const Host*)node;
            if (h->has_ip) {
                append(buf, sizeof(buf),
                       "IP: %d.%d.%d.%d/%d\n",
                       h->ip_address.octet[0], h->ip_address.octet[1],
                       h->ip_address.octet[2], h->ip_address.octet[3],
                       h->ip_address.prefix);
                append(buf, sizeof(buf),
                       "GW: %d.%d.%d.%d\n",
                       h->default_gateway.octet[0], h->default_gateway.octet[1],
                       h->default_gateway.octet[2], h->default_gateway.octet[3]);
            }
            append(buf, sizeof(buf), "ARP cache: %zu entries\n", h->arp_table.size);
            break;
        }
        case SIM_NODE_SWITCH: {
            const Switch* sw = (const Switch*)node;
            append(buf, sizeof(buf), "MAC table: %zu entries\n", sw->mac_table.size);
            for (int i = 0; i < sw->base.NUM_INTERFACES; i++) {
                if (sw->port_configs[i].mode == SWITCH_PORT_TRUNK ||
                    sw->port_configs[i].vlan_id != SWITCH_DEFAULT_VLAN_ID) {
                    append(buf, sizeof(buf),
                           "  Port %d: %s (VLAN %d)\n",
                           i + 1,
                           sw->port_configs[i].mode == SWITCH_PORT_TRUNK ? "trunk" : "access",
                           sw->port_configs[i].vlan_id);
                }
            }
            break;
        }
        case SIM_NODE_ROUTER: {
            const Router* rt = (const Router*)node;
            append(buf, sizeof(buf), "Routes: %zu\n", rt->route_count);
            append(buf, sizeof(buf), "ARP table: %zu entries\n", rt->arp_table.size);
            for (size_t i = 0; i < rt->route_count; i++) {
                const RoutingTableEntry* rte = &rt->routing_table[i];
                append(buf, sizeof(buf),
                       "  %d.%d.%d.%d/%d -> %d.%d.%d.%d (if%d)\n",
                       rte->destination.octet[0], rte->destination.octet[1],
                       rte->destination.octet[2], rte->destination.octet[3],
                       rte->destination.prefix,
                       rte->next_hop.octet[0], rte->next_hop.octet[1],
                       rte->next_hop.octet[2], rte->next_hop.octet[3],
                       rte->out_interface);
            }
            break;
        }
    }

    return buf;
}

/* ======================== RENDER ======================== */

static void render(AppState* state)
{
    SDL_Renderer* r = state->renderer;

    set_col(r, COL_BG);
    SDL_RenderClear(r);

    draw_string(r, 12, 6, "MAGI SYSTEM - GUI",
                COL_R(COL_TITLE), COL_G(COL_TITLE), COL_B(COL_TITLE), COL_A(COL_TITLE));

    if (state->sim->node_count == 0) {
        draw_string(r, (WINDOW_W - PANEL_W) / 2 - 140, WINDOW_H / 2,
                    "No topology loaded. Use 'load <file>' in CLI first.",
                    0xFF, 0x66, 0x66, 0xFF);
        SDL_RenderPresent(r);
        return;
    }

    if (state->needs_layout) layout_nodes(state);

    /* ---- Links ---- */
    for (size_t i = 0; i < state->glink_count; i++) {
        const GLink* gl = &state->glinks[i];
        int x1 = state->gnodes[gl->n1].x;
        int y1 = state->gnodes[gl->n1].y;
        int x2 = state->gnodes[gl->n2].x;
        int y2 = state->gnodes[gl->n2].y;

        bool hl = (state->selected_idx >= 0 &&
                   (gl->n1 == state->selected_idx || gl->n2 == state->selected_idx));
        draw_thick_line(r, x1, y1, x2, y2, hl ? COL_LINK_HI : COL_LINK);

        /* Delay label at midpoint */
        char delay_str[16];
        snprintf(delay_str, sizeof(delay_str), "%.0fms", gl->delay);
        draw_string(r, (x1 + x2) / 2 + 6, (y1 + y2) / 2 - 6, delay_str,
                    0x44, 0xCC, 0x88, 0xCC);
    }

    /* ---- Nodes ---- */
    double now_ms = sim_clock_now_ms();
    for (size_t i = 0; i < state->gnode_count; i++) {
        const GNode* gn = &state->gnodes[i];
        int x = gn->x - gn->w / 2, y = gn->y - gn->h / 2;
        uint32_t col = node_color(state->sim->nodes[gn->index].type);
        bool is_sel = (state->selected_idx == (int)i);
        bool is_hov = (state->hover_idx == (int)i);

        double age = now_ms - state->flash_at_ms[i];
        bool is_flash = (state->flash_at_ms[i] > 0.0 && age >= 0.0 && age <= FLASH_MS);

        /* Selection/hover border */
        if (is_sel)
            fill_rounded_rect(r, x - 3, y - 3, gn->w + 6, gn->h + 6,
                              NODE_RADIUS + 2, COL_SEL);
        else if (is_flash)
            fill_rounded_rect(r, x - 4, y - 4, gn->w + 8, gn->h + 8,
                              NODE_RADIUS + 3, COL_FLASH);
        else if (is_hov)
            fill_rounded_rect(r, x - 2, y - 2, gn->w + 4, gn->h + 4,
                              NODE_RADIUS + 1, COL_HOVER);

        /* Node body */
        fill_rounded_rect(r, x, y, gn->w, gn->h, NODE_RADIUS, col);

        /* Node name centered */
        const char* name = state->sim->nodes[gn->index].name;
        int name_pw = (int)strlen(name) * (FONT_W + CHAR_SPACE);
        draw_string(r, gn->x - name_pw / 2, gn->y - FONT_H_PX / 2,
                    name, 0xFF, 0xFF, 0xFF, 0xFF);

        /* Small type label */
        const char* tl = node_type_label(state->sim->nodes[gn->index].type);
        int tl_pw = (int)strlen(tl) * (FONT_W + CHAR_SPACE);
        draw_string(r, gn->x - tl_pw / 2, gn->y + FONT_H_PX / 2 + 2,
                    tl, 0x00, 0x00, 0x00, 0xAA);
    }

    /* ---- In-flight packets ---- */
    size_t in_flight = sim_clock_in_flight_count();
    for (size_t i = 0; i < in_flight; i++) {
        const InFlightPacket* p = sim_clock_in_flight_at(i);
        if (p == NULL) break;
        int sn = node_idx_for_interface(state, p->sender);
        int rn = node_idx_for_interface(state, p->receiver);
        if (sn < 0 || rn < 0) continue;
        double total = p->deliver_at_ms - p->launch_at_ms;
        double t = (total > 0.0) ? (now_ms - p->launch_at_ms) / total : 1.0;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        int x1 = state->gnodes[sn].x;
        int y1 = state->gnodes[sn].y;
        int x2 = state->gnodes[rn].x;
        int y2 = state->gnodes[rn].y;
        int px = x1 + (int)((double)(x2 - x1) * t);
        int py = y1 + (int)((double)(y2 - y1) * t);
        uint32_t pc = pkt_color_for(p->ethertype, p->l4_protocol);
        fill_circle(r, px, py, PKT_RADIUS + 2, 0x000000AAu);
        fill_circle(r, px, py, PKT_RADIUS, pc);
        const char* lbl = pkt_label_for(p->ethertype, p->l4_protocol);
        int lbl_w = (int)strlen(lbl) * (FONT_W + CHAR_SPACE);
        draw_string(r, px - lbl_w / 2, py - PKT_RADIUS - FONT_H_PX - 2, lbl,
                    COL_R(pc), COL_G(pc), COL_B(pc), 0xFF);
    }

    /* ---- Toolbar: Play/Pause, Step, Reset, Speed slider ---- */
    {
        bool ps = sim_clock_is_paused();
        double sp = sim_clock_get_speed();
        int tx = LEGEND_X;
        int ty = TOOLBAR_Y;
        int th = TOOLBAR_H;

        SDL_Rect strip = { 0, ty - 4, WINDOW_W - PANEL_W, th + 8 };
        set_col(r, 0x101A2EFFu);
        SDL_RenderFillRect(r, &strip);
        set_col(r, 0x223355FFu);
        SDL_RenderDrawLine(r, 0, ty + th + 4, WINDOW_W - PANEL_W, ty + th + 4);

        state->widget_play.bounds = (SDL_Rect){ tx, ty, 96, th };
        render_button(r, &state->widget_play,
                      ps ? "PLAY >" : "PAUSE ||",
                      ps ? 0x115533FFu : 0x553311FFu,
                      ps ? 0x227755FFu : 0x885533FFu,
                      ps ? 0x33AA77FFu : 0xAA6644FFu);
        tx += 104;

        state->widget_step.bounds = (SDL_Rect){ tx, ty, 76, th };
        render_button(r, &state->widget_step, "STEP >|",
                      0x223355FFu, 0x335577FFu, 0x4477AAFFu);
        tx += 84;

        state->widget_reset.bounds = (SDL_Rect){ tx, ty, 72, th };
        render_button(r, &state->widget_reset, "RESET",
                      0x332244FFu, 0x553366FFu, 0x774488FFu);
        tx += 80;

        int slider_x = tx + 60;
        int slider_w = 200;
        int slider_y = ty + th / 2 - 2;
        int slider_h = 4;
        state->widget_slider.bounds = (SDL_Rect){
            slider_x - 8, ty + 6, slider_w + 16, th - 12
        };

        draw_string(r, tx, ty + (th - FONT_H_PX) / 2, "Speed:",
                    0xCC, 0xDD, 0xEE, 0xFF);

        SDL_Rect track = { slider_x, slider_y, slider_w, slider_h };
        set_col(r, 0x223355FFu);
        SDL_RenderFillRect(r, &track);
        set_col(r, 0x556699FFu);
        SDL_RenderDrawRect(r, &track);

        for (double mark = -2.0; mark <= 2.0; mark += 2.0) {
            double rt = (mark - SPEED_LOG_MIN) / (SPEED_LOG_MAX - SPEED_LOG_MIN);
            int mx = slider_x + (int)(rt * slider_w);
            set_col(r, 0x6688AAFFu);
            SDL_RenderDrawLine(r, mx, slider_y - 4, mx, slider_y + slider_h + 4);
        }

        double r_now = speed_to_ratio(sp);
        int hx = slider_x + (int)(r_now * slider_w);
        SDL_Rect handle = { hx - 5, ty + 4, 10, th - 8 };
        uint32_t hcol = state->slider_dragging ? 0xFFDD66FFu :
                       (state->hover_widget == WIDGET_SLIDER_SPEED ? 0xCCAA44FFu : 0x99AA55FFu);
        fill_rounded_rect(r, handle.x, handle.y, handle.w, handle.h, 2, hcol);
        set_col(r, 0xFFFFFFAAu);
        SDL_RenderDrawRect(r, &handle);

        char sps[24];
        if (sp >= 1.0) snprintf(sps, sizeof(sps), "%.0fx", sp);
        else           snprintf(sps, sizeof(sps), "%.3gx", sp);
        draw_string(r, slider_x + slider_w + 12, ty + (th - FONT_H_PX) / 2,
                    sps, 0xFF, 0xEE, 0x99, 0xFF);

        const char* badge = ps ? "[ PAUSED ]" : "[ LIVE ]";
        int bw = (int)strlen(badge) * (FONT_W + CHAR_SPACE);
        int bx = WINDOW_W - PANEL_W - bw - 12;
        int by = ty + (th - FONT_H_PX) / 2;
        SDL_Rect bbg = { bx - 6, by - 3, bw + 12, FONT_H_PX + 6 };
        set_col(r, ps ? 0x553322F0u : 0x114433E0u);
        SDL_RenderFillRect(r, &bbg);
        set_col(r, ps ? 0xFFAA66FFu : 0x66FFAAFFu);
        SDL_RenderDrawRect(r, &bbg);
        draw_string(r, bx, by, badge,
                    ps ? 0xFF : 0x66, ps ? 0xAA : 0xFF, ps ? 0x66 : 0xAA, 0xFF);
    }

    /* ---- Legend (compact horizontal strip) ---- */
    {
        int lx = LEGEND_X;
        int ly = TOOLBAR_Y + TOOLBAR_H + 10;
        int lw = 540;
        int lh = 18;

        SDL_Rect bg = {lx - 4, ly - 3, lw, lh + 6};
        set_col(r, COL_LEGEND);
        SDL_RenderFillRect(r, &bg);

        int x = lx;
        fill_rounded_rect(r, x, ly + 3, 10, 10, 2, COL_HOST);
        draw_string(r, x + 14, ly + 3, "Host", 0xCC, 0xFF, 0xCC, 0xFF);
        x += 60;

        fill_rounded_rect(r, x, ly + 3, 10, 10, 2, COL_SWITCH);
        draw_string(r, x + 14, ly + 3, "Switch", 0xCC, 0xDD, 0xFF, 0xFF);
        x += 70;

        fill_rounded_rect(r, x, ly + 3, 10, 10, 2, COL_ROUTER);
        draw_string(r, x + 14, ly + 3, "Router", 0xFF, 0xDD, 0xAA, 0xFF);
        x += 80;

        draw_string(r, x, ly + 3, "|", 0x66, 0x88, 0xAA, 0xFF);
        x += 16;

        fill_circle(r, x + 5, ly + 8, 5, COL_PKT_ARP);
        draw_string(r, x + 14, ly + 3, "ARP", 0xCC, 0xDD, 0xFF, 0xFF);
        x += 56;

        fill_circle(r, x + 5, ly + 8, 5, COL_PKT_ICMP);
        draw_string(r, x + 14, ly + 3, "ICMP", 0xFF, 0xCC, 0xCC, 0xFF);
        x += 64;

        fill_circle(r, x + 5, ly + 8, 5, COL_PKT_TCP);
        draw_string(r, x + 14, ly + 3, "TCP", 0xCC, 0xFF, 0xCC, 0xFF);
        x += 56;

        fill_circle(r, x + 5, ly + 8, 5, COL_PKT_UDP);
        draw_string(r, x + 14, ly + 3, "UDP", 0xFF, 0xFF, 0xCC, 0xFF);
    }

    /* ---- Right Panel: detail (top) + events list (bottom) ---- */
    {
        int px = WINDOW_W - PANEL_W;
        int pw = PANEL_W;
        int ph = WINDOW_H;

        SDL_Rect panel_bg = {px, 0, pw, ph};
        set_col(r, COL_PANEL);
        SDL_RenderFillRect(r, &panel_bg);
        set_col(r, 0x335577FFu);
        SDL_RenderDrawLine(r, px, 0, px, ph);

        int detail_y = 8;
        int detail_h = 264;

        if (state->selected_idx >= 0) {
            const SimulatorNodeEntry* entry = &state->sim->nodes[state->selected_idx];
            char ptitle[64];
            snprintf(ptitle, sizeof(ptitle), " %s [%s]", entry->name,
                     node_type_label(entry->type));
            draw_string(r, px + 8, detail_y, ptitle,
                        COL_R(COL_TITLE), COL_G(COL_TITLE),
                        COL_B(COL_TITLE), COL_A(COL_TITLE));
            set_col(r, 0x446688FFu);
            SDL_RenderDrawLine(r, px + 8, detail_y + FONT_H_PX + 4,
                               px + pw - 8, detail_y + FONT_H_PX + 4);

            const char* detail = get_node_detail(state, state->selected_idx);
            int line_h = FONT_H_PX + LINE_SPACE;
            int content_y0 = detail_y + FONT_H_PX + 12;
            int content_h  = detail_h - (FONT_H_PX + 12);

            int total_lines = count_wrapped_lines(detail, pw - 20);
            int rows_visible = content_h / line_h;
            if (rows_visible < 1) rows_visible = 1;
            int max_scroll = total_lines - rows_visible;
            if (max_scroll < 0) max_scroll = 0;
            if (state->detail_scroll > max_scroll) state->detail_scroll = max_scroll;
            if (state->detail_scroll < 0) state->detail_scroll = 0;

            SDL_Rect content_rect = { px + 6, content_y0, pw - 12, content_h };
            state->detail_panel_rect = content_rect;
            SDL_RenderSetClipRect(r, &content_rect);
            draw_string_wrapped(r, px + 8,
                                content_y0 - state->detail_scroll * line_h,
                                detail, pw - 20,
                                0xCC, 0xDD, 0xEE, 0xFF);
            SDL_RenderSetClipRect(r, NULL);

            if (total_lines > rows_visible) {
                int bar_x = px + pw - 6;
                int bar_y = content_y0;
                int bar_h = rows_visible * line_h;
                SDL_Rect track = {bar_x, bar_y, 3, bar_h};
                set_col(r, 0x223344FFu);
                SDL_RenderFillRect(r, &track);

                double frac_vis = (double)rows_visible / total_lines;
                double frac_off = (double)state->detail_scroll / total_lines;
                int handle_y = bar_y + (int)(frac_off * bar_h);
                int handle_h = (int)(frac_vis * bar_h);
                if (handle_h < 12) handle_h = 12;
                set_col(r, 0x778899FFu);
                SDL_Rect handle = {bar_x, handle_y, 3, handle_h};
                SDL_RenderFillRect(r, &handle);
            }
        } else {
            state->detail_panel_rect = (SDL_Rect){0, 0, 0, 0};
            draw_string(r, px + 8, detail_y, " NODE DETAIL",
                        COL_R(COL_TITLE), COL_G(COL_TITLE),
                        COL_B(COL_TITLE), COL_A(COL_TITLE));
            set_col(r, 0x446688FFu);
            SDL_RenderDrawLine(r, px + 8, detail_y + FONT_H_PX + 4,
                               px + pw - 8, detail_y + FONT_H_PX + 4);
            draw_string(r, px + 12, detail_y + FONT_H_PX + 14,
                        "Click a node to inspect.",
                        COL_R(COL_TEXT_DIM), COL_G(COL_TEXT_DIM),
                        COL_B(COL_TEXT_DIM), 0xFF);
            char counts[64];
            snprintf(counts, sizeof(counts), "Nodes: %zu   Links: %zu",
                     state->sim->node_count, state->sim->link_count);
            draw_string(r, px + 12, detail_y + FONT_H_PX + 36, counts,
                        0xBB, 0xDD, 0xEE, 0xFF);
        }

        int ev_y = detail_y + detail_h + 4;
        int ev_h = ph - ev_y - 4;
        SDL_Rect ev_bg = {px + 4, ev_y, pw - 8, ev_h};
        set_col(r, 0x0A1024FFu);
        SDL_RenderFillRect(r, &ev_bg);
        set_col(r, 0x446688FFu);
        SDL_RenderDrawRect(r, &ev_bg);

        int total_seen = state->events_count + state->first_event_id;
        char hdr[80];
        snprintf(hdr, sizeof(hdr), " EVENTS  (%d shown / %d total)",
                 state->events_count, total_seen);
        draw_string(r, ev_bg.x + 6, ev_bg.y + 4, hdr,
                    COL_R(COL_TITLE), COL_G(COL_TITLE), COL_B(COL_TITLE), 0xFF);
        set_col(r, 0x446688FFu);
        SDL_RenderDrawLine(r, ev_bg.x + 4, ev_bg.y + 18,
                           ev_bg.x + ev_bg.w - 4, ev_bg.y + 18);

        int list_x = ev_bg.x + 4;
        int list_y = ev_bg.y + 22;
        int list_w = ev_bg.w - 14;
        int list_h = ev_bg.h - 26;
        int rows_visible = list_h / EVENT_ROW_H;
        if (rows_visible < 1) rows_visible = 1;

        int max_scroll = state->events_count - rows_visible;
        if (max_scroll < 0) max_scroll = 0;
        if (state->events_scroll > max_scroll) state->events_scroll = max_scroll;
        if (state->events_scroll < 0) state->events_scroll = 0;

        state->events_panel_rect = (SDL_Rect){
            list_x, list_y, list_w, rows_visible * EVENT_ROW_H
        };

        int n_to_draw = state->events_count - state->events_scroll;
        if (n_to_draw > rows_visible) n_to_draw = rows_visible;

        for (int row = 0; row < n_to_draw; row++) {
            int rev = state->events_scroll + row;
            int idx = (state->events_head - 1 - rev + EVENT_RING_CAP * 2)
                      % EVENT_RING_CAP;
            EventEntry* e = &state->events[idx];
            int abs_id = state->first_event_id + state->events_count - 1 - rev;

            SDL_Rect rr = {list_x, list_y + row * EVENT_ROW_H,
                           list_w, EVENT_ROW_H - 2};
            bool sel = (abs_id == state->selected_event);
            bool hov = (row == state->events_hover_row);
            uint32_t rc = sel ? 0x336699FFu :
                          (hov ? 0x223355FFu : 0x14182CFFu);
            fill_rounded_rect(r, rr.x, rr.y, rr.w, rr.h, 3, rc);

            uint32_t ecol = event_color(e->kind);
            fill_circle(r, rr.x + 8, rr.y + 7, 3, ecol);

            char tbuf[16];
            if (e->sim_time_ms >= 1000.0)
                snprintf(tbuf, sizeof(tbuf), "%.2fs", e->sim_time_ms / 1000.0);
            else
                snprintf(tbuf, sizeof(tbuf), "%.0fms", e->sim_time_ms);
            draw_string(r, rr.x + 16, rr.y + 2, tbuf, 0xCC, 0xDD, 0xEE, 0xFF);

            draw_string(r, rr.x + 70, rr.y + 2, event_short_label(e->kind),
                        COL_R(ecol), COL_G(ecol), COL_B(ecol), 0xFF);

            if (e->node_idx >= 0 && (size_t)e->node_idx < state->sim->node_count) {
                draw_string(r, rr.x + 130, rr.y + 2,
                            state->sim->nodes[e->node_idx].name,
                            0xFF, 0xEE, 0x99, 0xFF);
            }

            int max_chars = (rr.w - 16) / (FONT_W + CHAR_SPACE);
            char trim[EVENT_SUMMARY_LEN];
            snprintf(trim, sizeof(trim), "%s", e->summary);
            if ((int)strlen(trim) > max_chars - 1 && max_chars >= 4)
                trim[max_chars - 1] = '\0';
            draw_string(r, rr.x + 8, rr.y + 14, trim, 0xAA, 0xBB, 0xCC, 0xFF);
        }

        if (state->events_count > rows_visible) {
            int bar_x = ev_bg.x + ev_bg.w - 8;
            int bar_y = list_y;
            int bar_h = rows_visible * EVENT_ROW_H;
            SDL_Rect track = {bar_x, bar_y, 4, bar_h};
            set_col(r, 0x223344FFu);
            SDL_RenderFillRect(r, &track);

            double frac_vis = (double)rows_visible / state->events_count;
            double frac_off = (double)state->events_scroll / state->events_count;
            int handle_y = bar_y + (int)(frac_off * bar_h);
            int handle_h = (int)(frac_vis * bar_h);
            if (handle_h < 16) handle_h = 16;
            set_col(r, 0x667788FFu);
            SDL_Rect handle = {bar_x, handle_y, 4, handle_h};
            SDL_RenderFillRect(r, &handle);
        }
    }

    /* ---- Log Panel ---- */
    {
        int log_x = 0;
        int log_y = TOPO_H;
        int log_w = WINDOW_W - PANEL_W;
        int log_h = LOG_H;

        SDL_Rect bg = {log_x, log_y, log_w, log_h};
        set_col(r, COL_LOG_BG);
        SDL_RenderFillRect(r, &bg);

        set_col(r, COL_LOG_BORDER);
        SDL_RenderDrawLine(r, log_x, log_y, log_x + log_w, log_y);
        SDL_RenderDrawLine(r, log_x + log_w - 1, log_y, log_x + log_w - 1, log_y + log_h);

        char hdr[96];
        snprintf(hdr, sizeof(hdr),
                 " Console (%d lines, scroll=%d) [PgUp/PgDn] [Up/Down history]",
                 state->log_count, state->log_scroll);
        draw_string(r, log_x + 6, log_y + 4, hdr,
                    COL_R(COL_TEXT_DIM), COL_G(COL_TEXT_DIM),
                    COL_B(COL_TEXT_DIM), 0xFF);

        int line_h = FONT_H_PX + LINE_SPACE;
        int visible = (log_h - 22) / line_h;
        if (visible < 1) visible = 1;

        int total = state->log_count;
        int start_offset = state->log_scroll;
        int max_scroll = (total > visible) ? (total - visible) : 0;
        if (start_offset > max_scroll) start_offset = max_scroll;
        if (start_offset < 0) start_offset = 0;

        int first = total - visible - start_offset;
        if (first < 0) first = 0;

        int draw_y = log_y + 22;
        int max_chars = (log_w - 16) / (FONT_W + CHAR_SPACE);
        if (max_chars < 1) max_chars = 1;
        if (max_chars > LOG_LINE_LEN - 1) max_chars = LOG_LINE_LEN - 1;

        for (int i = 0; i < visible && (first + i) < total; i++) {
            int slot;
            if (state->log_count < LOG_LINES) {
                slot = first + i;
            } else {
                slot = (state->log_head + first + i) % LOG_LINES;
            }
            char buf[LOG_LINE_LEN];
            snprintf(buf, sizeof(buf), "%s", state->log_lines[slot]);
            if ((int)strlen(buf) > max_chars) buf[max_chars] = '\0';
            draw_string(r, log_x + 8, draw_y, buf,
                        COL_R(COL_LOG_TEXT), COL_G(COL_LOG_TEXT),
                        COL_B(COL_LOG_TEXT), 0xFF);
            draw_y += line_h;
        }

        if (state->log_partial_len > 0 && state->log_scroll == 0
            && draw_y + line_h <= log_y + log_h) {
            char buf[LOG_LINE_LEN];
            size_t copy_len = state->log_partial_len;
            if (copy_len > LOG_LINE_LEN - 1) copy_len = LOG_LINE_LEN - 1;
            memcpy(buf, state->log_partial, copy_len);
            buf[copy_len] = '\0';
            if ((int)strlen(buf) > max_chars) buf[max_chars] = '\0';
            draw_string(r, log_x + 8, draw_y, buf,
                        0xAA, 0xCC, 0xAA, 0xFF);
        }
    }

    /* ---- Input Box ---- */
    {
        int ix = 0;
        int iy = TOPO_H + LOG_H;
        int iw = WINDOW_W - PANEL_W;
        int ih = INPUT_H;

        SDL_Rect bg = {ix, iy, iw, ih};
        set_col(r, COL_INPUT_BG);
        SDL_RenderFillRect(r, &bg);
        set_col(r, COL_LOG_BORDER);
        SDL_RenderDrawLine(r, ix, iy, ix + iw, iy);

        int prompt_x = ix + 8;
        int text_y = iy + (INPUT_H - FONT_H_PX) / 2;
        draw_string(r, prompt_x, text_y, "magi>",
                    COL_R(COL_PROMPT), COL_G(COL_PROMPT),
                    COL_B(COL_PROMPT), 0xFF);

        int after_prompt = prompt_x + 6 * (FONT_W + CHAR_SPACE);
        char shown[INPUT_BUF_LEN];
        snprintf(shown, sizeof(shown), "%s", state->input_buf);
        int max_chars = (iw - (after_prompt - ix) - 16) / (FONT_W + CHAR_SPACE);
        if (max_chars < 1) max_chars = 1;
        if ((int)strlen(shown) > max_chars) {
            int from = (int)strlen(shown) - max_chars;
            memmove(shown, shown + from, max_chars + 1);
        }
        draw_string(r, after_prompt, text_y, shown,
                    COL_R(COL_TEXT), COL_G(COL_TEXT),
                    COL_B(COL_TEXT), 0xFF);

        int caret_x = after_prompt + (int)strlen(shown) * (FONT_W + CHAR_SPACE);
        Uint32 ms = SDL_GetTicks();
        if ((ms / 500) % 2 == 0) {
            set_col(r, COL_CARET);
            for (int yy = 0; yy < FONT_H_PX; yy++) {
                SDL_RenderDrawPoint(r, caret_x, text_y + yy);
                SDL_RenderDrawPoint(r, caret_x + 1, text_y + yy);
            }
        }
    }

    SDL_RenderPresent(r);
}

/* ======================== EVENTS ======================== */

static WidgetId widget_at(AppState* s, int mx, int my)
{
    if (point_in_rect(mx, my, &s->widget_play.bounds))   return WIDGET_BTN_PLAYPAUSE;
    if (point_in_rect(mx, my, &s->widget_step.bounds))   return WIDGET_BTN_STEP;
    if (point_in_rect(mx, my, &s->widget_reset.bounds))  return WIDGET_BTN_RESET;
    if (point_in_rect(mx, my, &s->widget_slider.bounds)) return WIDGET_SLIDER_SPEED;
    return WIDGET_NONE;
}

static void apply_slider_at_x(AppState* s, int mx)
{
    int x0 = s->widget_slider.bounds.x + 8;
    int w  = s->widget_slider.bounds.w - 16;
    if (w <= 0) return;
    double r = (double)(mx - x0) / (double)w;
    if (r < 0.0) r = 0.0;
    if (r > 1.0) r = 1.0;
    double sp = ratio_to_speed(r);
    sp = snap_speed(sp);
    sim_clock_set_speed(sp);
}

static int events_row_at(AppState* s, int mx, int my)
{
    if (!point_in_rect(mx, my, &s->events_panel_rect)) return -1;
    int row = (my - s->events_panel_rect.y) / EVENT_ROW_H;
    if (row < 0) return -1;
    if (s->events_scroll + row >= s->events_count) return -1;
    return row;
}

/* Replay-style click: flash + select the event's node, pause the sim. */
static void events_activate_row(AppState* s, int row)
{
    int rev = s->events_scroll + row;
    if (rev < 0 || rev >= s->events_count) return;
    int idx = (s->events_head - 1 - rev + EVENT_RING_CAP * 2) % EVENT_RING_CAP;
    EventEntry* ev = &s->events[idx];
    s->selected_event = s->first_event_id + s->events_count - 1 - rev;

    if (ev->node_idx >= 0 && (size_t)ev->node_idx < s->sim->node_count) {
        s->flash_at_ms[ev->node_idx] = sim_clock_now_ms() + FLASH_MS;
        s->selected_idx = ev->node_idx;
    }
    if (!sim_clock_is_paused()) sim_clock_set_paused(true);
}

static void handle_events(AppState* state)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                state->running = false;
                break;
            case SDL_TEXTINPUT: {
                size_t add = strlen(e.text.text);
                if (state->input_len + add < INPUT_BUF_LEN - 1) {
                    memcpy(state->input_buf + state->input_len, e.text.text, add);
                    state->input_len += add;
                    state->input_buf[state->input_len] = '\0';
                }
                break;
            }
            case SDL_KEYDOWN: {
                SDL_Keycode k = e.key.keysym.sym;
                bool ctrl = (e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0;
                if (k == SDLK_ESCAPE) {
                    state->running = false;
                } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                    cli_dispatch(state);
                } else if (k == SDLK_BACKSPACE) {
                    if (state->input_len > 0) {
                        state->input_len--;
                        state->input_buf[state->input_len] = '\0';
                    }
                } else if (ctrl && k == SDLK_u) {
                    state->input_buf[0] = '\0';
                    state->input_len = 0;
                } else if (ctrl && k == SDLK_l) {
                    state->log_count = 0;
                    state->log_head = 0;
                    state->log_partial_len = 0;
                    state->log_scroll = 0;
                } else if (k == SDLK_UP) {
                    history_recall(state, -1);
                } else if (k == SDLK_DOWN) {
                    history_recall(state, +1);
                } else if (k == SDLK_PAGEUP) {
                    state->log_scroll += 10;
                } else if (k == SDLK_PAGEDOWN) {
                    state->log_scroll -= 10;
                    if (state->log_scroll < 0) state->log_scroll = 0;
                } else if (k == SDLK_F5) {
                    state->needs_layout = true;
                } else if (state->input_len == 0 && k == SDLK_SPACE) {
                    sim_clock_set_paused(!sim_clock_is_paused());
                } else if (state->input_len == 0 && k == SDLK_s) {
                    if (!sim_clock_is_paused()) sim_clock_set_paused(true);
                    sim_clock_step(50.0);
                } else if (k == SDLK_LEFTBRACKET) {
                    sim_clock_set_speed(sim_clock_get_speed() * 0.5);
                } else if (k == SDLK_RIGHTBRACKET) {
                    sim_clock_set_speed(sim_clock_get_speed() * 2.0);
                } else if (state->input_len == 0 && k == SDLK_0) {
                    sim_clock_set_speed(1.0);
                }
                break;
            }
            case SDL_MOUSEMOTION: {
                int mx = e.motion.x, my = e.motion.y;
                if (state->slider_dragging) {
                    apply_slider_at_x(state, mx);
                    state->hover_widget = WIDGET_SLIDER_SPEED;
                    break;
                }
                state->hover_widget = widget_at(state, mx, my);
                state->widget_play.hovered   = (state->hover_widget == WIDGET_BTN_PLAYPAUSE);
                state->widget_step.hovered   = (state->hover_widget == WIDGET_BTN_STEP);
                state->widget_reset.hovered  = (state->hover_widget == WIDGET_BTN_RESET);
                state->widget_slider.hovered = (state->hover_widget == WIDGET_SLIDER_SPEED);

                state->events_hover_row = events_row_at(state, mx, my);

                if (my >= TOPO_TOP_Y && my < TOPO_H && mx < WINDOW_W - PANEL_W &&
                    state->hover_widget == WIDGET_NONE) {
                    state->hover_idx = hit_test_node(state, mx, my);
                    for (size_t i = 0; i < state->gnode_count; i++)
                        state->gnodes[i].hovered = ((int)i == state->hover_idx);
                } else {
                    state->hover_idx = -1;
                    for (size_t i = 0; i < state->gnode_count; i++)
                        state->gnodes[i].hovered = false;
                }
                break;
            }
            case SDL_MOUSEBUTTONDOWN: {
                int mx = e.button.x, my = e.button.y;

                int erow = events_row_at(state, mx, my);
                if (erow >= 0 && e.button.button == SDL_BUTTON_LEFT) {
                    events_activate_row(state, erow);
                    break;
                }

                WidgetId wid = widget_at(state, mx, my);
                if (wid != WIDGET_NONE && e.button.button == SDL_BUTTON_LEFT) {
                    switch (wid) {
                        case WIDGET_BTN_PLAYPAUSE:
                            sim_clock_set_paused(!sim_clock_is_paused());
                            state->widget_play.pressed = true;
                            break;
                        case WIDGET_BTN_STEP:
                            if (!sim_clock_is_paused()) sim_clock_set_paused(true);
                            sim_clock_step(50.0);
                            state->widget_step.pressed = true;
                            break;
                        case WIDGET_BTN_RESET:
                            state->needs_layout = true;
                            state->widget_reset.pressed = true;
                            break;
                        case WIDGET_SLIDER_SPEED:
                            state->slider_dragging = 1;
                            apply_slider_at_x(state, mx);
                            break;
                        default: break;
                    }
                    break;
                }

                if (e.button.button == SDL_BUTTON_LEFT &&
                    my >= TOPO_TOP_Y && my < TOPO_H && mx < WINDOW_W - PANEL_W) {
                    state->selected_idx = hit_test_node(state, mx, my);
                }
                break;
            }
            case SDL_MOUSEBUTTONUP:
                if (state->slider_dragging) state->slider_dragging = 0;
                state->widget_play.pressed  = false;
                state->widget_step.pressed  = false;
                state->widget_reset.pressed = false;
                break;
            case SDL_MOUSEWHEEL: {
                int mx, my;
                SDL_GetMouseState(&mx, &my);
                if (point_in_rect(mx, my, &state->detail_panel_rect)) {
                    state->detail_scroll -= e.wheel.y * 2;
                    if (state->detail_scroll < 0) state->detail_scroll = 0;
                } else if (point_in_rect(mx, my, &state->events_panel_rect)) {
                    state->events_scroll -= e.wheel.y * 3;
                    if (state->events_scroll < 0) state->events_scroll = 0;
                } else if (my >= TOPO_H) {
                    if (e.wheel.y > 0) state->log_scroll += 3;
                    else if (e.wheel.y < 0) {
                        state->log_scroll -= 3;
                        if (state->log_scroll < 0) state->log_scroll = 0;
                    }
                }
                break;
            }
            default: break;
        }
    }
}

/* ======================== MAIN LOOP ======================== */

int gui_run(Simulator* simulator)
{
    if (simulator == NULL) return -1;

    AppState state;
    memset(&state, 0, sizeof(state));
    state.sim = simulator;
    state.selected_idx = -1;
    state.hover_idx = -1;
    state.running = true;
    state.needs_layout = true;
    state.selected_event = -1;
    state.events_hover_row = -1;
    state.hover_widget = WIDGET_NONE;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return -1;
    }

    state.window = SDL_CreateWindow(
        WINDOW_TITLE,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H,
        SDL_WINDOW_SHOWN);
    if (!state.window) {
        fprintf(stderr, "SDL window failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    state.renderer = SDL_CreateRenderer(
        state.window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!state.renderer) {
        fprintf(stderr, "SDL renderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(state.window);
        SDL_Quit();
        return -1;
    }

    SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_BLEND);
    layout_nodes(&state);

    sim_clock_set_realtime(true);
    sim_clock_set_paused(true);

    capture_stdout_init(&state);
    log_append_line(&state, "=== Magi System (GUI) ===");
    log_append_line(&state, "Started PAUSED. PLAY runs sim, STEP >| advances 50ms.");
    log_append_line(&state, "Toolbar: PAUSE/PLAY | STEP | RESET | speed slider (0.0625x..16x)");
    log_append_line(&state, "Click an event row on the right to replay (jumps to + flashes the involved node).");
    log_append_line(&state, "Keys: SPACE=pause/play, S=step, [/]=halve/double speed, 0=1x, F5=relayout.");
    log_append_line(&state, "Try: H1 ping 10.0.0.5  |  topology  |  help");
    capture_stdout_drain(&state);

    SDL_StartTextInput();

    uint32_t last = SDL_GetTicks();
    uint32_t delay = 1000 / FPS_CAP;

    while (state.running) {
        handle_events(&state);

        double now_ms = sim_clock_now_ms();

        size_t before = sim_clock_in_flight_count();
        int will_deliver_to[SIMULATOR_MAX_NODES];
        size_t deliver_count = 0;
        for (size_t i = 0; i < before && deliver_count < SIMULATOR_MAX_NODES; i++) {
            const InFlightPacket* p = sim_clock_in_flight_at(i);
            if (p == NULL) break;
            if (p->deliver_at_ms <= now_ms) {
                int rn = node_idx_for_interface(&state, p->receiver);
                if (rn >= 0) will_deliver_to[deliver_count++] = rn;
            }
        }

        sim_clock_tick(now_ms);

        for (size_t i = 0; i < state.sim->node_count; i++) {
            if (state.sim->nodes[i].type == SIM_NODE_ROUTER) {
                router_poll_arp((Router*)state.sim->nodes[i].node, now_ms);
            }
        }

        for (size_t i = 0; i < deliver_count; i++) {
            int idx = will_deliver_to[i];
            if (idx >= 0 && idx < (int)state.gnode_count) {
                state.flash_at_ms[idx] = now_ms;
            }
        }

        capture_stdout_drain(&state);

        render(&state);

        uint32_t now = SDL_GetTicks();
        uint32_t elapsed = now - last;
        if (elapsed < delay) SDL_Delay(delay - elapsed);
        last = now;
    }

    SDL_StopTextInput();
    capture_stdout_restore(&state);

    sim_clock_set_paused(false);
    sim_clock_set_speed(1.0);
    sim_clock_set_realtime(false);
    sim_clock_clear();

    SDL_DestroyRenderer(state.renderer);
    SDL_DestroyWindow(state.window);
    SDL_Quit();
    return 0;
}

/* ======================== PNG EXPORT ======================== */

static uint32_t crc32_table[256];
static int crc32_table_ready = 0;

static void crc32_init_table(void)
{
    if (crc32_table_ready) return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            if (c & 1) c = 0xEDB88320u ^ (c >> 1);
            else       c = c >> 1;
        }
        crc32_table[n] = c;
    }
    crc32_table_ready = 1;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len)
{
    crc = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static void png_write_u32be(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static int png_write_chunk(FILE* f, const char* type, const uint8_t* data, uint32_t len)
{
    uint8_t hdr[8];
    png_write_u32be(hdr, len);
    memcpy(hdr + 4, type, 4);
    if (fwrite(hdr, 1, 8, f) != 8) return 0;

    uint32_t crc = crc32_update(0, (const uint8_t*)type, 4);
    if (len > 0 && data) {
        if (fwrite(data, 1, len, f) != len) return 0;
        crc = crc32_update(crc, data, len);
    }

    uint8_t crc_buf[4];
    png_write_u32be(crc_buf, crc);
    if (fwrite(crc_buf, 1, 4, f) != 4) return 0;
    return 1;
}

static uint32_t adler32_calc(const uint8_t* data, size_t len)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

static int png_save_rgb(const char* path, const uint8_t* pixels, int w, int h)
{
    crc32_init_table();
    FILE* f = fopen(path, "wb");
    if (!f) return 0;

    static const uint8_t sig[8] = {137,80,78,71,13,10,26,10};
    fwrite(sig, 1, 8, f);

    uint8_t ihdr[13];
    png_write_u32be(ihdr, (uint32_t)w);
    png_write_u32be(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;   /* bit depth */
    ihdr[9] = 2;   /* color type: RGB */
    ihdr[10] = 0;  /* compression */
    ihdr[11] = 0;  /* filter */
    ihdr[12] = 0;  /* interlace */
    png_write_chunk(f, "IHDR", ihdr, 13);

    size_t raw_row = 1 + (size_t)w * 3;
    size_t raw_size = raw_row * (size_t)h;
    uint8_t* raw = (uint8_t*)malloc(raw_size);
    if (!raw) { fclose(f); return 0; }
    for (int y = 0; y < h; y++) {
        raw[y * raw_row] = 0; /* filter none */
        memcpy(raw + y * raw_row + 1, pixels + y * w * 3, (size_t)w * 3);
    }

    uint32_t adler = adler32_calc(raw, raw_size);

    size_t max_block = 65535;
    size_t num_blocks = (raw_size + max_block - 1) / max_block;
    size_t deflate_size = 2 + raw_size + 5 * num_blocks + 4;
    uint8_t* zdata = (uint8_t*)malloc(deflate_size);
    if (!zdata) { free(raw); fclose(f); return 0; }

    size_t zpos = 0;
    zdata[zpos++] = 0x78; /* zlib CM=8, CINFO=7 */
    zdata[zpos++] = 0x01; /* FCHECK */

    size_t remaining = raw_size;
    size_t src_off = 0;
    while (remaining > 0) {
        size_t block = remaining > max_block ? max_block : remaining;
        int is_final = (remaining <= max_block) ? 1 : 0;
        zdata[zpos++] = (uint8_t)(is_final ? 0x01 : 0x00);
        zdata[zpos++] = (uint8_t)(block & 0xFF);
        zdata[zpos++] = (uint8_t)((block >> 8) & 0xFF);
        zdata[zpos++] = (uint8_t)(~block & 0xFF);
        zdata[zpos++] = (uint8_t)((~block >> 8) & 0xFF);
        memcpy(zdata + zpos, raw + src_off, block);
        zpos += block;
        src_off += block;
        remaining -= block;
    }
    free(raw);

    png_write_u32be(zdata + zpos, adler);
    zpos += 4;

    png_write_chunk(f, "IDAT", zdata, (uint32_t)zpos);
    free(zdata);

    png_write_chunk(f, "IEND", NULL, 0);
    fclose(f);
    return 1;
}

static void render_export(AppState* state)
{
    SDL_Renderer* r = state->renderer;

    set_col(r, COL_BG);
    SDL_RenderClear(r);

    draw_string(r, 12, 6, "MAGI SYSTEM - GUI",
                COL_R(COL_TITLE), COL_G(COL_TITLE), COL_B(COL_TITLE), COL_A(COL_TITLE));

    if (state->sim->node_count == 0) {
        draw_string(r, WINDOW_W / 2 - 140, WINDOW_H / 2,
                    "No topology loaded.",
                    0xFF, 0x66, 0x66, 0xFF);
        return;
    }

    if (state->needs_layout) layout_nodes(state);

    for (size_t i = 0; i < state->glink_count; i++) {
        const GLink* gl = &state->glinks[i];
        int x1 = state->gnodes[gl->n1].x;
        int y1 = state->gnodes[gl->n1].y;
        int x2 = state->gnodes[gl->n2].x;
        int y2 = state->gnodes[gl->n2].y;
        draw_thick_line(r, x1, y1, x2, y2, COL_LINK);

        char delay_str[16];
        snprintf(delay_str, sizeof(delay_str), "%.0fms", gl->delay);
        draw_string(r, (x1 + x2) / 2 + 6, (y1 + y2) / 2 - 6, delay_str,
                    0x44, 0xCC, 0x88, 0xCC);
    }

    for (size_t i = 0; i < state->gnode_count; i++) {
        const GNode* gn = &state->gnodes[i];
        int x = gn->x - gn->w / 2, y = gn->y - gn->h / 2;
        uint32_t col = node_color(state->sim->nodes[gn->index].type);

        fill_rounded_rect(r, x, y, gn->w, gn->h, NODE_RADIUS, col);

        const char* name = state->sim->nodes[gn->index].name;
        int name_pw = (int)strlen(name) * (FONT_W + CHAR_SPACE);
        draw_string(r, gn->x - name_pw / 2, gn->y - FONT_H_PX / 2,
                    name, 0xFF, 0xFF, 0xFF, 0xFF);

        const char* tl = node_type_label(state->sim->nodes[gn->index].type);
        int tl_pw = (int)strlen(tl) * (FONT_W + CHAR_SPACE);
        draw_string(r, gn->x - tl_pw / 2, gn->y + FONT_H_PX / 2 + 2,
                    tl, 0x00, 0x00, 0x00, 0xAA);
    }

    /* Legend */
    {
        int lx = LEGEND_X;
        int ly = WINDOW_H - 30;
        int x = lx;

        fill_rounded_rect(r, x, ly + 3, 10, 10, 2, COL_HOST);
        draw_string(r, x + 14, ly + 3, "Host", 0xCC, 0xFF, 0xCC, 0xFF);
        x += 60;
        fill_rounded_rect(r, x, ly + 3, 10, 10, 2, COL_SWITCH);
        draw_string(r, x + 14, ly + 3, "Switch", 0xCC, 0xDD, 0xFF, 0xFF);
        x += 70;
        fill_rounded_rect(r, x, ly + 3, 10, 10, 2, COL_ROUTER);
        draw_string(r, x + 14, ly + 3, "Router", 0xFF, 0xDD, 0xAA, 0xFF);
    }

    /* Node count */
    {
        char info[64];
        snprintf(info, sizeof(info), "Nodes: %zu  Links: %zu",
                 state->sim->node_count, state->sim->link_count);
        int iw = (int)strlen(info) * (FONT_W + CHAR_SPACE);
        draw_string(r, WINDOW_W - iw - 12, 6, info, 0xCC, 0xDD, 0xEE, 0xFF);
    }
}

int gui_export_topology(Simulator* simulator, const char* output_path)
{
    if (simulator == NULL || output_path == NULL) return -1;

    int need_sdl_init = 0;
    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
            return -1;
        }
        need_sdl_init = 1;
    }

    int export_w = 1280, export_h = 720;
    SDL_Window* win = SDL_CreateWindow("export", SDL_WINDOWPOS_UNDEFINED,
                                       SDL_WINDOWPOS_UNDEFINED,
                                       export_w, export_h,
                                       SDL_WINDOW_HIDDEN);
    if (!win) {
        fprintf(stderr, "SDL window failed: %s\n", SDL_GetError());
        if (need_sdl_init) SDL_Quit();
        return -1;
    }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        fprintf(stderr, "SDL renderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        if (need_sdl_init) SDL_Quit();
        return -1;
    }

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    AppState state;
    memset(&state, 0, sizeof(state));
    state.sim = simulator;
    state.selected_idx = -1;
    state.hover_idx = -1;
    state.running = false;
    state.needs_layout = true;
    state.window = win;
    state.renderer = ren;
    state.pipe_r = -1;
    state.pipe_w = -1;
    state.orig_stdout = -1;
    state.orig_stderr = -1;

    layout_nodes(&state);
    render_export(&state);
    SDL_RenderPresent(ren);

    SDL_Surface* surface = SDL_CreateRGBSurface(0, export_w, export_h, 32,
                                                 0x00FF0000, 0x0000FF00,
                                                 0x000000FF, 0xFF000000);
    if (!surface) {
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        if (need_sdl_init) SDL_Quit();
        return -1;
    }

    if (SDL_RenderReadPixels(ren, NULL, surface->format->format,
                             surface->pixels, surface->pitch) != 0) {
        SDL_FreeSurface(surface);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        if (need_sdl_init) SDL_Quit();
        return -1;
    }

    uint8_t* rgb = (uint8_t*)malloc((size_t)export_w * (size_t)export_h * 3);
    if (!rgb) {
        SDL_FreeSurface(surface);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        if (need_sdl_init) SDL_Quit();
        return -1;
    }

    for (int y = 0; y < export_h; y++) {
        uint32_t* src_row = (uint32_t*)((uint8_t*)surface->pixels + y * surface->pitch);
        uint8_t* dst_row = rgb + y * export_w * 3;
        for (int x = 0; x < export_w; x++) {
            uint32_t pixel = src_row[x];
            dst_row[x * 3 + 0] = (uint8_t)((pixel >> 16) & 0xFF);
            dst_row[x * 3 + 1] = (uint8_t)((pixel >> 8) & 0xFF);
            dst_row[x * 3 + 2] = (uint8_t)(pixel & 0xFF);
        }
    }

    int result = png_save_rgb(output_path, rgb, export_w, export_h) ? 0 : -1;

    free(rgb);
    SDL_FreeSurface(surface);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    if (need_sdl_init) SDL_Quit();

    return result;
}
