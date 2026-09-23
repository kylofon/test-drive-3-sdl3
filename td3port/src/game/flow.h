#pragma once
/* Game flow module (game_flow.md): segments 0000, 01f4, 0792 except the per-frame HUD — main state
 * machine, start-up/shut-down, files and archives, messages, menus, race_run, results.
 * Functions other modules call. */
#include "mem.h"

/* 0000:0000 main — game_flow.md §4.1 (FN_main; state machine on DS:0086; ends through exit_to_dos /
 * fatal_exit -> crt_exit, so it does not return in practice; main.c returns its value) */
int game_main(void);

/* 0000:0d62 pal_load — game_flow.md §4.3 (112 colours -> DS:0B6A at 16 + DS:90F0; pal_set if VGA and game_state != 5) */
void pal_load(u16 name_ds);
/* 0000:0ee0 file_load_far — game_flow.md §4.3 (whole file / archive entry into dst; DS:E86A = bytes read, DS:E86C = 0) */
void file_load_far(u16 name_ds, FarPtr dst);
/* 0000:0f58 random — game_flow.md §4.4 (seed DS:00D2 = seed*41C64E6Dh + 3039h; returns (seed >> 16) & 7FFFh) */
u16 random(void);
/* 0000:0f80 key_poll — game_flow.md §4.4 (key from DS:915B or the joystick, hotkeys, DS:E08C; host pointer,
 * e.g. key_poll(&DSW(0x90DA)) from dissolve_poll) */
void key_poll(u16 *key);
/* 0000:1518 nop_1518 — game_flow.md §2a (empty) */
void nop_1518(void);
/* 0000:151a nop_151a — game_flow.md §2a (empty) */
void nop_151a(void);
/* 0000:179c message_box — game_flow.md §4.5 (message DS:16DA[id] in a grey box, per-id wait/prompt; answer in DS globals) */
void message_box(s16 id);
/* 0000:1e44 joystick_keys — game_flow.md §4.4 (if DS:0092: button -> 0Dh, else direction -> DS:00AA[..]; *key = result) */
u16 joystick_keys(u16 *key);
/* 01f4:1864 gfx_frame — game_flow.md §4.6 (rectangle outline with gfx_move_to / gfx_line_to) */
void gfx_frame(s16 x0, s16 x1, s16 y0, s16 y1);
/* 01f4:1a82 print_records — game_flow.md §4.6 ({col,row,text} records at s_ds+off, 80h next / AAh end; returns the offset after AAh) */
s16 print_records(u16 s_ds, s16 off);
/* 01f4:1c00 ega_pal_black — game_flow.md §4.6 (clears DS:E788[32]; EGA palette only outside VGA) */
void ega_pal_black(void);
/* 01f4:1c34 ega_pal_restore — game_flow.md §4.6 (EGA palette from DS:0E6A outside VGA; VGA: nothing) */
void ega_pal_restore(void);
/* 01f4:1c48 pal_apply — game_flow.md §4.6 (VGA: pal_set) */
void pal_apply(void);
/* 01f4:59ac pic_draw — game_flow.md §4.6 (VGA: rle_draw_page(pairs, npairs, width, x, y_bottom); flag unused) */
void pic_draw(u16 pairs_ds, u16 npairs, u16 width, s16 x, s16 y_bottom, s16 flag);
