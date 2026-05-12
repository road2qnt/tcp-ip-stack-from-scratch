#ifndef LOADER_H
#define LOADER_H

#include "../simulator.h"

int simulator_save(Simulator *simulator, const char *filename);
int simulator_load(Simulator *simulator, const char *filename);

#endif