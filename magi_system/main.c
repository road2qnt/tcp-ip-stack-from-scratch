#include "utils/cli.h"
#include "utils/loader.h"
#include "simulator.h"
#include <string.h>
#include <stdio.h>

#ifdef HAS_SDL
#include "gui/app.h"
#endif

int main(int argc, char* argv[])
{
    /* Check for --gui flag to launch SDL visualizer */
    if (argc > 1 && strcmp(argv[1], "--gui") == 0) {
#ifdef HAS_SDL
        Simulator sim;
        simulator_init(&sim);
        simulator_load(&sim, "magi_system/topology.json");
        gui_run(&sim);
        simulator_clear(&sim);
#else
        printf("GUI not available (SDL2 not found at build time).\n");
        printf("Install libsdl2-dev and rebuild.\n");
#endif
        return 0;
    }

    cli();
    return 0;
}
