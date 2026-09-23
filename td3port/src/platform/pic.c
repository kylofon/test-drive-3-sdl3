/* Platform: LZW decoder (0ab4:000a-01f0) and the RLE picture drawers (0c1c:0c45, 0c1c:1759) —
 * platform.md §4.5, §4.3.3, FORMATS.md "Pictures".
 * All decoder state is at its DGROUP address (DS:12AC..16D2, DS:E7DC); the input window is DS:12B4[400h];
 * the dictionary is the DOS block DS:16B8 (3 bytes per code: u16 prefix, u8 char). */
#include "platform/platform.h"
#include "platform/plat_priv.h"

/* 0ab4:000a nop_far — platform.md §2.4 */
void nop_far(void)
{
}

/* 0ab4:000f lzw_alloc — platform.md §2.4 (DOS 48h 300h paragraphs; 1 ok, 0 fail) */
s16 lzw_alloc(void)
{
    u16 err;
    u16 seg = dos21_alloc(0x300, &err);
    /* the original stores AX (the segment, or the error code on failure) before testing CF */
    u16 ax = seg ? seg : err;
    DSW(DS_lzw_dict_seg) = ax;
    DSW(DS_mirror_block + 2) = ax;
    DSW(DS_mirror_block) = 0;
    return seg ? 1 : 0;
}

/* 0ab4:0034 lzw_free — platform.md §2.4 (DOS 49h) */
void lzw_free(void)
{
    dos21_free(DSW(DS_lzw_dict_seg));
}

/* 0ab4:0140 lzw_fill_input — copies 400h source bytes to DS:12B4, advances the source offset */
static void lzw_fill_input(void)
{
    FarPtr dst = ds_far(0x16B4);                /* = 1BE4:12B4 (static far pointer) */
    FarPtr src = ds_far(DS_lzw_src);
    for (u16 i = 0; i < 0x400; i++)
        wr8(dst.seg, (u16)(dst.off + i), rd8(src.seg, (u16)(src.off + i)));
    DSW(DS_lzw_src) = (u16)(src.off + 0x400);
}

/* 0ab4:015c lzw_get_code — next nbits-bit code (LSB first), refilling the input window */
static u16 lzw_get_code(void)
{
    u16 pos = DSW(DS_lzw_bitpos);
    DSW(DS_lzw_bitpos) = (u16)(pos + DSW(DS_lzw_nbits));
    u16 byte = pos >> 3, bit = pos & 7;
    if ((s16)byte >= 0x3FD) {                   /* cmp ax, 3FDh / jl: keep the tail, append new bytes */
        DSW(DS_lzw_bitpos) = (u16)(bit + DSW(DS_lzw_nbits));
        u16 keep = (u16)(0x400 - byte);
        u16 di = DS_lzw_inbuf;
        for (u16 i = 0; i < keep; i++, di++) DSB(di) = DSB((u16)(DS_lzw_inbuf + byte + i));
        FarPtr src = ds_far(DS_lzw_src);
        u16 dseg = DSW(0x16B6);
        for (u16 i = 0; i < byte; i++, di++) wr8(dseg, di, rd8(src.seg, (u16)(src.off + i)));
        DSW(DS_lzw_src) = (u16)(src.off + byte);
        byte = 0;
    }
    u16 si = (u16)(DS_lzw_inbuf + byte);
    u32 w = DSB(si) | (u32)DSB((u16)(si + 1)) << 8 | (u32)DSB((u16)(si + 2)) << 16;
    u16 ax = (u16)(w >> bit);                   /* shr al, 1 / rcr bx, 1 per bit */
    return ax & DSW((u16)(DS_lzw_masks + ((DSW(DS_lzw_nbits) - 9) << 1)));
}

/* 0ab4:01ca lzw_reset_width */
static void lzw_reset_width(void)
{
    DSW(DS_lzw_nbits) = 9;
    DSW(DS_lzw_maxcode) = 0x200;
    DSW(DS_lzw_next) = 0x102;
}

/* 0ab4:01dd lzw_out — *dst++ = al (offset only) */
static void lzw_out(u8 al)
{
    FarPtr d = ds_far(DS_lzw_dst);
    wr8(d.seg, d.off, al);
    DSW(DS_lzw_dst) = (u16)(d.off + 1);
}

/* 0ab4:01e9 lzw_idx3 — bx * 3 */
static u16 lzw_idx3(u16 bx)
{
    return (u16)(bx * 3);
}

/* 0ab4:01f0 lzw_add_entry — dict[next] = {old, firstchar}
 * PORT: at 12 bits `next` keeps growing past FFFh and the original writes behind the 3000h-byte block;
 * the port skips those writes (platform.md 4.5; never happens with the shipped pictures). */
static void lzw_add_entry(void)
{
    u16 next = DSW(DS_lzw_next);
    if (next >= 0x1000) return;
    u16 bx = lzw_idx3(next);
    u16 seg = DSW(DS_lzw_dict_seg);
    wr8(seg, (u16)(bx + 2), DSB(DS_lzw_firstchar));
    wr16(seg, bx, DSW(DS_lzw_old));
}

/* 0ab4:006a lzw_decode_body — platform.md §4.5 (the decoder without the reset; entered by fall-through) */
void lzw_decode_body(FarPtr src, FarPtr dst)
{
    /* The original pushes the expanded characters on the CPU stack (count DS:16C2). */
    u8 stack[0x1001 + 1];

    ds_far_wr(DS_lzw_src, src);
    ds_far_wr(DS_lzw_dst, dst);
    lzw_fill_input();
    for (;;) {
        u16 code = lzw_get_code();
        if (code == 0x101) return;
        if (code == 0x100) {
            lzw_reset_width();
            code = lzw_get_code();
            DSW(DS_lzw_cur) = code;
            DSW(DS_lzw_old) = code;
            DSB(DS_lzw_firstchar) = (u8)code;
            DSB(DS_lzw_finchar) = (u8)code;
            lzw_out(DSB(DS_lzw_firstchar));
            continue;
        }
        DSW(DS_lzw_cur) = code;
        DSW(DS_lzw_incode) = code;
        u16 dseg = DSW(DS_lzw_dict_seg);
        if ((s16)code >= (s16)DSW(DS_lzw_next)) {          /* KwKwK */
            DSW(DS_lzw_cur) = DSW(DS_lzw_old);
            stack[DSW(DS_lzw_stack_n)] = DSB(DS_lzw_finchar);
            DSW(DS_lzw_stack_n)++;
        }
        while ((s16)DSW(DS_lzw_cur) > 0xFF) {
            u16 bx = lzw_idx3(DSW(DS_lzw_cur));
            /* PORT: the CPU stack depth is bounded by the dictionary chain in valid data; a corrupt stream
             * (a prefix loop) would overflow the original's stack: stop there. */
            if (DSW(DS_lzw_stack_n) >= 0x1001) return;
            stack[DSW(DS_lzw_stack_n)] = rd8(dseg, (u16)(bx + 2));
            DSW(DS_lzw_stack_n)++;
            DSW(DS_lzw_cur) = rd16(dseg, bx);
        }
        u8 c = (u8)DSW(DS_lzw_cur);
        DSB(DS_lzw_finchar) = c;
        DSB(DS_lzw_firstchar) = c;
        stack[DSW(DS_lzw_stack_n)] = c;
        DSW(DS_lzw_stack_n)++;
        for (u16 n = DSW(DS_lzw_stack_n); n; n--) lzw_out(stack[n - 1]);
        DSW(DS_lzw_stack_n) = 0;
        lzw_add_entry();
        DSW(DS_lzw_next)++;
        DSW(DS_lzw_old) = DSW(DS_lzw_incode);
        if ((s16)DSW(DS_lzw_next) >= (s16)DSW(DS_lzw_maxcode) && DSW(DS_lzw_nbits) != 12) {
            DSW(DS_lzw_nbits)++;
            DSW(DS_lzw_maxcode) = (u16)(DSW(DS_lzw_maxcode) << 1);
        }
    }
}

/* 0ab4:0047 lzw_decode — platform.md §4.5 */
void lzw_decode(FarPtr src, FarPtr dst)
{
    DSB(DS_lzw_mirror_dirty) = 1;
    DSW(DS_lzw_next) = 0x102;
    DSW(DS_lzw_stack_n) = 0;
    DSW(DS_lzw_nbits) = 9;
    DSW(DS_lzw_maxcode) = 0x200;
    DSW(DS_lzw_bitpos) = 0;
    lzw_decode_body(src, dst);
}

/* 0c1c:0c45 rle_draw_page — platform.md §4.3.3 ((colour + DS:90F0, count) runs into page DS:009A,
 * bottom row first, wrapping lazily into the row above) */
void rle_draw_page(u16 pairs_ds, u16 npairs, u16 width, s16 x, s16 y_bottom)
{
    u16 di = (u16)(plat_row320((u16)y_bottom) + (u16)x);
    u16 es = DSW((u16)(DS_page_seg + (DSW(DS_page_cur) << 1)));
    u16 si = pairs_ds, bx = npairs, dx = 0, bp = width;
    /* PORT: npairs = 0 would run 65536 pairs in the original; no caller passes 0. */
    if (bx == 0) return;
    do {
        u8 al = (u8)(DSB(si) + DSB(DS_colour_offset));
        u16 cx = DSB((u16)(si + 1));
        si = (u16)(si + 2);
        dx = (u16)(dx + cx);
        while (dx > bp) {                       /* the run crosses the row end */
            dx = (u16)(dx - bp);
            cx = (u16)(cx - dx);
            for (u16 i = 0; i < cx; i++) wr8(es, di++, al);
            di = (u16)(di - 0x140 - bp);
            cx = dx;
        }
        for (u16 i = 0; i < cx; i++) wr8(es, di++, al);
    } while (--bx);
}

/* 0c1c:1759 rle_draw_viewbuf — platform.md §4.3.3 (320-wide runs into DS:90D0, colour 0Fh transparent) */
void rle_draw_viewbuf(u16 pairs_ds, u16 npairs, s16 y_bottom)
{
    u16 es = DSW(DS_viewbuf_seg);
    u16 di = plat_row320((u16)y_bottom);
    u16 si = pairs_ds, bx = npairs, bp = 0;
    if (bx == 0) return;                        /* PORT: as rle_draw_page */
    do {
        u8 al = DSB(si);
        u16 cx = DSB((u16)(si + 1));
        si = (u16)(si + 2);
        bp = (u16)(bp + cx);
        if (al == 0x0F) {                       /* transparent: skip */
            if (bp > 0x140) {
                bp = (u16)(bp - 0x140);
                cx = (u16)(cx - bp);
                di = (u16)(di + cx - 0x280);
                cx = bp;
            }
            di = (u16)(di + cx);
        } else {
            if (bp > 0x140) {                   /* one wrap per run */
                bp = (u16)(bp - 0x140);
                cx = (u16)(cx - bp);
                for (u16 i = 0; i < cx; i++) wr8(es, di++, al);
                di = (u16)(di - 0x280);
                cx = bp;
            }
            for (u16 i = 0; i < cx; i++) wr8(es, di++, al);
        }
    } while (--bx);
}
