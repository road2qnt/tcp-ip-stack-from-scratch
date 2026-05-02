#include "cli.h"

#define MAX_COMMAND_LENGTH 100

void cli(){
    printf("=== Magi System ===\n");
    bool loop = true;

    char input[MAX_COMMAND_LENGTH];
    while (loop){
        printf("magi> ");
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = '\0';
        loop = process(input);
    }

    printf("=== END ===\n");
}

bool process(char* command){
    if (strcmp(command, "create") == 0){
        /* create */
        return true;
    } else if (strcmp(command, "link") == 0){
        /* link */
        return true;
    } else if (strcmp(command, "unlink") == 0){
        /* unlink */
        return true;
    } else if (strcmp(command, "save") == 0){
        /* save */
        return true;
    } else if (strcmp(command, "load") == 0){
        /* load */
        return true;
    } else if (strcmp(command, "exit") == 0){
        return false;
    } else {
        printf("Unknown command: %s\n", command);
        return true;
    }
}