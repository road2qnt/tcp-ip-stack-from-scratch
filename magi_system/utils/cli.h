#ifndef CLI_H
#define CLI_H

#include "../simulator.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/*
typedef struct CLI{
    bool is_loop;
} CLI;

CLI cli_init();
void process(CLI*cli)
*/

void cli();
bool process(char* command);

#endif
