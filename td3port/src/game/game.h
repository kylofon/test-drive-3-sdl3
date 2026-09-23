#pragma once
/* Cross-module interface of the game code: one include for the game modules (flow, hud, sim, render) and
 * the platform and sound APIs they call. Each module's own header lists the functions other modules call;
 * functions private to a module are declared in that module's .c files (or a private header).
 *
 * Start-up order (main.c, coordinator):
 *     mem_load_exe(); host_init(); vga_init(); modules_init(); game_main(); host_shutdown();
 */
#include "types.h"
#include "mem.h"
#include "host.h"
#include "codeptr.h"
#include "symbols.h"
#include "platform/vga.h"
#include "platform/platform.h"
#include "sound/sound.h"
#include "game/flow.h"
#include "game/hud.h"
#include "game/sim.h"
#include "game/render.h"

/* Implemented by the coordinator (main.c): every module's port set-up, before game_main():
 *     platform_init();              host tick + keyboard handlers (platform/platform.h)
 *     sound_register_codeptrs();    device / effect / music handler tables (sound/sound.h)
 *     sim_register_codeptrs();      key handler tables 0e12:0000 and DS:B6EF (game/sim.h)
 * The game's own initialisation (config_load, kbd_install, snd_init, mem_alloc_all, ...) stays in
 * game_main as in the original. */
void modules_init(void);
