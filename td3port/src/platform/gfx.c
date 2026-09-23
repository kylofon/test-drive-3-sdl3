/* Platform: the graphics library 16cf-1937, mode 13h paths — platform.md §4.2.
 *
 * Every primitive of the original dispatches on DS:BD44 (mode x 2). The port implements the mode 13h
 * handlers (the VGA build's only mode); for the other modes the drawing primitives do nothing (PORT: EGA,
 * Tandy, CGA, Hercules, text and the unchained mode 14h are not ported). gfx_set_mode fills the parameter
 * block from the per-mode tables DS:C128..C288 of the loaded EXE for every mode, as the original. */
#include "platform/platform.h"
#include "platform/plat_priv.h"
#include "platform/vga.h"
#include "host.h"

#include <string.h>

#define MODE13_X2 0x26          /* DS:BD44 for mode 13h */

static bool mode13(void) { return DSW(DS_gfx_mode_x2) == MODE13_X2; }

/* PORT: the BIOS video mode (INT 10h AH=0Fh) of the modelled machine: text mode 3 at start. */
static u8 bios_mode = 3;

/* INT 10h AH=0: mode 13h clears the 64 KB at A000h and loads the default palette (PORT: black; the game
 * uploads its palette before anything is shown, platform.md 4.2.1). Other modes: only the mode number. */
static void bios_set_mode(u8 mode)
{
    bios_mode = (u8)(mode & 0x7F);
    if (bios_mode == 0x13) {
        memset(mp(VRAM_SEG, 0), 0, 0x10000);
        for (int i = 0; i < 256; i++) vga_dac_write((u8)i, 0, 0, 0);
        vga_set_start(0);
    }
}

/* 16cf:0006 gfx_detect — platform.md §4.2.10 (PORT: VGA colour) */
s16 gfx_detect(void)
{
    return 0x12;
}

/* 16e5:0007 gfx_get_draw_seg — platform.md §4.2.2 */
u16 gfx_get_draw_seg(void)
{
    return DSW(DS_gfx_draw_seg);
}

/* 16e5:000b gfx_get_mode — platform.md §2.1 */
s16 gfx_get_mode(void)
{
    s8 m = DSC(DS_gfx_mode);
    if (m >= 0) return m;               /* cbw */
    return (s8)bios_mode;
}

/* 1905:000b gfx_set_mode — platform.md §4.2.1 */
void gfx_set_mode(s16 mode)
{
    if (mode > 0x14) return;
    DSB(DS_gfx_mode) = (u8)mode;
    if (mode < 0) mode = (s8)bios_mode;         /* INT 10h AH=0Fh, cbw */
    u8 m = (u8)mode;
    switch (m) {
    case 0x14:
        /* 0080: BIOS mode 13h + unchain. PORT: not ported (never selected by TD3); only the mode set. */
        bios_set_mode(0x13);
        break;
    case 0x0B: case 0x0C:
        /* 0033: Hercules set-up. PORT: not ported. */
        break;
    default: {
        /* 4, 5, 6, 8, 9, 0Ah first point INT 43h at the ROM 8x8 font (PORT: dropped); then 00e5: */
        s8 req = DSC(DS_gfx_mode);
        DSB(DS_gfx_mode) = m;
        if (req >= 0) bios_set_mode(m);
        break;
    }
    }
    /* common part 00f4 */
    u16 bx = (u16)(DSB(DS_gfx_mode) << 1);
    DSW(DS_gfx_mode_x2) = bx;
    bx = (u16)(bx + 2);
    DSB(DS_gfx_draw_page) = 0;
    DSB(DS_gfx_copy_page) = 0;
    DSB(DS_gfx_visible_page) = 0;
    DSB(DS_gfx_colour) = 0;
    DSB(DS_gfx_colour_raw) = 0;
    DSW(0xC3C1) = 0;
    DSW(DS_gfx_pen_x) = 0;
    DSW(DS_gfx_pen_y) = 0;
    u16 colours = DSW((u16)(DS_mode_colours + bx));
    DSW(DS_mode_colours) = colours;
    if ((u8)colours != 0) {                     /* or al, al: 100h (256 colours) skips both tables */
        DSW(DS_mode_expand_tab) = DSW((u16)(DS_mode_expand_tab + bx));
        u16 n = 0x100, di = DS_gfx_colour_map;
        while (n) {
            u16 cx = colours;
            u8 v = 0;
            do { DSB(di) = v; v++; di++; n--; } while (--cx);
        }
        n = 0x100; di = DS_gfx_colour_expand;
        while (n) {
            u16 cx = colours, si = DSW(DS_mode_expand_tab);
            do { DSW(di) = DSW(si); si += 2; di += 2; n--; } while (--cx);
        }
    }
    DSW(DS_mode_row_bytes) = DSW((u16)(DS_mode_row_bytes + bx));
    DSW(DS_mode_text_cols) = DSW((u16)(DS_mode_text_cols + bx));
    DSB(0xC3D6) = 0x19;                         /* text_rows */
    DSW(DS_mode_page_bytes) = DSW((u16)(DS_mode_page_bytes + bx));
    u16 seg = DSW((u16)(DS_mode_video_seg + bx));
    DSW(DS_mode_video_seg) = seg;
    DSW(DS_gfx_draw_seg) = seg;
    DSW(DS_gfx_copy_seg) = seg;
    DSW(DS_gfx_visible_seg) = seg;
    for (int i = 0; i < 16; i++) DSW(DS_gfx_page_seg + 2 * i) = seg;
    DSW(DS_mode_interleave) = DSW((u16)(DS_mode_interleave + bx));
    u16 w = DSW((u16)(DS_mode_width + bx));
    DSW(DS_mode_width) = w;
    DSW(DS_clip_x_min) = 0;
    DSW(DS_clip_x_max) = (u16)(w - 1);
    DSW(0xBD5B) = 0;
    DSW(0xBD59) = (u16)(w - 1);
    u16 h = DSW((u16)(DS_mode_height + bx));
    DSW(DS_mode_height) = h;
    DSW(DS_clip_y_min) = 0;
    DSW(DS_clip_y_max) = (u16)(h - 1);
    DSW(0xBD5F) = 0;
    DSW(0xBD5D) = (u16)(h - 1);
    s8 cm = DSC(DS_gfx_mode);
    if (cm >= 0x0B && cm <= 0x0C) {             /* Hercules: second page at B800h, own view box */
        DSW(DS_gfx_page_seg + 2) = 0xB800;
        if (cm == 0x0C) { DSW(0xBD5B) = 0x28; DSW(0xBD59) = 0x2A7; DSW(0xBD5F) = 0x18; DSW(0xBD5D) = 0x143; }
    } else if (cm > 0x0C && cm <= 0x12) {
        /* EGA/VGA 16-colour modes: GC register 1 = 0Fh; 11h/12h: 30 text rows and palette set-up.
         * PORT: registers not modelled. */
        if (cm > 0x10) DSB(0xC3D6) = 0x1E;
    }
}

/* 16fb:0008 gfx_move_to — platform.md §4.2.3 */
void gfx_move_to(s16 x, s16 y)
{
    DSS(DS_gfx_pen_x) = x;
    DSS(DS_gfx_pen_y) = y;
}

/* 1703:0001 gfx_set_colour — platform.md §4.2.3 (13h/14h: no mapping) */
void gfx_set_colour(s16 c)
{
    DSB(DS_gfx_colour_raw) = (u8)c;
    if (mode13() || DSW(DS_gfx_mode_x2) == 0x28) {
        DSB(DS_gfx_colour) = (u8)c;
    } else {
        /* PORT: other modes map through BDAC[c] and mask to their depth; approximated by the map alone */
        DSB(DS_gfx_colour) = DSB((u16)(DS_gfx_colour_map + (u8)c));
    }
}

/* 170e:0002 gfx_set_copy_page — platform.md §4.2.2 (13h path 001a) */
void gfx_set_copy_page(s16 p)
{
    p &= 15;
    DSB(DS_gfx_copy_page) = (u8)p;
    DSW(DS_gfx_copy_seg) = DSW(DS_gfx_page_seg + 2 * p);
}

/* 1714:000e gfx_set_draw_page — platform.md §4.2.2 (13h path 0026) */
void gfx_set_draw_page(s16 p)
{
    p &= 15;
    DSB(DS_gfx_draw_page) = (u8)p;
    DSW(DS_gfx_draw_seg) = DSW(DS_gfx_page_seg + 2 * p);
}

/* 171b:000a gfx_set_visible_page — platform.md §4.2.2 (13h path 006e) */
void gfx_set_visible_page(s16 p)
{
    p &= 15;
    if (DSB(DS_gfx_visible_page) == p) return;
    if (!mode13()) return;                      /* PORT: other modes not ported */
    u8 old = DSB(DS_gfx_visible_page);
    DSB(DS_gfx_visible_page) = (u8)p;
    u16 sa = DSW(DS_gfx_page_seg + 2 * p), sb = DSW(DS_gfx_page_seg + 2 * old);
    DSW(DS_gfx_page_seg + 2 * p) = sb;
    DSW(DS_gfx_page_seg + 2 * old) = sa;
    DSW(DS_gfx_draw_seg) = DSW(DS_gfx_page_seg + 2 * DSB(DS_gfx_draw_page));
    u16 n = DSW(DS_mode_page_bytes);
    for (u16 i = 0; i < n; i++) {               /* exchange the two blocks */
        u8 t = rd8(sa, i);
        wr8(sa, i, rd8(sb, i));
        wr8(sb, i, t);
    }
}

/* 172d:000e gfx_alloc_page — platform.md §4.2.2 (13h path 0027) */
s16 gfx_alloc_page(s16 p)
{
    if (!mode13()) return 0;                    /* PORT: RAM pages of CGA/Tandy/Hercules not ported */
    if (p <= 0 || p > 15) return 1;
    u16 err;
    u16 bytes = DSW(DS_mode_page_bytes);
    u16 seg = dos21_alloc((u16)(bytes >> 4), &err);
    if (!seg) return (s16)err;                  /* 7 or 8 */
    DSW(DS_gfx_page_seg + 2 * p) = seg;
    memset(mp(seg, 0), 0, bytes);
    return 0;
}

/* 1736:0004 gfx_free_page — platform.md §4.2.2 (13h path 0015) */
s16 gfx_free_page(s16 p)
{
    if (!mode13()) return 0;
    u16 err = dos21_free(DSW(DS_gfx_page_seg + 2 * (p & 15)));
    if (err == 7 || err == 9) return (s16)err;
    DSW(DS_gfx_page_seg + 2 * (p & 15)) = DSW(DS_mode_video_seg);
    return 0;
}

/* 173c:0005 gfx_put_pixel — platform.md §4.2.3 (13h path 0161) */
void gfx_put_pixel(s16 x, s16 y)
{
    if (x < DSS(DS_clip_x_min) || x > DSS(DS_clip_x_max)) return;
    if (y < DSS(DS_clip_y_min) || y > DSS(DS_clip_y_max)) return;
    if (!mode13()) return;
    wr8(DSW(DS_gfx_draw_seg), (u16)(plat_row320((u16)y) + (u16)x), DSB(DS_gfx_colour));
}

/* 1785:000b gfx_fill_rect — platform.md §4.2.4 (13h path 02c4: unclipped, bottom row first) */
void gfx_fill_rect(s16 x0, s16 x1, s16 y0, s16 y1)
{
    if (!mode13()) return;
    u16 seg = DSW(DS_gfx_draw_seg);
    u16 di = (u16)(plat_row320((u16)y1) + (u16)x0);
    s16 rows = (s16)(y1 + 1 - y0);
    s16 w = (s16)(x1 + 1 - x0);
    /* PORT: rows <= 0 (loop runs 65536 times) or w < 0 (rep stosb of 64 K) never happen in TD3: nothing. */
    if (rows <= 0 || w < 0) return;
    u8 c = DSB(DS_gfx_colour);
    while (rows--) {
        for (s16 i = 0; i < w; i++) wr8(seg, di++, c);
        di = (u16)(di - (0x140 + w));
    }
}

/* 192e:0005 gfx_fill_rect_clipped — platform.md §4.2.4 */
void gfx_fill_rect_clipped(s16 x0, s16 x1, s16 y0, s16 y1)
{
    if (x0 > DSS(DS_clip_x_max)) return;
    if (x0 < DSS(DS_clip_x_min)) x0 = DSS(DS_clip_x_min);
    if (x1 < DSS(DS_clip_x_min)) return;
    if (x1 > DSS(DS_clip_x_max)) x1 = DSS(DS_clip_x_max);
    if (y0 > DSS(DS_clip_y_max)) return;
    if (y0 < DSS(DS_clip_y_min)) y0 = DSS(DS_clip_y_min);
    if (y1 < DSS(DS_clip_y_min)) return;
    if (y1 > DSS(DS_clip_y_max)) y1 = DSS(DS_clip_y_max);
    gfx_fill_rect(x0, x1, y0, y1);
}

/* 1937:0003 gfx_clear_page — platform.md §4.2.4 (13h path 0027: zero-fill C1AC bytes of the draw page) */
void gfx_clear_page(void)
{
    if (!mode13()) return;
    u16 seg = DSW(DS_gfx_draw_seg), words = (u16)(DSW(DS_mode_page_bytes) >> 1);
    for (u16 di = 0; words; words--, di = (u16)(di + 2)) wr16(seg, di, 0);
}

/* 16d9:0002 gfx_line_to — platform.md §4.2.3 */
void gfx_line_to(s16 x, s16 y)
{
    s16 px = DSS(DS_gfx_pen_x), py = DSS(DS_gfx_pen_y);
    if (x == px) {                              /* vertical (or a single point) */
        DSS(DS_gfx_pen_y) = y;
        s16 lo = y, hi = py;
        if (py < y) { lo = py; hi = y; }
        gfx_fill_rect_clipped(px, x, lo, hi);
        return;
    }
    if (y == py) {                              /* horizontal */
        DSS(DS_gfx_pen_x) = x;
        s16 lo = px, hi = x;
        if (px > x) { lo = x; hi = px; }
        gfx_fill_rect_clipped(lo, hi, y, py);
        return;
    }
    s16 ady = (s16)(y - py), sy = 1;
    if (ady < 0) { sy = -1; ady = (s16)-ady; }
    DSS(0xC304) = sy;
    s16 adx = (s16)(x - px), sx = 1;
    if (adx < 0) { sx = -1; adx = (s16)-adx; }
    DSS(0xC302) = sx;
    s16 major = adx, minor = ady, stx = sx, sty = 0;
    if (adx < ady) { major = ady; minor = adx; stx = 0; sty = sy; }
    DSS(0xC306) = stx;                          /* straight step */
    DSS(0xC308) = sty;
    DSS(0xC30C) = (s16)(minor << 1);            /* inc_s */
    s16 d = (s16)((minor << 1) - major);
    DSS(0xC30A) = (s16)(d - major);             /* inc_d */
    s16 cx = px, cy = py;
    u16 n = (u16)(major + 1);
    for (;;) {
        gfx_put_pixel(cx, cy);
        if (--n == 0) break;
        if (d < 0) { cx = (s16)(cx + DSS(0xC306)); cy = (s16)(cy + DSS(0xC308)); d = (s16)(d + DSS(0xC30C)); }
        else       { cx = (s16)(cx + DSS(0xC302)); cy = (s16)(cy + DSS(0xC304)); d = (s16)(d + DSS(0xC30A)); }
    }
    DSS(DS_gfx_pen_x) = cx;
    DSS(DS_gfx_pen_y) = cy;
}

/* Rectangle copy between two segments, 13h: rows bottom-up, each row forward byte by byte. */
static void copy_rows(u16 sseg, u16 si, u16 dseg, u16 di, u16 w, u16 rows, u16 stride)
{
    /* PORT: rows = 0 (65536 iterations in the original) never happens in TD3: nothing. */
    while (rows--) {
        for (u16 i = 0; i < w; i++) wr8(dseg, (u16)(di + i), rd8(sseg, (u16)(si + i)));
        si = (u16)(si - stride);
        di = (u16)(di - stride);
    }
}

/* 17be:0009 gfx_copy_rect_to_copy_page — platform.md §4.2.5 (13h path 01f2; draw page -> copy page; no callers) */
void gfx_copy_rect_to_copy_page(s16 x0, s16 x1, s16 y0, s16 y1)
{
    if (!mode13()) return;
    u16 off = (u16)(plat_row320((u16)y1) + (u16)x0);
    copy_rows(DSW(DS_gfx_draw_seg), off, DSW(DS_gfx_copy_seg), off, (u16)(x1 + 1 - x0), (u16)(y1 + 1 - y0), 0x140);
}

/* 17eb:0006 gfx_copy_rect_from_copy_page — platform.md §4.2.5 (13h path 01ef; copy page -> draw page) */
void gfx_copy_rect_from_copy_page(s16 x0, s16 x1, s16 y0, s16 y1)
{
    if (!mode13()) return;
    u16 off = (u16)(plat_row320((u16)y1) + (u16)x0);
    copy_rows(DSW(DS_gfx_copy_seg), off, DSW(DS_gfx_draw_seg), off, (u16)(x1 + 1 - x0), (u16)(y1 + 1 - y0), 0x140);
}

/* 1818:0003 gfx_copy_rect — platform.md §4.2.5 (13h path 0227; linear, C154 = 0) */
void gfx_copy_rect(s16 x0, s16 x1, s16 y0, s16 y1, s16 dx, s16 dy_bottom, s16 src_page, s16 dst_page)
{
    if (!mode13()) return;
    u16 si = (u16)(plat_row320((u16)y1) + (u16)x0);
    u16 di = (u16)(plat_row320((u16)dy_bottom) + (u16)dx);
    u16 dseg = DSW((u16)(DS_gfx_page_seg + 2 * dst_page));   /* [bp+14h] << 1, no masking */
    u16 sseg = DSW((u16)(DS_gfx_page_seg + 2 * src_page));
    copy_rows(sseg, si, dseg, di, (u16)(x1 + 1 - x0), (u16)(y1 + 1 - y0), DSW(DS_mode_row_bytes));
}

/* 185f:000b gfx_draw_bitmap — platform.md §4.2.6 (13h path 0453: 1 bpp, set bits = BD41, rows upward) */
void gfx_draw_bitmap(u16 bits_ds, s16 bytes_per_row, s16 rows)
{
    if (!mode13()) return;
    u16 seg = DSW(DS_gfx_draw_seg);
    u16 bx = (u16)(plat_row320(DSW(DS_gfx_pen_y)) + DSW(DS_gfx_pen_x));
    u8 c = DSB(DS_gfx_colour);
    u16 si = bits_ds;
    u16 di = (u16)rows;
    /* PORT: rows or bytes_per_row = 0 (65536 iterations) never happen in TD3: nothing. */
    if (di == 0 || bytes_per_row == 0) return;
    do {
        u16 p = bx;
        u16 dx = (u16)bytes_per_row;
        do {
            u8 b = DSB(si++);
            for (u8 m = 0x80; m; m >>= 1, p++)
                if (b & m) wr8(seg, p, c);
        } while (--dx);
        bx = (u16)(bx - 0x140);
    } while (--di);
}

/* 18b3:0004 gfx_read_bitmap — platform.md §4.2.6 (13h path 030e: bit = pixel == BD41, rows upward) */
void gfx_read_bitmap(u16 dst_ds, s16 bytes_per_row, s16 rows)
{
    if (!mode13()) return;
    u16 seg = DSW(DS_gfx_draw_seg);
    u16 bx = (u16)(plat_row320(DSW(DS_gfx_pen_y)) + DSW(DS_gfx_pen_x));
    u8 c = DSB(DS_gfx_colour);
    u16 di = dst_ds;
    u16 n = (u16)rows;
    if (n == 0 || bytes_per_row == 0) return;   /* PORT: as gfx_draw_bitmap */
    do {
        u16 p = bx;
        u16 dx = (u16)bytes_per_row;
        do {
            u8 b = 0;
            for (u8 m = 0x80; m; m >>= 1, p++)
                if (rd8(seg, p) == c) b |= m;
            DSB(di++) = b;
        } while (--dx);
        bx = (u16)(bx - 0x140);
    } while (--n);
}

/* 1776:0008 gfx_set_display_offset — platform.md §4.2.9 (13h/14h path 0080: CRTC start = y*80 + x/4)
 * PORT: the wait for a vertical-retrace edge before the CRTC write is dropped; the new start applies at the
 * next present (platform.md §7, open question 2). */
void gfx_set_display_offset(s16 x, s16 y)
{
    if (!mode13() && DSW(DS_gfx_mode_x2) != 0x28) return;
    u16 start = (u16)((u16)y * 0x50 + ((u16)x >> 2));
    wr16(0x0040, 0x004E, start);                /* BIOS 0040:004E (page offset) */
    vga_set_start((u16)(start << 2));           /* double-word mode: 4 bytes per CRTC address */
}

/* 18f3:0002 gfx_set_ega_palette — platform.md §4.2.10 (13h path 004e: EGA words rgbRGB -> DAC 0-15
 * through C380 = {0, 2Ah, 15h, 3Fh}; never called in the VGA build) */
void gfx_set_ega_palette(u16 pal16_ds)
{
    if (!mode13()) return;                      /* PORT: Tandy / EGA attribute paths not ported */
    for (int i = 0; i < 16; i++) {
        u16 v = DSW((u16)(pal16_ds + 2 * i));
        /* index into C380 = low bit | high bit << 1: red bits 2/5, green 1/4, blue 0/3 (004e) */
        u8 r = DSB((u16)(0xC380 + (((v >> 2) & 1) | ((v >> 5) & 1) << 1)));
        u8 g = DSB((u16)(0xC380 + (((v >> 1) & 1) | ((v >> 4) & 1) << 1)));
        u8 b = DSB((u16)(0xC380 + (((v >> 0) & 1) | ((v >> 3) & 1) << 1)));
        vga_dac_write((u8)i, r, g, b);          /* INT 10h AX=1012h, DAC 0-15 */
    }
}

/* 16fc:000b text_exit_clear — platform.md §4.2.8 (text modes only: DOS "ESC[2J", clear; port: nothing) */
void text_exit_clear(void)
{
}
