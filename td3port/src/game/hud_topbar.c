/* HUD top bar (hud.md §4.9): race clock, compass window, radar detector. Near routines of segment
 * 0e12 called from frame_update; they draw on the current page (page 0 at that point). */
#include "game/game.h"

/* Scratch bytes the original uses for its divisions / loop counters (render3d names, other uses). */
#define SCR_A DSB(DS_place_rot)       /* DS:946A */
#define SCR_B DSB(DS_place_nverts)    /* DS:946B */

/* One 4x5 top-bar digit (digit font DS:B75B, 5 bytes each) at the current pen. */
static void topbar_digit(u8 d)
{
    gfx_draw_bitmap((u16)(DS_digit_font + (u8)(d * 5)), 1, 5);
}

/* 0e12:0b1d race_clock_hud — hud.md §4.9 (topbar_update) */
void race_clock_hud(void)
{
    if (DSW(DS_race_state) != 1 || DSB(DS_ext_view) != 0) {
        DSB(DS_clock_frames) = 0;
        return;
    }
    if (DSB(DS_clock_running) != 0) {
        DSB(DS_clock_frames)++;
        u8 f = DSB(DS_clock_frames);
        DSB(DS_clock_sub) = (u8)((u8)(f + (f >> 1)) >> 1);
        if (DSB(DS_clock_frames) < 5) goto draw;
        DSB(DS_clock_sec)++;
        DSB(DS_clock_sub) = 0;
    }
    DSB(DS_clock_frames) = 0;
    if (DSB(DS_clock_sec) >= 0x3C) {
        DSB(DS_clock_sec) = 0;
        DSB(DS_clock_min)++;
        if (DSB(DS_clock_min) >= 0x3C) DSB(DS_clock_min)--;   /* stops at 59 */
    }
draw:
    clock_draw();
    compass_draw();
    radar_detector();
}

/* 0e12:0b85 clock_draw — hud.md §4.9 */
void clock_draw(void)
{
    u8 s = DSB(DS_clock_sec);
    if (s == DSB(DS_clock_drawn)) return;
    DSB(DS_clock_drawn) = s;
    u16 qr = div16_8(s, 10);
    SCR_A = (u8)qr;
    SCR_B = (u8)(qr >> 8);
    gfx_set_colour(0);
    gfx_fill_rect(0x35, 0x3E, 8, 13);
    gfx_fill_rect(0x29, 0x32, 8, 13);                 /* y0, y1 re-used from the stack */
    gfx_set_colour(DSB(DS_car_colours + 9));          /* DS:CEB7 */
    gfx_move_to(0x35, 13);
    topbar_digit(SCR_A);
    gfx_move_to(0x3A, 13);
    topbar_digit(SCR_B);
    u8 m = DSB(DS_clock_min);
    if (m == 0) return;
    qr = div16_8(m, 10);
    SCR_A = (u8)qr;
    SCR_B = (u8)(qr >> 8);
    gfx_move_to(0x29, 13);
    if ((u8)(SCR_A << 1) != 0) topbar_digit(SCR_A);   /* no leading zero */
    gfx_move_to(0x2E, 13);
    topbar_digit(SCR_B);
}

/* 0e12:0cbe compass_draw — hud.md §4.9 */
void compass_draw(void)
{
    if (DSB(DS_debug_keys) == 0) {
        u8 x = (u8)((u8)((DSW(DS_cam_heading) >> 8) + DSB(DS_leg_compass_offset)) >> 1);
        u16 mode = DSW(DS_video_mode);
        if ((u8)mode != 0x13) {                        /* EGA / Tandy byte alignment (not reached) */
            x &= 0xFE;
            if ((u8)mode != 9) x &= 0xF8;
        }
        if (x == DSB(DS_compass_drawn)) return;
        DSB(DS_compass_drawn) = x;
        gfx_copy_rect(x, (s16)(x + 0x17), 0, 7, 8, 14, 1, 0);   /* page-1 strip -> screen */
        return;
    }
    gfx_set_colour(0);
    gfx_fill_rect(10, 0x13, 8, 13);
    gfx_set_colour(DSB(DS_car_colours + 8));          /* DS:CEB6 */
    u16 qr = div16_8(DSB(DS_frame_ticks), 10);
    SCR_A = (u8)qr;
    SCR_B = (u8)(qr >> 8);
    gfx_move_to(10, 13);
    topbar_digit(SCR_A);
    gfx_move_to(15, 13);
    topbar_digit(SCR_B);
}

/* 0e12:0db4 radar_detector — hud.md §4.9 (radar_draw) */
void radar_detector(void)
{
    DSB(DS_radar_phase)++;
    u8 ph = DSB(DS_radar_phase);
    u8 lvl = 0xFF;                                    /* blank phase */
    if (!(ph & 8)) {
        lvl = (u8)(DSB(DS_radar_level) >> 4);
        if (!(ph & 7) && lvl != 0) sfx_play_ax(0x0E); /* blip every 16 frames */
    }
    if (lvl == DSB(DS_radar_drawn)) return;
    DSB(DS_radar_drawn) = lvl;
    if ((s8)lvl < 0) {
        gfx_set_colour(0);
        gfx_fill_rect(0x120, 0x137, 9, 11);
        return;
    }
    SCR_A = lvl;
    gfx_set_colour(DSB(DS_car_colours + 0));          /* DS:CEAE power light */
    gfx_fill_rect(0x11C, 0x11F, 9, 10);
    gfx_set_colour(DSB(DS_car_colours + 1));          /* DS:CEAF bars */
    DSW(DS_vert_base) = 0x127;                        /* DS:945E used as the bar x */
    while ((s8)--SCR_A >= 0) {
        gfx_fill_rect((s16)(DSW(DS_vert_base) - 1), (s16)DSW(DS_vert_base), 9, 10);
        DSW(DS_vert_base) = (u16)(DSW(DS_vert_base) + 3);
    }
}
