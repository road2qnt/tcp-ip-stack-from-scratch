#ifndef GUI_APP_H
#define GUI_APP_H

#include "../simulator.h"

// Run the SDL2 topology visualizer.
// Takes a Simulator* and renders the network topology in an interactive window.
// Returns 0 on success, -1 on failure (e.g., SDL not available).
int gui_run(Simulator* simulator);

#endif
