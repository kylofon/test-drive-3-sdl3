/* Test Drive III: The Passion SDL3 port — entry point.
 *
 * usage: td3port [--game-dir DIR] [--scale N] [--fullscreen] [--frame-ticks N] [--sound adlib|speaker]
 *                [--car CODE] [--course CODE] [--skill N] [--check]
 *   --game-dir    folder with the original game files (default: "Game" in the working directory)
 *   --scale       initial window scale: 320x240 times N (default 3)
 *   --fullscreen  start in full screen (Alt+Enter switches)
 *   --frame-ticks race frame pacing in 145.6 Hz timer ticks (default HOST_DEFAULT_FRAME_TICKS = 23;
 *                 5 = the original engine cap, 29 = the race clock's design rate; PLAN.md decision 4)
 *   --sound       overrides TD3.CFG's audio device: adlib (AdLib / Sound Blaster) or speaker (PC speaker)
 *   --car         the car as if chosen in the game's menu: a slot's base name, e.g. CCNSX (PLAYDISK.DAT)
 *   --course      the course as if chosen in the game's menu: a scene's base name, e.g. SCENE02
 *   --skill       the skill level 1..9 as shown in the game (1-3 automatic gearbox)
 *   --check       load and verify the original executable, print a summary and exit (no window)
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "mem.h"
#include "portcfg.h"
#include "platform/vga.h"
#include "game/game.h"

static int usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [--game-dir DIR] [--scale N] [--fullscreen] [--frame-ticks N] [--sound adlib|speaker]\n"
            "          [--car CODE] [--course CODE] [--skill N] [--check]\n", prog);
    return 2;
}

int main(int argc, char **argv)
{
    const char *dir = "Game";
    int scale = 3;
    bool check = false, fullscreen = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--game-dir") && v) { dir = v; i++; }
        else if (!strcmp(a, "--scale") && v) { scale = atoi(v); i++; }
        else if (!strcmp(a, "--fullscreen")) fullscreen = true;
        else if (!strcmp(a, "--frame-ticks") && v) { host_set_frame_ticks(atoi(v)); i++; }
        else if (!strcmp(a, "--sound") && v) {
            if (!SDL_strcasecmp(v, "adlib")) portcfg.sound = 4;
            else if (!SDL_strcasecmp(v, "speaker")) portcfg.sound = 0;
            else return usage(argv[0]);
            i++;
        }
        else if (!strcmp(a, "--car") && v) { SDL_strlcpy(portcfg.car, v, sizeof portcfg.car); i++; }
        else if (!strcmp(a, "--course") && v) { SDL_strlcpy(portcfg.course, v, sizeof portcfg.course); i++; }
        else if (!strcmp(a, "--skill") && v) { portcfg.skill = atoi(v); i++; }
        else if (!strcmp(a, "--check")) check = true;
        else return usage(argv[0]);
    }

    char exe_path[1024];
    snprintf(exe_path, sizeof exe_path, "%s/%s", dir, TD_EXE_NAME);
    char err[256];
    if (!mem_load_exe(exe_path, err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Test Drive III", err, NULL);
        return 1;
    }
    if (check) {
        printf("%s ok: image %u bytes at %04X:0000, DGROUP %04X, frame pacing %d ticks\n", TD_EXE_NAME,
               mem_image_size, LOAD_SEG, DGROUP, host_frame_ticks());
        return 0;
    }

    if (!host_init(dir, scale, fullscreen)) return 1;
    vga_init();      /* mode 13h model: frame source, DAC, CRTC start */
    modules_init();  /* host handlers, code-pointer tables (modules.c) */
    int rc = game_main();
    host_shutdown();
    return rc;
}
