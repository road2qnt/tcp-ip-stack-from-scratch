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

#define WINDOW_TITLE  "Magi System - Network Topology Visualizer"
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

static void log_append_line(AppState* s, const char* line)
{
    int idx = s->log_head;
    snprintf(s->log_lines[idx], LOG_LINE_LEN, "%s", line);
    s->log_head = (s->log_head + 1) % LOG_LINES;
    if (s->log_count < LOG_LINES) s->log_count++;
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
    static char buf[2048];
    buf[0] = '\0';

    if (node_idx < 0 || node_idx >= (int)state->sim->node_count) return "";

    const SimulatorNodeEntry* entry = &state->sim->nodes[node_idx];
    const Node* node = entry->node;

    append(buf, sizeof(buf), "Name: %s\n", entry->name);
    append(buf, sizeof(buf), "Type: %s\n", node_type_label(entry->type));
    append(buf, sizeof(buf), "Interfaces: %d\n", node->NUM_INTERFACES);

    for (int i = 0; i < node->NUM_INTERFACES && i < 6; i++) {
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
            for (int i = 0; i < sw->base.NUM_INTERFACES && i < 8; i++) {
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
            for (size_t i = 0; i < rt->route_count && i < 5; i++) {
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

    /* Clear */
    set_col(r, COL_BG);
    SDL_RenderClear(r);

    /* Title */
    draw_string(r, 12, 8, "MAGI SYSTEM - Topology Visualizer",
                COL_R(COL_TITLE), COL_G(COL_TITLE), COL_B(COL_TITLE), COL_A(COL_TITLE));

    char header[64];
    snprintf(header, sizeof(header),
             "Nodes: %zu | Links: %zu | [R]efresh [ESC] Quit",
             state->sim->node_count, state->sim->link_count);
    draw_string(r, 12, 24, header,
                COL_R(COL_TEXT_DIM), COL_G(COL_TEXT_DIM),
                COL_B(COL_TEXT_DIM), COL_A(COL_TEXT_DIM));

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

    /* ---- Legend (compact horizontal strip) ---- */
    {
        int lx = LEGEND_X;
        int ly = 44;
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

    /* ---- Detail Panel ---- */
    if (state->selected_idx >= 0) {
        int px = WINDOW_W - PANEL_W;
        int pw = PANEL_W;
        int ph = WINDOW_H;

        SDL_Rect panel_bg = {px, 0, pw, ph};
        set_col(r, COL_PANEL);
        SDL_RenderFillRect(r, &panel_bg);

        set_col(r, 0x335577FFu);
        SDL_RenderDrawLine(r, px, 0, px, ph);

        const SimulatorNodeEntry* entry = &state->sim->nodes[state->selected_idx];
        char ptitle[64];
        snprintf(ptitle, sizeof(ptitle), " %s [%s]", entry->name,
                 node_type_label(entry->type));
        draw_string(r, px + 8, 8, ptitle,
                    COL_R(COL_TITLE), COL_G(COL_TITLE),
                    COL_B(COL_TITLE), COL_A(COL_TITLE));

        set_col(r, 0x446688FFu);
        SDL_RenderDrawLine(r, px + 8, 8 + FONT_H_PX + 4,
                           px + pw - 8, 8 + FONT_H_PX + 4);

        const char* detail = get_node_detail(state, state->selected_idx);
        draw_string_wrapped(r, px + 8, 8 + FONT_H_PX + 12,
                            detail, pw - 20,
                            0xCC, 0xDD, 0xEE, 0xFF);
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
                }
                break;
            }
            case SDL_MOUSEMOTION:
                if (e.motion.y < TOPO_H) {
                    state->hover_idx = hit_test_node(state, e.motion.x, e.motion.y);
                    for (size_t i = 0; i < state->gnode_count; i++)
                        state->gnodes[i].hovered = ((int)i == state->hover_idx);
                } else {
                    state->hover_idx = -1;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT && e.button.y < TOPO_H) {
                    state->selected_idx = hit_test_node(state, e.button.x, e.button.y);
                }
                break;
            case SDL_MOUSEWHEEL:
                if (e.wheel.y > 0) state->log_scroll += 3;
                else if (e.wheel.y < 0) {
                    state->log_scroll -= 3;
                    if (state->log_scroll < 0) state->log_scroll = 0;
                }
                break;
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

    capture_stdout_init(&state);
    log_append_line(&state, "=== Magi System (GUI) ===");
    log_append_line(&state, "Type a command and press Enter. Esc quits.");
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

    sim_clock_set_realtime(false);
    sim_clock_clear();

    SDL_DestroyRenderer(state.renderer);
    SDL_DestroyWindow(state.window);
    SDL_Quit();
    return 0;
}
