/* Test Drive III: The Passion SDL3 port — entry point.
 *
 * usage: td3port [--game-dir DIR] [--scale N] [--frame-ticks N] [--check]
 *   --game-dir    folder with the original game files (default: "Game" in the working directory)
 *   --scale       initial window scale (default 3)
 *   --frame-ticks race frame pacing in timer ticks (default HOST_DEFAULT_FRAME_TICKS = design speed;
 *                 5 = the original engine cap, PLAN.md decision 4)
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
#include "platform/vga.h"

int game_main(void);   /* game code: port of main() at 0000:0000 */

int main(int argc, char **argv)
{
    const char *dir = "Game";
    int scale = 3;
    bool check = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--game-dir") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frame-ticks") && i + 1 < argc) host_set_frame_ticks(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--check")) check = true;
        else {
            fprintf(stderr, "usage: %s [--game-dir DIR] [--scale N] [--frame-ticks N] [--check]\n", argv[0]);
            return 2;
        }
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

    if (!host_init(dir, scale)) return 1;
    vga_init();      /* mode 13h model: frame source, DAC, CRTC start */
    int rc = game_main();
    host_shutdown();
    return rc;
}
