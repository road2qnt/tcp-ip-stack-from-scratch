#include "app.h"
#include "font.h"

#include "../layer2/host.h"
#include "../layer2/switch.h"
#include "../layer3/router.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

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

    int cx = WINDOW_W / 2 - PANEL_W / 2;
    int cy = WINDOW_H / 2;
    int radius = (state->gnode_count <= 1) ? 0 :
                 (int)(fmin(WINDOW_W, WINDOW_H) * 0.35);
    if (radius < 80) radius = 80;
    if (radius > 350) radius = 350;

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
        if (ny + NODE_H / 2 > WINDOW_H - 10) ny = WINDOW_H - 10 - NODE_H / 2;
        if (ny - NODE_H / 2 < 10) ny = 10 + NODE_H / 2;

        state->gnodes[i].x = nx;
        state->gnodes[i].y = ny;
        placed++;
    }

    state->needs_layout = false;
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
    for (size_t i = 0; i < state->gnode_count; i++) {
        const GNode* gn = &state->gnodes[i];
        int x = gn->x - gn->w / 2, y = gn->y - gn->h / 2;
        uint32_t col = node_color(state->sim->nodes[gn->index].type);
        bool is_sel = (state->selected_idx == (int)i);
        bool is_hov = (state->hover_idx == (int)i);

        /* Selection/hover border */
        if (is_sel)
            fill_rounded_rect(r, x - 3, y - 3, gn->w + 6, gn->h + 6,
                              NODE_RADIUS + 2, COL_SEL);
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

    /* ---- Legend ---- */
    {
        int lx = LEGEND_X;
        int ly = WINDOW_H - 100;

        SDL_Rect bg = {lx - 4, ly - 4, 200, 90};
        set_col(r, COL_LEGEND);
        SDL_RenderFillRect(r, &bg);

        draw_string(r, lx, ly, "Legend:", 0xFF, 0xFF, 0xFF, 0xFF);
        ly += FONT_H_PX + LINE_SPACE;

        fill_rounded_rect(r, lx, ly, 12, 12, 2, COL_HOST);
        draw_string(r, lx + 18, ly - 2, "Host", 0xCC, 0xFF, 0xCC, 0xFF);
        ly += FONT_H_PX + LINE_SPACE;

        fill_rounded_rect(r, lx, ly, 12, 12, 2, COL_SWITCH);
        draw_string(r, lx + 18, ly - 2, "Switch", 0xCC, 0xDD, 0xFF, 0xFF);
        ly += FONT_H_PX + LINE_SPACE;

        fill_rounded_rect(r, lx, ly, 12, 12, 2, COL_ROUTER);
        draw_string(r, lx + 18, ly - 2, "Router", 0xFF, 0xDD, 0xAA, 0xFF);
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
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q)
                    state->running = false;
                else if (e.key.keysym.sym == SDLK_r)
                    state->needs_layout = true;
                break;
            case SDL_MOUSEMOTION:
                state->hover_idx = hit_test_node(state, e.motion.x, e.motion.y);
                for (size_t i = 0; i < state->gnode_count; i++)
                    state->gnodes[i].hovered = ((int)i == state->hover_idx);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT)
                    state->selected_idx = hit_test_node(state, e.button.x, e.button.y);
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

    uint32_t last = SDL_GetTicks();
    uint32_t delay = 1000 / FPS_CAP;

    while (state.running) {
        handle_events(&state);
        render(&state);

        uint32_t now = SDL_GetTicks();
        uint32_t elapsed = now - last;
        if (elapsed < delay) SDL_Delay(delay - elapsed);
        last = now;
    }

    SDL_DestroyRenderer(state.renderer);
    SDL_DestroyWindow(state.window);
    SDL_Quit();
    return 0;
}
