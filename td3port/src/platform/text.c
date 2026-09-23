/* Platform: 8x8 proportional text (0c1c:0733-0784) and BIOS text output (0c1c:1f32) — platform.md §4.3.6.
 * The font lives in the code segment of the loaded EXE: glyphs CS:0262 + 8c (bottom row first, MSB left),
 * widths CS:02E2 + c. */
#include "platform/platform.h"
#include "platform/plat_priv.h"

#include <stdio.h>

#define CS_FONT_GLYPHS 0x0262
#define CS_FONT_WIDTHS 0x02E2

/* 0c1c:0733 text_set_colours — platform.md §4.3.6 */
void text_set_colours(s16 fg, s16 bg)
{
    DSW(DS_text_fg) = (u16)(fg << 8);
    DSW(DS_text_bg) = (u16)(bg << 8);
}

/* 0c1c:074c text_goto_cell — platform.md §4.3.6 */
void text_goto_cell(s16 row, s16 col)
{
    DSB(DS_text_y) = (u8)(row << 3);
    DSW(DS_text_x) = (u16)(col << 3);
}

/* 0c1c:076b text_goto — platform.md §4.3.6 */
void text_goto(s16 y, s16 col)
{
    DSB(DS_text_y) = (u8)y;
    DSW(DS_text_x) = (u16)(col << 3);
}

/* 0c1c:0784 text_draw_char — platform.md §4.3.6 (draws c[0] only) */
void text_draw_char(const u8 *c)
{
    u8 ch = c[0];
    if (ch >= 0x20) {
        u16 si = (u16)(((u16)ch << 3) + CS_FONT_GLYPHS);   /* 8c + 362h - 100h */
        for (int i = 0; i < 8; i++) DSB(DS_text_glyph + i) = SEGB(PLAT_CS, (u16)(si + i));
        gfx_move_to((s16)DSW(DS_text_x), (s16)(DSB(DS_text_y) + 7));
        gfx_set_colour((s16)(DSW(DS_text_fg) >> 8));
        gfx_draw_bitmap(DS_text_glyph, 1, 8);
        gfx_set_colour((s16)(DSW(DS_text_bg) >> 8));
        if (DSB(DS_text_transparent) == 0) {
            for (int i = 0; i < 8; i += 2) DSW(DS_text_glyph + i) ^= 0xFFFF;
            gfx_draw_bitmap(DS_text_glyph, 1, 8);
        }
    }
    DSW(DS_text_x) = (u16)(DSW(DS_text_x) + SEGB(PLAT_CS, (u16)(CS_FONT_WIDTHS + ch)));
}

/* 0c1c:1f32 print_text_bios — platform.md §4.3.6 (INT 10h text output at (col,row), attribute 0Fh;
 * PORT: the text goes to stderr, the text screen does not exist) */
void print_text_bios(u16 s_ds, s16 col, s16 row)
{
    fprintf(stderr, "[%2d,%2d] %s\n", row, col, ds_str(s_ds));
}
