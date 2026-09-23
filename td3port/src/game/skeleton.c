/* PORT: phase 4 placeholder for game_main (main 0000:0000, game_flow.md). Delete when game/flow.c
 * provides the real one. It exercises the skeleton end to end: the EXE's initial palette buffer
 * (DS:0B6A) goes to the DAC, a 256-colour test pattern into VGA memory, and the 145.65 Hz tick and
 * keyboard paths run until Esc (XT 01h) is pressed. */
#include "host.h"
#include "mem.h"
#include "platform/vga.h"

static u32 ticks;
static bool esc;

static void tick(void) { ticks++; }
static void kbd(u8 b) { if (b == 0x01) esc = true; }

int game_main(void)
{
    for (int i = 0; i < 256; i++)
        vga_dac_write((u8)i, DSB(0x0B6A + 3 * i), DSB(0x0B6B + 3 * i), DSB(0x0B6C + 3 * i));
    u8 *vram = mp(VRAM_SEG, 0);
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++)
            vram[y * 320 + x] = (u8)((y / 12) * 16 + x / 20);
    host_set_tick_handler(tick);
    host_set_kbd_handler(kbd);
    while (!esc) {
        host_wait_vretrace();
        vga_set_start((u16)(ticks / 146 % 2 ? 320 * 2 : 0));   /* 1 Hz two-line shake: CRTC start path */
    }
    return 0;
}
