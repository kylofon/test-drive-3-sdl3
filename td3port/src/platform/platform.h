#pragma once
/* Platform module (platform.md): segment 0c1c (timer/keyboard ISRs, joystick, DOS file wrappers, picture
 * drawers, palette upload and fades, 8x8 text, view presenter), the graphics library 16cf-1937 (mode 13h
 * paths only), the LZW helpers 0ab4:000a-01f0, and the few MS C 5.1 runtime functions the game calls.
 *
 * Conventions (PORTING.md): MSC `int` = s16. Near data pointers are DGROUP offsets (`u16 x_ds`), far
 * pointers are FarPtr. Graphics-library primitives that always return 0 in the original are void here.
 * A pointer argument that callers fill from a stack local (key codes, mouse position, the character
 * given to text_draw_char) is a host pointer; pass `&DSW(off)` / `&DSS(off)` / `&DSB(off)` when the
 * original passed a DGROUP address.
 *
 * Every busy-wait of the original (dissolve, fades, bios_wait_ticks, vretrace waits) calls host_pump()
 * per iteration / host_wait_vretrace() (host.h). Page 0 is VGA memory in mem[] (A000:0000); the DAC and
 * CRTC start go through platform/vga.h. The font (0c1c:0262), the palette buffer DS:0B6A and all other
 * tables are read from mem[] (the loaded EXE image), not embedded. */
#include "mem.h"

/* ---- Port set-up (no original address) -------------------------------------------------------------
 * Called once by modules_init() (game/game.h) before game_main(). Installs with the host:
 *   - the tick handler (host_set_tick_handler): the body of timer_isr 0c1c:10e8 = snd_tick(),
 *     DS:00A0++, BIOS tick every 8th tick. It is inert until timer_install() (called by snd_init) and
 *     again after timer_restore(); the BIOS tick counter used by bios_wait_ticks keeps running at
 *     18.2 Hz in both states (host-side static, PORT).
 *   - the keyboard handler (host_set_kbd_handler): kbd_isr 0c1c:0e98, active between kbd_install and
 *     kbd_restore (host-side "INT 9 hooked" flag, PORT: kbd_restore does not clear DS:009D); bytes
 *     arriving while it is not hooked go to the "BIOS" (dropped).
 * No platform function is stored as a code pointer in game data, so there are no codeptr registrations. */
void platform_init(void);

/* ---- LZW helpers (0ab4) ---- */
/* 0ab4:000a nop_far — platform.md §2.4 (empty; called by key_poll) */
void nop_far(void);
/* 0ab4:000f lzw_alloc — platform.md §2.4 (DOS 48h 300h paragraphs -> DS:16B8, far ptr DS:E7DC; 1 ok, 0 fail) */
s16 lzw_alloc(void);
/* 0ab4:0034 lzw_free — platform.md §2.4 (DOS 49h on DS:16B8) */
void lzw_free(void);
/* 0ab4:0047 lzw_decode — platform.md §4.5 (resets the decoder, DS:BD3F = 1, decodes src -> dst) */
void lzw_decode(FarPtr src, FarPtr dst);

/* ---- Segment 0c1c: input, timer, files, misc ---- */
/* 0c1c:0662 enable_interrupts — platform.md §2.3 (sti; port: no-op) */
void enable_interrupts(void);
/* 0c1c:0664 joy_read_raw — platform.md §4.4.3 (axes -> DS:00C2/DS:00CC unless a tick hit; returns (~port 201h) & 30h) */
u16 joy_read_raw(void);
/* 0c1c:06d8 joy_read — platform.md §4.4.3 (joy_read_raw, clamp x <= 4*DS:00BE, y <= 4*DS:00C8; returns the buttons) */
u16 joy_read(void);
/* 0c1c:084e dos_seek — platform.md §2.3 (INT 21h 4200h to hi:lo; 1 ok, 0 error) */
s16 dos_seek(s16 fh, u16 lo, u16 hi);
/* 0c1c:0868 dos_open_read — platform.md §2.3 (INT 21h 3D00h, name = DGROUP string; handle or -1) */
s16 dos_open_read(u16 name_ds);
/* 0c1c:087a dos_file_size — platform.md §2.3 (low word of the size, file rewound to 0) */
u16 dos_file_size(s16 fh);
/* 0c1c:08a0 dos_read — platform.md §2.3 (INT 21h 3Fh into buf; returns AX = bytes read) */
u16 dos_read(FarPtr buf, u16 n, s16 fh);
/* 0c1c:08b9 dos_close — platform.md §2.3 (INT 21h 3Eh) */
void dos_close(s16 fh);
/* 0c1c:0e2d kbd_install — platform.md §4.4.1 (clears the ISR state, DS:009D = 1) */
void kbd_install(void);
/* 0c1c:0e7d kbd_restore — platform.md §2.3 (if DS:009D: unhook kbd_isr; DS:009D stays set) */
void kbd_restore(void);
/* 0c1c:109f timer_install — platform.md §4.4.2 (enables the 145.652 Hz ISR body; the DS=CS store bug has no effect) */
void timer_install(void);
/* 0c1c:10cf timer_restore — platform.md §4.4.2 (disables the ISR body) */
void timer_restore(void);

/* ---- Segment 0c1c: palette, pictures, pages, text, presenter ---- */
/* 0c1c:08c5 dissolve_page1_to_0 — platform.md §4.3.5 (64 ordered steps, one per tick, abort on DS:008C) */
void dissolve_page1_to_0(void);
/* Names below carry the address suffix because game_flow has wrappers with the same original names
 * (01f4:1c56..1c80); FN_<name> in symbols.h matches. */
/* 0c1c:0b5b pal_fade_out — platform.md §4.3.2 (16 uploads of DS:0B6A scaled 15..0) */
void pal_fade_out_0C1C_0B5B(void);
/* 0c1c:0b89 page_clear_low_colours — platform.md §4.3.4 (page DS:009A: bytes < 80h := 0) */
void page_clear_low_colours(void);
/* 0c1c:0bb7 pal_fade_out_low — platform.md §4.3.2 (fade out colours 0-127 only) */
void pal_fade_out_low_0C1C_0BB7(void);
/* 0c1c:0be5 pal_fade_in — platform.md §4.3.2 (16 uploads, levels 1..16) */
void pal_fade_in_0C1C_0BE5(void);
/* 0c1c:0c16 pal_black — platform.md §4.3.2 (uploads 768 zeros) */
void pal_black_0C1C_0C16(void);
/* 0c1c:0c2a pal_set — platform.md §4.3.2 (uploads DS:0B6A unchanged) */
void pal_set(void);
/* 0c1c:0c45 rle_draw_page — platform.md §4.3.3 ((colour + DS:90F0, count) pairs into page DS:009A, bottom row first) */
void rle_draw_page(u16 pairs_ds, u16 npairs, u16 width, s16 x, s16 y_bottom);
/* 0c1c:11f7 rect_save — platform.md §4.3.4 (page DS:009A rectangle -> DS:90D0:0000, packed rows) */
void rect_save(s16 x0, s16 x1, s16 y0, s16 y1);
/* 0c1c:1303 rect_restore — platform.md §4.3.4 (inverse of rect_save) */
void rect_restore(s16 x0, s16 x1, s16 y0, s16 y1);
/* 0c1c:13d8 view_present — platform.md §4.3.7 (mirror_present, then 3D buffer DS:90D0 -> page 0) */
void view_present(void);
/* 0c1c:15c7 page_mirror_left_half — platform.md §4.3.4 (page DS:009A: pixel 319-x := pixel x, x < 160) */
void page_mirror_left_half(void);
/* 0c1c:1759 rle_draw_viewbuf — platform.md §4.3.3 (320-wide runs into DS:90D0, colour 0Fh transparent) */
void rle_draw_viewbuf(u16 pairs_ds, u16 npairs, s16 y_bottom);
/* 0c1c:0733 text_set_colours — platform.md §4.3.6 (DS:90E3 = fg << 8, DS:90E1 = bg << 8) */
void text_set_colours(s16 fg, s16 bg);
/* 0c1c:074c text_goto_cell — platform.md §4.3.6 (DS:90E7 = (u8)(row*8), DS:90E5 = col*8) */
void text_goto_cell(s16 row, s16 col);
/* 0c1c:076b text_goto — platform.md §4.3.6 (DS:90E7 = (u8)y, DS:90E5 = col*8) */
void text_goto(s16 y, s16 col);
/* 0c1c:0784 text_draw_char — platform.md §4.3.6 (draws c[0] only; the original's near char* — host pointer, see top) */
void text_draw_char(const u8 *c);
/* 0c1c:1f32 print_text_bios — platform.md §4.3.6 (text-mode string at (col,row); port: log / stderr) */
void print_text_bios(u16 s_ds, s16 col, s16 row);

/* ---- Graphics library (16cf-1937), mode 13h ---- */
/* 16cf:0006 gfx_detect — platform.md §4.2.10 (adapter code; port: 12h = VGA colour) */
s16 gfx_detect(void);
/* 16d9:0002 gfx_line_to — platform.md §4.2.3 (pen -> (x,y), ends inclusive, pen := (x,y)) */
void gfx_line_to(s16 x, s16 y);
/* 16e5:0007 gfx_get_draw_seg — platform.md §4.2.2 (returns DS:BD88) */
u16 gfx_get_draw_seg(void);
/* 16e5:000b gfx_get_mode — platform.md §2.1 (DS:BD49 if >= 0, else the BIOS mode) */
s16 gfx_get_mode(void);
/* 16e6:000e mouse_init — platform.md §4.2.7 (0 = no driver, else button count; sets BD46/BD47/BD4F/BD50) */
s16 mouse_init(void);
/* 16ec:0004 mouse_set_range — platform.md §4.2.7 (INT 33h 7/8, shifted) */
void mouse_set_range(s16 x0, s16 x1, s16 y0, s16 y1);
/* 16ef:0009 mouse_set_pos — platform.md §4.2.7 (INT 33h 4, shifted) */
void mouse_set_pos(s16 x, s16 y);
/* 16f1:000d mouse_get — platform.md §4.2.7 (INT 33h 3; host pointers, e.g. &DSS(0xE864)) */
void mouse_get(s16 *x, s16 *y, s16 *buttons);
/* 16f5:0008 mouse_show — platform.md §4.2.7 (INT 33h 1/2 on change; port: cursor never shown) */
void mouse_show(s16 on);
/* 16fb:0008 gfx_move_to — platform.md §4.2.3 (pen DS:BD4B/BD4D) */
void gfx_move_to(s16 x, s16 y);
/* 16fc:000b text_exit_clear — platform.md §4.2.8 (text mode only; port: nothing) */
void text_exit_clear(void);
/* 16ff:000b bios_wait_ticks — platform.md §4.2.8 (busy-wait n 18.2 Hz BIOS ticks, host_pump per iteration) */
void bios_wait_ticks(s16 n);
/* 1703:0001 gfx_set_colour — platform.md §4.2.3 (13h: BD41 = BD42 = c) */
void gfx_set_colour(s16 c);
/* 170e:0002 gfx_set_copy_page — platform.md §4.2.2 (BD43 = p, BD86 = BD8C[p & 15]; not a display select) */
void gfx_set_copy_page(s16 p);
/* 1714:000e gfx_set_draw_page — platform.md §4.2.2 (BD40 = p, BD88 = BD8C[p & 15]; game mirrors it in DS:009A) */
void gfx_set_draw_page(s16 p);
/* 171b:000a gfx_set_visible_page — platform.md §4.2.2 (swaps page blocks; TD3 calls it once with 0 = no-op) */
void gfx_set_visible_page(s16 p);
/* 172d:000e gfx_alloc_page — platform.md §4.2.2 (DOS 48h FA0h paragraphs -> BD8C[p], zeroed; 0 ok / DOS error 7,8 / 1 bad p) */
s16 gfx_alloc_page(s16 p);
/* 1736:0004 gfx_free_page — platform.md §4.2.2 (DOS 49h, BD8C[p] = A000h; 0 ok or DOS error) */
s16 gfx_free_page(s16 p);
/* 173c:0005 gfx_put_pixel — platform.md §4.2.3 (clipped to BD53..BD51 x BD57..BD55; draw[y*320+x] = BD41) */
void gfx_put_pixel(s16 x, s16 y);
/* 1776:0008 gfx_set_display_offset — platform.md §4.2.9 (CRTC start = y*80 + x/4 -> vga_set_start; screen shake) */
void gfx_set_display_offset(s16 x, s16 y);
/* 1785:000b gfx_fill_rect — platform.md §4.2.4 (inclusive, unclipped, colour BD41, bottom row first) */
void gfx_fill_rect(s16 x0, s16 x1, s16 y0, s16 y1);
/* 17eb:0006 gfx_copy_rect_from_copy_page — platform.md §4.2.5 (copy page BD86 -> draw page BD88) */
void gfx_copy_rect_from_copy_page(s16 x0, s16 x1, s16 y0, s16 y1);
/* 1818:0003 gfx_copy_rect — platform.md §4.2.5 (page src -> page dst, rows bottom-up, dst by left x / bottom row) */
void gfx_copy_rect(s16 x0, s16 x1, s16 y0, s16 y1, s16 dx, s16 dy_bottom, s16 src_page, s16 dst_page);
/* 185f:000b gfx_draw_bitmap — platform.md §4.2.6 (1 bpp at the pen, set bits = BD41, rows upward) */
void gfx_draw_bitmap(u16 bits_ds, s16 bytes_per_row, s16 rows);
/* 18b3:0004 gfx_read_bitmap — platform.md §4.2.6 (1 bpp mask of pixels == BD41 at the pen, rows upward) */
void gfx_read_bitmap(u16 dst_ds, s16 bytes_per_row, s16 rows);
/* 18f3:0002 gfx_set_ega_palette — platform.md §4.2.10 (EGA 16-colour palette; never called in the VGA build) */
void gfx_set_ega_palette(u16 pal16_ds);
/* 1905:000b gfx_set_mode — platform.md §4.2.1 (13h: parameter block, pages A000h, clears page 0 + DAC; other modes: PORT no-op) */
void gfx_set_mode(s16 mode);

/* ---- Added by the platform module (additional original functions; no existing prototype changed) ---- */
/* 0ab4:006a lzw_decode_body � platform.md �4.5 (decoder without the reset; the original enters it only by
 * falling through from lzw_decode) */
void lzw_decode_body(FarPtr src, FarPtr dst);
/* 0c1c:071c far_normalize � platform.md �2.3 (returns DX:AX = (seg + off/16) << 16; no callers) */
u32 far_normalize(u16 off, u16 seg);
/* 0c1c:0e98 kbd_isr � platform.md �4.4.1 (INT 9 body for one port 60h byte; the host feeds it through
 * platform_init's keyboard handler while INT 9 is hooked) */
void kbd_isr(u8 sc);
/* 0c1c:10e8 timer_isr � platform.md �4.4.2 (INT 8 body: snd_tick, DS:00A0++, BIOS clock every 8th tick;
 * run by platform_init's tick handler while INT 8 is hooked) */
void timer_isr(void);
/* 17be:0009 gfx_copy_rect_to_copy_page � platform.md �4.2.5 (draw page BD88 -> copy page BD86; no callers) */
void gfx_copy_rect_to_copy_page(s16 x0, s16 x1, s16 y0, s16 y1);
/* 192e:0005 gfx_fill_rect_clipped � platform.md �4.2.4 (clips to BD53..BD51 x BD57..BD55, then gfx_fill_rect) */
void gfx_fill_rect_clipped(s16 x0, s16 x1, s16 y0, s16 y1);
/* 1937:0003 gfx_clear_page � platform.md �4.2.4 (13h: zero-fills C1AC bytes of the draw page) */
void gfx_clear_page(void);

/* ---- PORT: DOS services (no original address) ------------------------------------------------------
 * INT 21h replacements for game code that calls DOS directly (sound's instr_load 1ace:02fa and its
 * dos_free 1bd7:000c, which is sound's own function and calls dos21_free; the platform's own lzw_alloc /
 * gfx_alloc_page) and for crt_fmalloc. The dos21_ prefix keeps them apart from original functions.
 * Memory blocks are carved out of mem[] between HEAP_BOTTOM and HEAP_TOP (mem.h); the segments returned
 * are real segments in mem[]. */
/* DOS 48h: returns the segment of a new block of `paragraphs`, or 0 with *err = 8 (not enough memory). */
u16 dos21_alloc(u16 paragraphs, u16 *err);
/* DOS 49h: frees the block at seg; returns 0 or the DOS error (9 = bad block). */
u16 dos21_free(u16 seg);
/* DOS 42h: whence 0 = SET, 1 = CUR, 2 = END (AL); returns the new position (DX:AX), 0xFFFFFFFF on error. */
u32 dos21_lseek(s16 fh, s32 offset, u8 whence);

/* ---- MS C 5.1 runtime (1940) ------------------------------------------------------------------------
 * Game code calls these runtime functions (interfaces_draft.txt). How each is ported:
 *
 *   1940:0746 strcpy, 1940:0778 strcmp, 1940:07a4 strlen (and 1424 strcat, 14dc memcpy, 180a memset,
 *   1464 itoa -> snprintf): plain C library calls on mem[] strings, e.g.
 *       strcpy(ds_str(DS_path_program + 2), "COMPASS.LZ");
 *   Keep every string in mem[] where the original has it (paths DS:0A9A etc.).
 *
 *   1940:08fc _aFldiv, 0998 _aFlmul, 09cc _aFNaldiv, 09f0 _aFNauldiv, 0a14 _aFuldiv, 0a76 _aFulrem:
 *   C operators / * % (and /=) on s32 / u32 (mem.h: truncation toward zero, as the runtime).
 *
 *   1940:08a6 _harderr, 08f3 _hardresume: dropped (INT 24h never happens; harderr_handler is unused).
 *   1940:07c4 getch: dropped with the text-mode setup screen (config_load uses the defaults when TD3.CFG
 *   is missing, game_flow.md §6).
 *
 *   1940:0308 fopen, 0334 fread, 0526 fwrite, 0240 fclose, 0681 _fmalloc, 066c _ffree, 07dc getcwd,
 *   01a2 exit: behaviour matters (files in the game directory, heap inside mem[], shutdown), so they are
 *   the platform functions below. The crt_ prefix avoids the C library names; FN_fopen etc. in
 *   symbols.h are never registered as code pointers. */

/* Host pointer to a DGROUP string (for the C string functions). */
static inline char *ds_str(u16 off) { return (char *)mp(DGROUP, off); }

/* 1940:0308 fopen — platform.md §2.6 (name/mode = DGROUP strings; drive prefix "X:" ignored, case-insensitive
 * lookup via host_game_path; "w" modes create in the game directory). Returns a nonzero handle, 0 = NULL. */
u16 crt_fopen(u16 name_ds, u16 mode_ds);
/* 1940:0334 fread — platform.md §2.6 (returns the number of complete items read) */
u16 crt_fread(FarPtr buf, u16 size, u16 count, u16 f);
/* 1940:0526 fwrite — platform.md §2.6 (returns the number of complete items written) */
u16 crt_fwrite(FarPtr buf, u16 size, u16 count, u16 f);
/* 1940:0240 fclose — platform.md §2.6 (0 ok, -1 error) */
s16 crt_fclose(u16 f);
/* 1940:0681 _fmalloc — platform.md §2.6 (far heap block inside mem[]; null FarPtr on failure) */
FarPtr crt_fmalloc(u16 size);
/* 1940:066c _ffree — platform.md §2.6 */
void crt_ffree(FarPtr p);
/* 1940:07dc getcwd — platform.md §2.6 (writes "C:\" into the DGROUP buffer; the drive letter is only patched
 * into paths whose drive prefix the port ignores; returns buf_ds) */
u16 crt_getcwd(u16 buf_ds, u16 size);
/* 1940:01a2 exit — platform.md §2.6 (host_shutdown(), then exit(code); used by exit_to_dos / fatal_exit) */
_Noreturn void crt_exit(s16 code);
