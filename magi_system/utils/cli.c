#include "cli.h"
#include "loader.h"
#include "visualizer.h"
#include "debugger.h"
#include "../layer2/host.h"
#include "../layer3/router.h"
#include "../layer3/icmp.h"

#define MAX_COMMAND_LENGTH 100

static Simulator simulator;

static void print_help(void)
{
    printf("Commands:\n");
    printf("  create <host|switch|router> <name> [ports]\n");
    printf("  link <device[:port]> <device[:port]> [delay_ms]\n");
    printf("  unlink <device[:port]> <device[:port]>\n");
    printf("  save [filename]\n");
    printf("  load [filename]\n");
    printf("  topology\n");
    printf("  <host> ping <ip|host>\n");
    printf("  <host> traceroute <ip|host>\n");
    printf("  <router> route\n");
    printf("  exit | quit\n");
}

static int resolve_ip_target(const char* token, IpAddress* out_ip)
{
    int idx;

    if (token == NULL || out_ip == NULL) {
        return 0;
    }

    if (ip_parse(token, out_ip)) {
        return 1;
    }

    idx = simulator_find_node(&simulator, token);
    if (idx < 0) {
        return 0;
    }

    if (simulator.nodes[idx].type == SIM_NODE_HOST) {
        Host* host = (Host*)simulator.nodes[idx].node;
        if (host->has_ip) {
            *out_ip = host->ip_address;
            return 1;
        }
    } else if (simulator.nodes[idx].type == SIM_NODE_ROUTER) {
        Router* router = (Router*)simulator.nodes[idx].node;
        for (int i = 0; i < router->base.NUM_INTERFACES; i++) {
            if (router->interface_ips[i].has_ip) {
                *out_ip = router->interface_ips[i].ip_address;
                return 1;
            }
        }
    }

    return 0;
}

static int cli_find_host(const char* name, Host** out_host)
{
    int idx;

    if (out_host != NULL) {
        *out_host = NULL;
    }

    idx = simulator_find_node(&simulator, name);
    if (idx < 0 || simulator.nodes[idx].type != SIM_NODE_HOST) {
        return 0;
    }

    if (out_host != NULL) {
        *out_host = (Host*)simulator.nodes[idx].node;
    }
    return 1;
}

static int cli_find_router(const char* name, Router** out_router)
{
    int idx;

    if (out_router != NULL) {
        *out_router = NULL;
    }

    idx = simulator_find_node(&simulator, name);
    if (idx < 0 || simulator.nodes[idx].type != SIM_NODE_ROUTER) {
        return 0;
    }

    if (out_router != NULL) {
        *out_router = (Router*)simulator.nodes[idx].node;
    }
    return 1;
}

void cli(){
    printf("=== Magi System ===\n");
    bool loop = true;

    simulator_init(&simulator);

    char input[MAX_COMMAND_LENGTH];
    while (loop){
        printf("magi> ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        input[strcspn(input, "\n")] = '\0';
        loop = process(input);
    }

    simulator_clear(&simulator);
    printf("=== END ===\n");
}

bool process(char* command){
    char buffer[MAX_COMMAND_LENGTH];
    char *tokens[8];
    int count = 0;
    char *token;

    if (command == NULL) {
        return true;
    }

    strncpy(buffer, command, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    token = strtok(buffer, " \t");
    while (token != NULL && count < 8) {
        tokens[count++] = token;
        token = strtok(NULL, " \t");
    }

    if (count == 0) {
        return true;
    }

    if (strcmp(tokens[0], "create") == 0){
        int ports = 0;

        if (count < 3) {
            printf("Usage: create <host|switch|router> <name> [ports]\n");
            return true;
        }

        if (count >= 4) {
            ports = atoi(tokens[3]);
        }

        if (simulator_create_node(&simulator, tokens[1], tokens[2], ports)) {
            printf("[SIM] Created %s %s\n", tokens[1], tokens[2]);
        } else {
            printf("[SIM] Failed to create %s %s\n", tokens[1], tokens[2]);
        }
        return true;
    } else if (strcmp(tokens[0], "link") == 0){
        float delay = 0;

        if (count < 3) {
            printf("Usage: link <device[:port]> <device[:port]> [delay_ms]\n");
            return true;
        }

        if (count >= 4) {
            delay = (float)atof(tokens[3]);
        }

        if (simulator_link(&simulator, tokens[1], tokens[2], delay)) {
            printf("[SIM] Linked %s <-> %s delay=%.0fms\n", tokens[1], tokens[2], delay);
        } else {
            printf("[SIM] Failed to link %s <-> %s\n", tokens[1], tokens[2]);
        }
        return true;
    } else if (strcmp(tokens[0], "unlink") == 0){
        if (count < 3) {
            printf("Usage: unlink <device[:port]> <device[:port]>\n");
            return true;
        }

        if (simulator_unlink(&simulator, tokens[1], tokens[2])) {
            printf("[SIM] Unlinked %s <-> %s\n", tokens[1], tokens[2]);
        } else {
            printf("[SIM] Failed to unlink %s <-> %s\n", tokens[1], tokens[2]);
        }
        return true;
    } else if (strcmp(tokens[0], "save") == 0){
        const char *filename = count >= 2 ? tokens[1] : "magi_system/topology.json";

        if (simulator_save(&simulator, filename)) {
            printf("[SIM] Saved topology to %s\n", filename);
        } else {
            printf("[SIM] Failed to save topology to %s\n", filename);
        }
        return true;
    } else if (strcmp(tokens[0], "load") == 0){
        const char *filename = count >= 2 ? tokens[1] : "magi_system/topology.json";

        if (simulator_load(&simulator, filename)) {
            printf("[SIM] Loaded topology from %s\n", filename);
        } else {
            printf("[SIM] Failed to load topology from %s\n", filename);
        }
        return true;
    } else if (strcmp(tokens[0], "debug") == 0){
        const char *target = count >= 2 ? tokens[1] : "help";

        if (strcmp(target, "m0") == 0 || strcmp(target, "0") == 0) {
            debug_milestone_0(&simulator);
        } else if (strcmp(target, "m1") == 0 || strcmp(target, "1") == 0) {
            debug_milestone_1(&simulator);
        } else if (strcmp(target, "m2") == 0 || strcmp(target, "2") == 0) {
            debug_milestone_2(&simulator);
        } else if (strcmp(target, "m3") == 0 || strcmp(target, "3") == 0) {
            debug_milestone_3(&simulator);
        } else if (strcmp(target, "m4") == 0 || strcmp(target, "4") == 0) {
            debug_milestone_4(&simulator);
        } else if (strcmp(target, "all") == 0) {
            debug_run_all(&simulator);
        } else {
            printf("Debug targets:\n");
            printf("  debug m0   - Milestone 0: Fondasi Simulasi\n");
            printf("  debug m1   - Milestone 1: Data Link Layer\n");
            printf("  debug m2   - Milestone 2: Network Layer\n");
            printf("  debug m3   - Milestone 3: Transport Layer\n");
            printf("  debug m4   - Milestone 4: Application Layer\n");
            printf("  debug all  - Run all milestone tests\n");
        }
        return true;
    } else if (strcmp(tokens[0], "topology") == 0){
        simulator_print_topology(&simulator);
        return true;
    } else if (count >= 2 && strcmp(tokens[1], "ping") == 0) {
        Host* host;
        IpAddress target_ip;

        if (count < 3) {
            printf("Usage: <host> ping <ip|host>\n");
            return true;
        }

        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }

        if (!resolve_ip_target(tokens[2], &target_ip)) {
            printf("Invalid target: %s\n", tokens[2]);
            return true;
        }

        host_send_icmp_echo_request(host, &target_ip, IPV4_DEFAULT_TTL, 1);
        return true;
    } else if (count >= 2 && strcmp(tokens[1], "traceroute") == 0) {
        Host* host;
        IpAddress target_ip;
        char target[32];

        if (count < 3) {
            printf("Usage: <host> traceroute <ip|host>\n");
            return true;
        }

        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }

        if (!resolve_ip_target(tokens[2], &target_ip)) {
            printf("Invalid target: %s\n", tokens[2]);
            return true;
        }

        ip_to_string(&target_ip, target, sizeof(target), false);
        printf("traceroute to %s\n", target);
        for (uint8_t ttl = 1; ttl <= 30; ttl++) {
            host_send_icmp_echo_request(host, &target_ip, ttl, ttl);
            if (!host->has_last_icmp) {
                printf("%u  *\n", ttl);
                continue;
            }

            char hop[32];
            ip_to_string(&host->last_icmp_source, hop, sizeof(hop), false);
            printf("%u  %s  0ms\n", ttl, hop);
            if (host->last_icmp_type == ICMP_ECHO_REPLY) {
                break;
            }
            if (host->last_icmp_type == ICMP_DEST_UNREACHABLE) {
                break;
            }
        }
        return true;
    } else if (count >= 2 && strcmp(tokens[1], "route") == 0) {
        Router* router;

        if (!cli_find_router(tokens[0], &router)) {
            printf("Unknown router: %s\n", tokens[0]);
            return true;
        }

        router_print_routes(router);
        return true;
    } else if (strcmp(tokens[0], "help") == 0){
        print_help();
        return true;
    } else if (strcmp(tokens[0], "exit") == 0 || strcmp(tokens[0], "quit") == 0){
        return false;
    } else {
        printf("Unknown command: %s\n", command);
        print_help();
        return true;
    }
}
