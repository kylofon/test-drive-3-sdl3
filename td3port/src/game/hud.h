#pragma once
/* HUD module (hud.md): cockpit set-up and per-frame instruments (0792:0414, 0792:0922-1c66), the top bar
 * (0e12:0b1d tree), chase-view panel and crash overlays. Functions other modules call. */
#include "mem.h"

/* 0792:0414 race_input — hud.md §4.2 (phase 0/1: joystick + controls poll (phase 1 only if DS:B70E > 28h); phase 2: key_poll + key_dispatch; wheel) */
void race_input(s16 phase);
/* 0792:04ca chase_view_exit — hud.md §4.10 (music, top bar and cockpit back from page 1, compass strip, instrument caches = FFh) */
void chase_view_exit(void);
/* 0792:059a chase_view_enter — hud.md §4.10 (music off, save top bar to page 1, blank it, replay_panel_draw, DS:CC92 = 1) */
void chase_view_enter(void);
/* 0792:0602 replay_panel_draw — hud.md §4.10 (grey panel rows 112-199, CHASE.LZ buttons, titles and labels) */
void replay_panel_draw(void);
/* 0792:0922 cockpit_setup — hud.md §4.3 (palettes, cockpit pictures, masks, backings, first frame, palette on) */
void cockpit_setup(void);
/* 0792:10a6 hud_reset_topbar — hud.md §2.1 (DS:B709 = FFh; DS:BD34 = FFFFh unless race_state 2; page 0) */
void hud_reset_topbar(void);
/* 0792:10c8 shifter_update — hud.md §4.8 (per frame: gear lever animation, show/hide) */
void shifter_update(void);
/* 0792:1310 wheel_update — hud.md §4.4 (rim picture by DS:E33A, steering marker) */
void wheel_update(void);
/* 0792:15d4 tach_draw — hud.md §4.5 (restore tach backing, needle when the step changed) */
void tach_draw(void);
/* 0792:16c4 odometer_draw — hud.md §4.6 (rolling odometer into the speedo backing; speedo needle) */
void odometer_draw(void);
/* 0792:1952 odometer_update — hud.md §4.7 (accumulate |velocity| in DS:1186, advance the odometer digits) */
void odometer_update(void);
/* 0792:19ca water_overlay_start — hud.md §4.11 (WATER.LZ into page 1, sfx 6, first water_roll_step) */
void water_overlay_start(void);
/* 0792:1a72 water_roll_step — hud.md §4.11 (page-1 water into the view area, rotate it down 2 rows) */
void water_roll_step(void);
/* 0792:1c00 broken_glass_overlay — hud.md §4.11 (sfx 2, BROKE.LZ over the 3D buffer, 0Fh transparent, view_present) */
void broken_glass_overlay(void);
/* 0792:1c66 cockpit_pictures_draw — hud.md §4.3 (2.BOT / 1.BOT at bottom_y, .TOP and page copy when 199, compass strip) */
void cockpit_pictures_draw(s16 bottom_y);
/* 0e12:0b1d race_clock_hud — hud.md §4.9 (near; from frame_update: race clock tick, clock / compass / radar draws) */
void race_clock_hud(void);

/* ---- Added by the hud module (internal to hud*.c; declared here so the hud files can call each other) */
/* 0792:0cec view_first_frame — hud.md §4.3 (frame_update + frame_draw + view_present unless chase view) */
void view_first_frame(void);
/* 0792:1156 shift_lever_draw — hud.md §4.8 (save screen, compose gate + knob at DS:CE94, copy to screen) */
void shift_lever_draw(void);
/* 0792:1488 wheel_rim_blit — hud.md §4.4 (rim picture from page 1 + lower gauge backings; far void(int dy, int dx)) */
void wheel_rim_blit(s16 dy, s16 dx);
/* 0792:155e steer_marker_draw — hud.md §4.4 (8x5 marker glyph at the wheel angle, current page) */
void steer_marker_draw(void);
/* 0792:18da speedo_draw — hud.md §4.5 (speedometer needle, page 0) */
void speedo_draw(void);
/* 0792:1dfe load_opponent_pob_nonvga — hud.md §2.1 (non-VGA only; no-op in mode 13h) */
void load_opponent_pob_nonvga(void);
/* 0e12:0b85 clock_draw — hud.md §4.9 (near; race clock m:ss when the second changed) */
void clock_draw(void);
/* 0e12:0cbe compass_draw — hud.md §4.9 (near; compass window or debug frame-time digits) */
void compass_draw(void);
/* 0e12:0db4 radar_detector — hud.md §4.9 (near; radar_draw: bars, blink, blip sfx 0Eh) */
void radar_detector(void);
