#include "cli.h"

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
    printf("  exit | quit\n");
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
    } else if (strcmp(tokens[0], "topology") == 0){
        simulator_print_topology(&simulator);
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
