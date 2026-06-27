#ifndef GUI_APP_H
#define GUI_APP_H

#include "../simulator.h"

int gui_run(Simulator* simulator);
int gui_export_topology(Simulator* simulator, const char* output_path);

#endif
