/* Coordinator: start-up of the port modules (game/game.h). Order: platform (host tick and keyboard
 * handlers), then the code-pointer tables of sound (device routine / MIDI handler tables) and
 * simulation (key handler table 0e12:0000). The game's own initialisation runs inside game_main. */
#include "game/game.h"

void modules_init(void)
{
    platform_init();
    sound_register_codeptrs();
    sim_register_codeptrs();
}
