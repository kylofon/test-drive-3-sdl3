/* render3d (render3d.md §4.7-4.10): sky and ground, the face drawer and the rasterizers of the 3D view V.
 * 0e12:373c sky_ground_draw, 3a7c draw_face_dispatch, 3a9e draw_face_polygon, 3ebf fill_around_camera,
 * 3f6c fill_below_edge, 3fe5 fill_above_edge, 41d4 fill_spans, 43db blob_spans, 44c2 line_thin_spans,
 * 66ad point_draw, 67af line_draw, 682e tri_fill (+ entries 685c, 68b0, 6909), 69da quad_fill.
 *
 * The mirror rasterizers (render_mirror.c) are instruction-for-instruction clones of these with other
 * buffers / constants; the shared bodies r3_* below take the differences as parameters, and each original
 * function is a C function of its own that calls them. Vertex arguments are byte offsets (2v). */
#define RENDER3D_INTERNAL
#include "game/game.h"

/* sar by CL: the count is masked to 5 bits (286+); counts 16..31 fill with the sign */
static s16 sar16(s16 v, u8 n) { n &= 31; return n >= 15 ? (s16)(v < 0 ? -1 : 0) : (s16)(v >> n); }

/* 16-bit `cwd; idiv` of a slope. PORT: the original traps (INT 0 -> R6003) on a zero divisor or the one
 * overflowing quotient; render3d.md §9.7 lists the slope divisions as overflow risks: clamp instead. */
s16 r3_idiv(s16 num, s16 den)
{
    if (den == 0) return 0;                               /* PORT: divide by zero -> 0 */
    if (num == -32768 && den == -1) return 32767;         /* PORT: quotient overflow -> clamp */
    return (s16)(num / den);
}

/* x in 1/32 px -> pixel (the shl/shl/rol/xchg sequence; exact for values with the low 5 bits clear) */
static u16 px9(u16 v) { return (u16)((v >> 5) & 0x1FF); }

/* ---------------------------------------------------------------------------------------------------
 * Span writers. c[0..4] are the five clip constants in instruction order: cmp c,c0 / cmp c,c1 /
 * cmp a,c2 / mov c,c3 / cmp a,c4 (V: the immediates 409c patches, read in place; M: B00h/8B00h/AE0h/B00h/AE0h).
 * Returns false when the row is skipped; else *pa = left x (1/32 px), *pn = width.
 * --------------------------------------------------------------------------------------------------- */
static bool span_clip(u16 xl, u16 xr, const u16 c[5], u16 *pa, u16 *pn)
{
    u16 a = (u16)(xl & 0xFFE0), cc = (u16)(xr & 0xFFE0);
    if (cc > c[0]) {
        if (cc > c[1]) return false;
        if (a > c[2] && a <= cc) return false;
        cc = c[3];
    }
    u16 n = (u16)(cc - a);
    if (r3_neg(n)) n = 0;
    if (a > c[4]) {
        if (a <= 0x8000) return false;
        n = (u16)(n + a);
        if (r3_neg(n)) return false;
        a = 0;
    }
    *pa = a; *pn = n;
    return true;
}

/* rep stosw / stosb tail: cnt>>1 == 0 -> one byte (even for cnt 0); else words, plus a byte when odd */
static void span_store(u16 seg, u16 di, u16 cnt, u16 ax, bool or_mode)
{
    u16 w = (u16)(cnt >> 1);
    if (w == 0) {
        if (or_mode) wr8(seg, di, (u8)(rd8(seg, di) | (u8)ax)); else wr8(seg, di, (u8)ax);
        return;
    }
    while (w--) {
        if (or_mode) { wr8(seg, di, (u8)(rd8(seg, di) | (u8)ax)); wr8(seg, (u16)(di + 1), (u8)(rd8(seg, (u16)(di + 1)) | (u8)(ax >> 8))); }
        else wr16(seg, di, ax);
        di = (u16)(di + 2);
    }
    if (cnt & 1) {
        if (or_mode) wr8(seg, di, (u8)(rd8(seg, di) | (u8)ax)); else wr8(seg, di, (u8)ax);
    }
}

/* ---------------------------------------------------------------------------------------------------
 * 41d4 / 7e11 body: the 3-segment edge walker with dithered spans (OR mode for face type 0).
 * --------------------------------------------------------------------------------------------------- */
void r3_fill_spans(u16 seg, u16 rowtab, u16 rows, const u16 cput[5], const u16 cor[5], u16 si, u16 bp)
{
    si = (u16)(si - 0x20);
    bp = (u16)(bp + 0x20);
    u16 bx = DSW(DS_span_top);
top:                                                      /* 41e6 */
    if (r3_neg(bx)) goto clip;
body:                                                     /* 41f2 */
    if (bx >= rows) return;
    {
        u16 ax = (u16)(bx + DSW(DS_rows1) - rows);
        if (!r3_neg(ax)) { DSW(DS_rows2) = 0; DSW(DS_rows1) = (u16)(DSW(DS_rows1) - ax); }
    }
    bx = (u16)(bx << 1);
    if (DSB(DS_face_type) == (u8)(bx >> 8)) {             /* cmp [BA9A], bh: face type 0 = OR mode */
        if (DSW(DS_colour) == 0x0707 && DSB(DS_colour_mode) != 0) return;
        do {
            u16 a, n;
            if (span_clip(si, bp, cor, &a, &n)) {
                u16 px = px9(a), cnt = px9(n);
                u16 di = (u16)(DSW(rowtab + bx) + px);
                u16 ax = DSW(DS_colour);
                if ((((u8)(px << 1) ^ (u8)bx) & 2) == 0) ax = (u16)(ax << 8 | ax >> 8);
                span_store(seg, di, cnt, ax, true);
            }
            bx = (u16)(bx + 2);
            si = (u16)(si - DSW(DS_dxl));
            bp = (u16)(bp - DSW(DS_dxr));
        } while (--DSW(DS_rows1));
    } else {
        do {
            u16 a, n;
            if (span_clip(si, bp, cput, &a, &n)) {
                u16 px = px9(a), cnt = px9(n);
                u16 di = (u16)(DSW(rowtab + bx) + px);
                u16 ax = DSW(DS_colour);
                if ((((u8)(px << 1) ^ (u8)bx) & 2) == 0) ax = (u16)(ax << 8 | ax >> 8);   /* checkerboard */
                span_store(seg, di, cnt, ax, false);
            }
            bx = (u16)(bx + 2);
            si = (u16)(si - DSW(DS_dxl));
            bp = (u16)(bp - DSW(DS_dxr));
        } while (--DSW(DS_rows1));
    }
    bx = (u16)(bx >> 1);
seg_end:                                                  /* 428b */
    if (DSW(DS_rows2) == 0) return;
    {
        u16 ax = DSW(DS_rows3);
        u16 t = DSW(DS_rows2); DSW(DS_rows2) = ax; ax = t;
        DSW(DS_rows1) = ax;
        DSW(DS_rows3) = 0;
        u8 al = DSB(DS_reload3);
        u8 f = DSB(DS_reload2); DSB(DS_reload2) = al; al = f;
        if (al & 1) {
            si = DSW(DS_xl2); DSW(DS_xl2) = DSW(DS_xl3);
            si = (u16)(si - 0x20);
            u16 cx = DSW(DS_dxl2); DSW(DS_dxl2) = DSW(DS_dxl3);
            DSW(DS_dxl) = cx;
            si = (u16)(si - cx);
        }
        if (al & 2) {
            bp = DSW(DS_xr2); DSW(DS_xr2) = DSW(DS_xr3);
            bp = (u16)(bp + 0x20);
            u16 cx = DSW(DS_dxr2); DSW(DS_dxr2) = DSW(DS_dxr3);
            DSW(DS_dxr) = cx;
            bp = (u16)(bp - cx);
        }
    }
    goto top;
clip: {                                                   /* 4294 */
        u16 cx = (u16)-bx;
        bx = 0;
        u16 dl = DSW(DS_dxl), dr = DSW(DS_dxr);
        DSW(DS_rows1) = (u16)(DSW(DS_rows1) - cx);
        u16 r1 = DSW(DS_rows1);
        if (r1 != 0 && !r3_neg(r1)) {                     /* 42cb: rows remain in this segment */
            u16 steps = (cx >> 1) == 0 ? 1 : cx;
            si = (u16)(si - steps * dl);
            bp = (u16)(bp - steps * dr);
            goto body;
        }
        bx = r1;                                          /* 42a9: <= 0, continue clipping in the next segment */
        cx = (u16)(cx + bx);
        u16 steps = (cx >> 1) == 0 ? 1 : cx;              /* QUIRK: a count of 0 still does one step */
        si = (u16)(si - steps * dl);
        bp = (u16)(bp - steps * dr);
        goto seg_end;
    }
}

/* 43db / 8016 body: disc-like blob, rows widen by `a` then shrink, no dither */
void r3_blob_spans(u16 seg, u16 rowtab, u16 rows, const u16 c[5], u16 si, u16 bp)
{
    si = (u16)(si - 0x20);
    bp = (u16)(bp + 0x20);
    u16 bx = DSW(DS_span_top);
    if (r3_neg(bx)) {                                     /* 447f */
        u16 cx = (u16)-bx;
        bx = 0;
        u16 ax = DSW(DS_dxl), di = DSW(DS_dxr);
        DSW(DS_rows1) = (u16)(DSW(DS_rows1) - cx);
        if (DSW(DS_rows1) == 0 || r3_neg(DSW(DS_rows1))) return;
        u16 steps = (cx >> 1) == 0 ? 1 : cx;
        while (steps--) { ax = (u16)(ax - di); si = (u16)(si - ax); bp = (u16)(bp + ax); }
        DSW(DS_dxl) = ax;
    }
    if (bx >= rows) return;                               /* 43f6 */
    {
        u16 ax = (u16)(bx + DSW(DS_rows1) - rows);
        if (!r3_neg(ax)) DSW(DS_rows1) = (u16)(DSW(DS_rows1) - ax);
    }
    bx = (u16)(bx << 1);
    do {
        u16 a, n;
        if (span_clip(si, bp, c, &a, &n)) {
            u16 di = (u16)(DSW(rowtab + bx) + px9(a));
            span_store(seg, di, px9(n), DSW(DS_colour), false);
        }
        bx = (u16)(bx + 2);
        u16 ax = (u16)(DSW(DS_dxl) - DSW(DS_dxr));
        DSW(DS_dxl) = ax;
        si = (u16)(si - ax);
        bp = (u16)(bp + ax);
    } while (--DSW(DS_rows1));
}

/* 44c2 / 80fb body: 1-px line as one span per row from x(y) to x(y+1), no dither */
void r3_thin_spans(u16 seg, u16 rowtab, u16 rows, const u16 c[5], u16 bp, u16 si)
{
    DSW(DS_rows2) = 0;                                    /* BAC3: step of the last row */
    u16 bx = DSW(DS_span_top);
    if ((s16)bx >= (s16)rows) return;
    if (bx >= rows) {                                     /* negative: clip the top */
        u16 cx = (u16)-bx;
        bx = 0;
        DSW(DS_rows1) = (u16)(DSW(DS_rows1) - cx);
        if ((s16)DSW(DS_rows1) < 1) return;
        if (DSW(DS_rows1) == 1) { si = DSW(DS_rows2); bx = (u16)(bx << 1); goto draw; }   /* QUIRK: x not advanced */
        u16 steps = (cx >> 1) == 0 ? 1 : cx;
        bp = (u16)(bp - steps * si);
    }
    {                                                     /* 4506 */
        u16 ax = bx;
        bx = (u16)(bx << 1);
        ax = (u16)(ax + DSW(DS_rows1) - rows);
        if (!r3_neg(ax) && ax != 0) { DSW(DS_rows2) = si; DSW(DS_rows1) = (u16)(DSW(DS_rows1) - ax); }
    }
draw:
    for (;;) {                                            /* 451e */
        u16 ax = bp, cx = (u16)(bp - si);
        if (!r3_neg((u16)(ax - cx))) { u16 t = ax; ax = cx; cx = t; }
        ax &= 0xFFE0; cx &= 0xFFE0;
        bool skip = false;
        if (cx > c[0]) {
            if (cx > c[1]) skip = true;
            else if (ax > c[2] && ax <= cx) skip = true;
            else cx = c[3];
        }
        if (!skip && ax > c[4]) {
            if (ax <= 0x8000) skip = true; else ax = 0;
        }
        if (!skip) {
            u16 cnt = (u16)(px9((u16)(cx - ax)) + 1);
            u16 di = (u16)(DSW(rowtab + bx) + px9(ax));
            span_store(seg, di, cnt, DSW(DS_colour), false);
        }
        bx = (u16)(bx + 2);                               /* 4576 */
        bp = (u16)(bp - si);
        u16 r = --DSW(DS_rows1);
        if (r > 1) continue;
        if (r < 1) return;
        si = DSW(DS_rows2);                               /* last row: 1-px dot (or the step if bottom-clipped) */
    }
}

/* clip constant sets of the view V: the immediates 0e12:409c patches (PORT: read in place from mem[]) */
static void v_consts(u16 out[5], u16 a0, u16 a1, u16 a2, u16 a3, u16 a4)
{
    out[0] = r3_cs16(a0); out[1] = r3_cs16(a1); out[2] = r3_cs16(a2); out[3] = r3_cs16(a3); out[4] = r3_cs16(a4);
}

/* 0e12:41d4 fill_spans — render3d.md §4.8 (V: seg DS:90D0, rows DS:B8D3, BA91 rows) */
void fill_spans(u16 xl_si, u16 xr_bp)
{
    u16 cput[5], cor[5];
    v_consts(cput, 0x4220, 0x4226, 0x422B, 0x4234, 0x423D);   /* PORT: patched immediates */
    v_consts(cor, 0x435D, 0x4363, 0x4368, 0x4371, 0x437A);
    r3_fill_spans(DSW(DS_viewbuf_seg), DS_row_ofs, DSW(DS_view_rows), cput, cor, xl_si, xr_bp);
}

/* 0e12:43db blob_spans — render3d.md §4.10 */
void blob_spans(u16 xl_si, u16 xr_bp)
{
    u16 c[5];
    v_consts(c, 0x4418, 0x441E, 0x4423, 0x442C, 0x4435);      /* PORT: patched immediates */
    r3_blob_spans(DSW(DS_viewbuf_seg), DS_row_ofs, DSW(DS_view_rows), c, xl_si, xr_bp);
}

/* 0e12:44c2 line_thin_spans — render3d.md §4.10 */
void line_thin_spans(u16 x_bp, u16 step_si)
{
    u16 c[5];
    v_consts(c, 0x452F, 0x4535, 0x453A, 0x4543, 0x4546);      /* PORT: patched immediates */
    r3_thin_spans(DSW(DS_viewbuf_seg), DS_row_ofs, DSW(DS_view_rows), c, x_bp, step_si);
}

/* ---------------------------------------------------------------------------------------------------
 * Triangle / quad setups, shared with the mirror clones (m = mirror: rows my[], x = (1600h-(sx-BD3D))>>1,
 * spans into M through 7e11).
 * --------------------------------------------------------------------------------------------------- */
static s16 ry(bool m, u16 v) { return m ? R3_MY(v) : R3_SY(v); }
static u16 rx(bool m, u16 v)
{
    u16 x = R3_SX(v);
    if (!m) return x;
    return (u16)((s16)(u16)(0x1600 - (u16)(x - DSW(DS_mirror_base))) >> 1);
}
static void rfill(bool m, u16 si, u16 bp) { if (m) fill_spans_mirror(si, bp); else fill_spans(si, bp); }
#define SWAPU(a, b) do { u16 t_ = (a); (a) = (b); (b) = t_; } while (0)

/* 685c / 839c: one row from min x to max x (compare tree with wrap compares) */
void r3_tri_row(bool m, u16 bx, u16 si, u16 di)
{
    u16 cx = rx(m, bx), dx = rx(m, si), ax = rx(m, di);
    if (!r3_neg((u16)(ax - dx))) {
        if (r3_neg((u16)(ax - cx))) { }
        else if (r3_neg((u16)(cx - dx))) { dx = ax; SWAPU(dx, cx); }
        else cx = ax;
    } else {
        if (!r3_neg((u16)(ax - cx))) SWAPU(dx, cx);
        else if (r3_neg((u16)(cx - dx))) { cx = ax; SWAPU(dx, cx); }
        else dx = ax;
    }
    DSW(DS_span_top) = (u16)ry(m, bx);
    DSW(DS_rows2) = 0; DSW(DS_rows3) = 0; DSW(DS_rows1) = 1;
    rfill(m, dx, cx);
}

/* 68b0 / 8413: apex bx on top, si/di on the bottom row */
void r3_tri_flat_bottom(bool m, u16 bx, u16 si, u16 di)
{
    u16 bp = rx(m, bx), cx = rx(m, si), dx = rx(m, di);
    u16 ax = (u16)ry(m, bx);
    u16 h = (u16)(ry(m, si) - ax);
    DSW(DS_span_top) = ax;
    DSW(DS_rows2) = 0; DSW(DS_rows3) = 0;
    DSW(DS_rows1) = (u16)(h + 1);
    if (!r3_neg((u16)(dx - cx))) SWAPU(dx, cx);
    DSS(DS_dxl) = r3_idiv((s16)-(s16)(u16)(dx - bp), (s16)h);
    DSS(DS_dxr) = r3_idiv((s16)-(s16)(u16)(cx - bp), (s16)h);
    rfill(m, bp, bp);
}

/* 6909 / 8490: bx/si on the top row, apex di at the bottom */
void r3_tri_flat_top(bool m, u16 bx, u16 si, u16 di)
{
    u16 bp = rx(m, di), cx = rx(m, bx), dx = rx(m, si);
    u16 ax = (u16)ry(m, bx);
    u16 h = (u16)(ry(m, di) - ax);
    DSW(DS_span_top) = ax;
    DSW(DS_rows2) = 0; DSW(DS_rows3) = 0;
    DSW(DS_rows1) = (u16)(h + 1);
    if (!r3_neg((u16)(dx - cx))) SWAPU(dx, cx);
    DSS(DS_dxl) = r3_idiv((s16)(u16)(dx - bp), (s16)h);
    DSS(DS_dxr) = r3_idiv((s16)(u16)(cx - bp), (s16)h);
    rfill(m, dx, cx);
}

/* 6956 / 8501: general triangle, bx top, si middle, di bottom */
static void r3_tri_general(bool m, u16 bx, u16 si, u16 di)
{
    u16 bp = rx(m, bx), cx = rx(m, si), dx = rx(m, di);
    u16 ya = (u16)ry(m, bx);
    u16 h1 = (u16)(ry(m, si) - ya);
    DSW(DS_xl2) = cx; DSW(DS_xr2) = cx;
    DSW(DS_span_top) = ya;
    DSW(DS_rows1) = (u16)(h1 + 1);
    u16 h = (u16)(ry(m, di) - ya);
    DSW(DS_rows3) = 0;
    u16 h2 = (u16)(h - h1);
    DSW(DS_rows2) = h2;
    DSS(DS_dxr2) = r3_idiv((s16)-(s16)(u16)(dx - cx), (s16)h2);
    DSB(DS_reload2) = 2;
    DSS(DS_dxl) = r3_idiv((s16)-(s16)(u16)(dx - bp), (s16)h);
    s16 s = r3_idiv((s16)-(s16)(u16)(cx - bp), (s16)h1);
    if (s > DSS(DS_dxl)) {                                /* the short edge is the left one */
        s16 t = DSS(DS_dxl); DSS(DS_dxl) = s; s = t;
        DSW(DS_dxl2) = DSW(DS_dxr2);
        DSB(DS_reload2) = 1;
    }
    DSS(DS_dxr) = s;
    rfill(m, bp, bp);
}

void r3_tri_fill(bool m, u16 bx, u16 si, u16 di)
{
    if (!(ry(m, bx) < ry(m, si))) SWAPU(si, bx);
    if (!(ry(m, si) < ry(m, di))) SWAPU(di, si);
    if (!(ry(m, bx) < ry(m, si))) SWAPU(si, bx);
    s16 yc = ry(m, di);
    if (yc == ry(m, bx)) { r3_tri_row(m, bx, si, di); return; }
    if (yc == ry(m, si)) { r3_tri_flat_bottom(m, bx, si, di); return; }
    if (ry(m, si) == ry(m, bx)) { r3_tri_flat_top(m, bx, si, di); return; }
    r3_tri_general(m, bx, si, di);
}

/* 0e12:682e tri_fill — render3d.md §4.10 */
void tri_fill(u16 a_bx, u16 b_si, u16 c_di) { r3_tri_fill(false, a_bx, b_si, c_di); }
/* 0e12:685c tri_fill_row — render3d.md §4.10 */
void tri_fill_row(u16 a_bx, u16 b_si, u16 c_di) { r3_tri_row(false, a_bx, b_si, c_di); }
/* 0e12:68b0 tri_fill_flat_bottom — render3d.md §4.10 */
void tri_fill_flat_bottom(u16 a_bx, u16 b_si, u16 c_di) { r3_tri_flat_bottom(false, a_bx, b_si, c_di); }
/* 0e12:6909 tri_fill_flat_top — render3d.md §4.10 */
void tri_fill_flat_top(u16 a_bx, u16 b_si, u16 c_di) { r3_tri_flat_top(false, a_bx, b_si, c_di); }

/* median-drop tree of 69da (6a3b etc.): x1 = x of r1, x2 = x of *si, x3 = x of *di. Leaves the median in
 * *di (by exchanging it with *si or *r1), as the xchg's do. */
static void drop_median(bool m, u16 *r1, u16 *si, u16 *di)
{
    u16 cx = rx(m, *r1), dx = rx(m, *si), ax = rx(m, *di);
    int x;                                                /* 0 = none, 1 = xchg di,si, 2 = xchg di,r1 */
    if (r3_neg((u16)(ax - dx))) {
        if (!r3_neg((u16)(ax - cx))) x = 0;
        else x = r3_neg((u16)(cx - dx)) ? 2 : 1;
    } else {
        if (r3_neg((u16)(ax - cx))) x = 0;
        else x = r3_neg((u16)(cx - dx)) ? 1 : 2;
    }
    if (x == 1) SWAPU(*di, *si);
    else if (x == 2) SWAPU(*di, *r1);
}

/* 1 when c is not an edge neighbour of the top vertex in the face record at rec_es:BAD0 (946A) */
static void quad_opposite(u16 es, u16 di)
{
    DSB(DS_place_rot) = 0;
    u16 cx = (u16)((DSW(DS_quad_top_word) + 2) & 7), dx = DSW(DS_face_ptr);
    if (di == (u16)((rd16(es, (u16)(cx + dx)) & 0x7FF) << 1)) return;
    cx = (u16)((cx - 4) & 7);
    if (di == (u16)((rd16(es, (u16)(cx + dx)) & 0x7FF) << 1)) return;
    DSB(DS_place_rot)++;
}

void r3_quad_fill(bool m, u16 bx, u16 si, u16 di, u16 bp, u16 es)
{
    u16 cx = 6;
    if (ry(m, bx) > ry(m, si)) { SWAPU(si, bx); cx = 0; }
    if (ry(m, si) > ry(m, di)) SWAPU(di, si);
    if (ry(m, di) > ry(m, bp)) SWAPU(bp, di);
    if (ry(m, bx) > ry(m, si)) { SWAPU(si, bx); cx = 2; }
    if (ry(m, si) > ry(m, di)) SWAPU(di, si);
    if (ry(m, bx) > ry(m, si)) { SWAPU(si, bx); cx = 4; }
    DSW(DS_quad_top_word) = cx;
    s16 ya = ry(m, bx);
    if (ya == ry(m, bp)) {                                /* 6a3b: one row */
        drop_median(m, &bx, &si, &di);
        di = bp;
        r3_tri_row(m, bx, si, di);
        return;
    }
    if (ya == ry(m, di)) {                                /* 6a6f: three on top */
        drop_median(m, &bx, &si, &di);
        di = bp;
        r3_tri_flat_top(m, bx, si, di);
        return;
    }
    if (ry(m, si) == ry(m, bp)) {                         /* 6aa7: three at the bottom */
        drop_median(m, &bp, &si, &di);
        di = bp;
        r3_tri_flat_bottom(m, bx, si, di);
        return;
    }
    if (ry(m, si) == ya) {                                /* 6ade: flat top edge a-b, c and d below */
        u16 xc = rx(m, di), xd = rx(m, bp);
        DSW(DS_xl2) = xc; DSW(DS_xr2) = xd;
        s16 e = (s16)(u16)(xc - xd);
        s16 hh = (s16)(ry(m, bp) - ry(m, di));
        if (hh) e = r3_idiv(e, hh);
        DSS(DS_dxl2) = e; DSS(DS_dxr2) = e;
        quad_opposite(es, di);
        u16 x1 = rx(m, bx), x2 = rx(m, si);               /* cx, dx */
        s16 y0 = ry(m, bx);
        s16 hc = (s16)(ry(m, di) - y0), hd = (s16)(ry(m, bp) - y0);
        DSW(DS_span_top) = (u16)y0;
        DSW(DS_rows3) = 0;
        DSB(DS_reload2) = 1;
        DSW(DS_rows1) = (u16)(hc + 1);
        DSW(DS_rows2) = (u16)(hd - hc);
        if (!r3_neg((u16)(x2 - x1))) { SWAPU(x2, x1); DSB(DS_place_rot) ^= 1; }
        u16 L = x2;
        if (DSB(DS_place_rot) == 0) {
            u16 t = DSW(DS_xl2); DSW(DS_xl2) = DSW(DS_xr2); DSW(DS_xr2) = t;
            s16 t2 = hc; hc = hd; hd = t2;
            DSB(DS_reload2) = 2;
        }
        DSS(DS_dxl) = r3_idiv((s16)(u16)(L - DSW(DS_xl2)), hc);
        DSS(DS_dxr) = r3_idiv((s16)(u16)(x1 - DSW(DS_xr2)), hd);
        rfill(m, L, x1);
        return;
    }
    /* 6bb9: general, a strictly above b, b strictly above d */
    s16 y0 = ry(m, bx);
    DSW(DS_span_top) = (u16)y0;
    s16 hb = (s16)(ry(m, si) - y0);
    DSS(DS_quad_temps + 2) = hb;                          /* 9466 */
    DSW(DS_rows1) = (u16)(hb + 1);
    s16 hc = (s16)(ry(m, di) - y0);
    DSS(DS_quad_temps + 4) = hc;                          /* 9468 */
    DSW(DS_rows2) = (u16)(hc - hb);
    s16 hd = (s16)(ry(m, bp) - y0);
    DSS(DS_vert_base) = hd;                               /* 945E */
    DSW(DS_rows3) = (u16)(hd - hb - DSW(DS_rows2));
    quad_opposite(es, di);
    u16 dxv = rx(m, si), cxv = rx(m, di);
    DSW(DS_quad_temps) = rx(m, bx);                       /* 9464 = xa */
    u16 xd = rx(m, bp);                                   /* BX */
    int path;                                             /* 0 = CHAINS_A, 1 = CHAIN_B_LEFT, 2 = CHAIN_B_RIGHT */
    if (ry(m, si) == ry(m, di)) {
        if (r3_neg((u16)(rx(m, di) - rx(m, si)))) { SWAPU(di, si); SWAPU(cxv, dxv); }
        path = 0;
    } else if (dxv == xd || r3_neg((u16)(dxv - xd))) path = DSB(DS_place_rot) ? 1 : 0;
    else path = DSB(DS_place_rot) ? 2 : 0;
    u16 xa = DSW(DS_quad_temps);
    if (path == 0) {                                      /* 6ca5 CHAINS_A */
        DSB(DS_reload2) = 1; DSB(DS_reload3) = 2;
        DSW(DS_xl2) = dxv; DSW(DS_xr2) = cxv;
        DSS(DS_dxl) = r3_idiv((s16)(u16)(xa - dxv), DSS(DS_quad_temps + 2));
        s16 s = r3_idiv((s16)(u16)(xa - cxv), DSS(DS_quad_temps + 4));
        if (!r3_neg((u16)(s - DSW(DS_dxl)))) {
            s16 t = DSS(DS_dxl); DSS(DS_dxl) = s; s = t;
            u16 t2 = DSW(DS_xl2); DSW(DS_xl2) = DSW(DS_xr2); DSW(DS_xr2) = t2;
            t2 = DSW(DS_quad_temps + 2); DSW(DS_quad_temps + 2) = DSW(DS_quad_temps + 4); DSW(DS_quad_temps + 4) = t2;
            DSB(DS_reload2) = 2; DSB(DS_reload3) = 1;
        }
        DSS(DS_dxr) = s;
        {
            s16 q = (s16)(u16)(DSW(DS_xl2) - xd);
            s16 d = (s16)(DSW(DS_vert_base) - DSW(DS_quad_temps + 2));
            if (d) q = r3_idiv(q, d);
            DSS(DS_dxl2) = q;
        }
        {
            s16 q = (s16)(u16)(DSW(DS_xr2) - xd);
            s16 d = (s16)(DSW(DS_vert_base) - DSW(DS_quad_temps + 4));
            if (d) q = r3_idiv(q, d);
            DSS(DS_dxr2) = q;
        }
        if (DSW(DS_rows2) == 0) {
            DSB(DS_reload2) = 3;
            DSW(DS_rows2) = DSW(DS_rows3); DSW(DS_rows3) = 0;
            if (r3_neg((u16)(DSW(DS_dxl) - DSW(DS_dxl2)))) {
                DSB(DS_reload2) = 2;
                DSS(DS_dxl) = r3_idiv((s16)(u16)(xa - rx(m, bp)), DSS(DS_vert_base));
            } else if (r3_neg((u16)(DSW(DS_dxr2) - DSW(DS_dxr)))) {
                DSB(DS_reload2) = 1;
                DSS(DS_dxr) = r3_idiv((s16)(u16)(xa - rx(m, bp)), DSS(DS_vert_base));
            }
        }
        rfill(m, xa, xa);
        return;
    }
    if (path == 1) {                                      /* 6d82 CHAIN_B_LEFT */
        if (DSW(DS_rows2) == 0) {                         /* unreachable (render3d.md) */
            DSW(DS_rows2) = DSW(DS_rows3); DSW(DS_rows3) = 0;
            if (!r3_neg((u16)(dxv - cxv))) SWAPU(cxv, dxv);
            cxv = xd;
        }
        DSW(DS_xl2) = dxv; DSW(DS_xl3) = cxv; DSW(DS_xr2) = dxv; DSW(DS_xr3) = cxv;
        DSS(DS_dxl) = r3_idiv((s16)(u16)(xa - dxv), DSS(DS_quad_temps + 2));
        s16 s = r3_idiv((s16)(u16)(xa - xd), DSS(DS_vert_base));
        u8 side = 1;
        if (!r3_neg((u16)(s - DSW(DS_dxl)))) { s16 t = DSS(DS_dxl); DSS(DS_dxl) = s; s = t; side++; }
        DSS(DS_dxr) = s;
        DSB(DS_reload2) = side; DSB(DS_reload3) = side;
        s16 q = r3_idiv((s16)(u16)(DSW(DS_xl2) - cxv), DSS(DS_rows2));
        DSS(DS_dxl2) = q; DSS(DS_dxr2) = q;
        q = (s16)(u16)(DSW(DS_xl3) - xd);
        if (DSW(DS_rows3)) q = r3_idiv(q, DSS(DS_rows3));
        DSS(DS_dxl3) = q; DSS(DS_dxr3) = q;
        rfill(m, xa, xa);
        return;
    }
    /* 6e0a CHAIN_B_RIGHT */
    if (DSW(DS_rows2) == 0) {                             /* unreachable (render3d.md) */
        DSW(DS_rows2) = DSW(DS_rows3); DSW(DS_rows3) = 0;
        if (r3_neg((u16)(dxv - cxv))) SWAPU(cxv, dxv);
        cxv = xd;
    }
    DSW(DS_xr2) = dxv; DSW(DS_xr3) = cxv; DSW(DS_xl2) = dxv; DSW(DS_xl3) = cxv;
    DSS(DS_dxr) = r3_idiv((s16)(u16)(xa - dxv), DSS(DS_quad_temps + 2));
    s16 s = r3_idiv((s16)(u16)(xa - xd), DSS(DS_vert_base));
    u8 side = 2;
    if (r3_neg((u16)(s - DSW(DS_dxr)))) { s16 t = DSS(DS_dxr); DSS(DS_dxr) = s; s = t; side--; }
    DSS(DS_dxl) = s;
    DSB(DS_reload2) = side; DSB(DS_reload3) = side;
    s16 q = r3_idiv((s16)(u16)(DSW(DS_xr2) - cxv), DSS(DS_rows2));
    DSS(DS_dxl2) = q; DSS(DS_dxr2) = q;
    q = (s16)(u16)(DSW(DS_xr3) - xd);
    if (DSW(DS_rows3)) q = r3_idiv(q, DSS(DS_rows3));
    DSS(DS_dxl3) = q; DSS(DS_dxr3) = q;
    rfill(m, xa, xa);
}

/* 0e12:69da quad_fill — render3d.md §4.10 (BX, SI, DI, BP; face record at rec_es:BAD0) */
void quad_fill(u16 a_bx, u16 b_si, u16 c_di, u16 d_bp, u16 rec_es) { r3_quad_fill(false, a_bx, b_si, c_di, d_bp, rec_es); }

/* ---------------------------------------------------------------------------------------------------
 * Points and lines
 * --------------------------------------------------------------------------------------------------- */
/* ratio index of the atan tables: size >= dist -> 1FFh, else ((size << 16) / dist) >> 7 & 1FFh */
static u16 ratio_idx(u16 size, u16 dist)
{
    if (size >= dist) return 0x1FF;
    u16 q = (u16)(((u32)size << 16) / dist);
    return (u16)((q >> 7) & 0x1FF);
}

/* 66ad / 81c7 body: lamp blob (m: mirror clone � distance x2, rows my[], 14h instead of BA99, mirror x) */
void r3_point_draw(bool m, u16 v_bx, u16 w0_si)
{
    u16 bp = w0_si;
    u16 size = DSB(DS_point_sizes + ((w0_si >> 11) & 3));
    u16 cx = R3_DIST(v_bx);
    if (m) cx = (u16)(cx << 1);
    u16 ax = (u16)(r3_cs16((u16)(0x12F6 + 2 * ratio_idx(size, cx))) >> 5);
    u16 r = ax;
    DSW(DS_span_top) = (u16)(ry(m, v_bx) - ax);
    ax = (u16)(ax << 1);
    if (ax == 0) return;
    DSW(DS_rows1) = ax;
    u16 v = (u16)(v_bx >> 1);
    s16 o = (s16)(DSW(DS_obj_count) << 1);
    for (;;) {                                            /* owner object: AFB9 <= v <= AFB9 + B0F9 */
        o = (s16)(o - 2);
        if (o < 0) return;
        u16 b = DSW(DS_obj_vert_base + (u16)o);
        if (b > v) continue;
        if ((u16)(b + DSW(DS_obj_vert_count + (u16)o)) >= v) break;
    }
    u16 a = (u16)(DSW(DS_obj_heading + (u16)o) & 0xFF00);
    if (!(bp & 0x2000)) a = (u16)(a + 0x4000);            /* bit 13 clear: lamp faces sideways */
    a = (u16)(a - DSW(DS_cam_heading));
    a = (u16)(a - R3_SX(v_bx));
    a = (u16)(a + ((u16)(m ? 0x14 : DSB(DS_view_cx_hi)) << 8));
    a &= 0x7FFF;
    if (a == 0x4000) a--;
    if (a & 0x4000) a = (u16)(0x8000 - a);
    u16 s = r3_cs16((u16)(0x0FF2 + ((a >> 6) & 0xFE)));
    u16 q = (r < 0x0C) ? 0x30 : 0x38;
    u8 al = (u8)q, ah = 0;
    if ((s16)(r - 1) > 0) { u16 d = div16_8(q, (u8)(r - 1)); al = (u8)d; ah = (u8)(d >> 8); }
    ah = (u8)(ah + 0x10);
    if (ah >= 0x20) al++;
    u16 inc = (u16)(((u32)al * s) >> 16);
    DSW(DS_dxr) = inc;
    DSW(DS_dxl) = (u16)((u8)inc * (u8)r);
    u16 w = (r < 0x0C) ? (u16)(r << 3) : (u16)(r << 2);
    u16 half = (u16)(((u32)w * s) >> 16);
    u16 x = rx(m, v_bx);
    if (m) blob_spans_mirror((u16)(x - half), (u16)(x + half));
    else blob_spans((u16)(x - half), (u16)(x + half));
}

/* 0e12:66ad point_draw � render3d.md �4.10 (lamp blob; BX = 2v, SI = w0) */
void point_draw(u16 v_bx, u16 w0_si) { r3_point_draw(false, v_bx, w0_si); }

/* 67af / 82d6 body (m: the x arguments are converted to mirror x first, rows my[], distance x2) */
void r3_line_draw(bool m, u16 p_bx, u16 q_si, u16 xp_ax, u16 xq_dx, FarPtr w0_es_bp)
{
    u16 ax = xp_ax, dx = xq_dx;
    if (m) {
        u16 b = DSW(DS_mirror_base);
        ax = (u16)((s16)(u16)(0x1600 - (u16)(ax - b)) >> 1);
        dx = (u16)((s16)(u16)(0x1600 - (u16)(dx - b)) >> 1);
    }
    s16 di = ry(m, p_bx), cx = ry(m, q_si);
    u16 w0 = rd16(w0_es_bp.seg, w0_es_bp.off);
    if (!(di < cx)) { s16 t = di; di = cx; cx = t; SWAPU(dx, ax); }
    u16 bp = ax;
    ax = (u16)(ax - dx);
    DSW(DS_span_top) = (u16)di;
    u16 h = (u16)(cx - di);
    DSW(DS_rows1) = (u16)(h + 1);
    if (h) ax = (u16)r3_idiv((s16)ax, (s16)h);
    if ((w0 & 0x1800) == 0) {
        if (m) line_thin_spans_mirror(bp, ax); else line_thin_spans(bp, ax);
        return;
    }
    DSW(DS_dxl) = ax; DSW(DS_dxr) = ax;
    DSW(DS_rows2) = 0;
    u16 size = DSB(DS_line_widths + ((w0 >> 11) & 3));
    u16 d = R3_DIST(p_bx);
    if (m) d = (u16)(d << 1);
    u16 half = r3_cs16((u16)(0x12F6 + 2 * ratio_idx(size, d)));
    rfill(m, (u16)(bp - half), (u16)(bp + half));
}

/* 0e12:67af line_draw � render3d.md �4.10 (BX = p, SI = q, AX = x_p, DX = x_q, ES:BP = &w0) */
void line_draw(u16 p_bx, u16 q_si, u16 xp_ax, u16 xq_dx, FarPtr w0_es_bp) { r3_line_draw(false, p_bx, q_si, xp_ax, xq_dx, w0_es_bp); }

/* ---------------------------------------------------------------------------------------------------
 * Edge fills (sky / ground and the face around the camera)
 * --------------------------------------------------------------------------------------------------- */
/* rows of the edge fills: the mirror clones (7cd9 / 7d94) halve my[] (sar 1) */
static s16 ey(bool m, u16 v) { return m ? (s16)(R3_MY(v) >> 1) : R3_SY(v); }

/* 3f6c / 7d94 body: the region below the edge a-b (rows from the view), then full columns to the bottom */
void r3_fill_below_edge(bool m, u16 rows, u16 ax, u16 cx, u16 bx, u16 si)
{
    if ((u16)(ax - cx) == 0x8000) return;
    DSW(DS_rows2) = 0; DSW(DS_rows3) = 0;
    DSB(DS_reload2) = 3;
    DSW(DS_dxl2) = 0; DSW(DS_dxr2) = 0;
    DSW(DS_xl2) = ax; DSW(DS_xr2) = cx;
    DSW(DS_dxl) = 0; DSW(DS_dxr) = 0;
    s16 dy = ey(m, bx), by = ey(m, si);
    u16 xs = ax, bp = cx;
    if (dy > by) { SWAPU(xs, bp); s16 t = dy; dy = by; by = t; }
    DSW(DS_span_top) = (u16)dy;
    u16 below = (u16)(rows - (u16)by);
    if (!r3_neg(below)) DSW(DS_rows2) = below;
    u16 h = (u16)((u16)by - (u16)dy);
    DSW(DS_rows1) = (u16)(h + 1);
    if (h) {
        s16 s = r3_idiv((s16)-(s16)(u16)(bp - xs), (s16)h);
        bp = xs;
        if (s > DSS(DS_dxl)) { s16 t = DSS(DS_dxl); DSS(DS_dxl) = s; s = t; }
        DSS(DS_dxr) = s;
    }
    rfill(m, xs, bp);
}

/* 3fe5 / 7cd9 body: the region above the edge a-b */
void r3_fill_above_edge(bool m, u16 ax, u16 cx, u16 bx, u16 si)
{
    if ((u16)(ax - cx) == 0x8000) return;
    DSW(DS_rows2) = 0; DSW(DS_rows3) = 0;
    DSB(DS_reload2) = 0;
    DSW(DS_dxl2) = 0; DSW(DS_dxr2) = 0;
    DSW(DS_xl2) = ax; DSW(DS_xr2) = cx;
    DSW(DS_dxl) = 0; DSW(DS_dxr) = 0;
    DSB(DS_place_rot) = 0;                                /* 946A: flip */
    s16 dy = ey(m, bx), by = ey(m, si);
    u16 xs = ax, bp = cx;
    if (dy > by) { s16 t = dy; dy = by; by = t; DSB(DS_place_rot)++; }
    if (dy > 0) {                                         /* full-width rows from span_top down to y0 */
        DSW(DS_rows1) = (u16)(dy + 1);
        DSB(DS_reload2) = 3;
        u16 r2 = (u16)(by - dy);
        DSW(DS_rows2) = r2;
        if (r2 == 0) { rfill(m, xs, bp); return; }
        s16 d = (s16)(u16)(bp - xs);
        if (DSB(DS_place_rot) == 0) d = (s16)-d;
        s16 s = r3_idiv(d, (s16)r2);
        if (s > DSS(DS_dxr2)) { s16 t = DSS(DS_dxr2); DSS(DS_dxr2) = s; s = t; }
        DSS(DS_dxl2) = s;
        rfill(m, xs, bp);
        return;
    }
    DSW(DS_span_top) = (u16)dy;                           /* 406d (may be <= 0) */
    u16 h = (u16)(by - dy);
    DSW(DS_rows1) = (u16)(h + 1);
    if (h) {
        s16 d = (s16)(u16)(bp - xs);
        if (DSB(DS_place_rot) == 0) d = (s16)-d;
        s16 s = r3_idiv(d, (s16)h);
        if (s > DSS(DS_dxr)) { s16 t = DSS(DS_dxr); DSS(DS_dxr) = s; s = t; }
        DSS(DS_dxl) = s;
    }
    rfill(m, xs, bp);
}

/* 0e12:3f6c fill_below_edge — render3d.md §4.7 */
void fill_below_edge(u16 x0_ax, u16 x1_cx, u16 a_bx, u16 b_si) { r3_fill_below_edge(false, DSW(DS_view_rows), x0_ax, x1_cx, a_bx, b_si); }
/* 0e12:3fe5 fill_above_edge — render3d.md §4.7 */
void fill_above_edge(u16 x0_ax, u16 x1_cx, u16 a_bx, u16 b_si) { r3_fill_above_edge(false, x0_ax, x1_cx, a_bx, b_si); }

/* 0e12:3ebf fill_around_camera — render3d.md §4.9 (sort by sx unsigned, fill below / above each edge) */
void fill_around_camera(u16 b_bx, u16 s_si, u16 d_di)
{
    u16 bx = b_bx, si = s_si, di = d_di;
    u16 ax = R3_SX(bx), cx = R3_SX(si), dx = R3_SX(di);
    if (ax > cx) { SWAPU(cx, ax); SWAPU(si, bx); }
    if (cx > dx) { SWAPU(dx, cx); SWAPU(di, si); }
    if (ax > cx) { SWAPU(cx, ax); SWAPU(si, bx); }
    DSW(DS_around_sorted) = bx; DSW(DS_around_sorted + 2) = si; DSW(DS_around_sorted + 4) = di;
    if (ax < r3_cs16(0x3EEE)) {                           /* PORT: patched immediate (W32) */
        if (DSB(DS_plane_above)) { DSW(DS_top_row) = 0; DSW(DS_span_top) = 0; fill_above_edge(ax, cx, bx, si); }
        else fill_below_edge(ax, cx, bx, si);
    }
    bx = DSW(DS_around_sorted + 2); si = DSW(DS_around_sorted + 4);
    ax = R3_SX(bx);
    if (ax < r3_cs16(0x3F1B)) {                           /* PORT: patched immediate (W32) */
        cx = R3_SX(si);
        if (DSB(DS_plane_above)) { DSW(DS_top_row) = 0; DSW(DS_span_top) = 0; fill_above_edge(ax, cx, bx, si); }
        else fill_below_edge(ax, cx, bx, si);
    }
    bx = DSW(DS_around_sorted + 4); si = DSW(DS_around_sorted);
    ax = R3_SX(bx); cx = R3_SX(si);
    if (DSB(DS_plane_above)) { DSW(DS_top_row) = 0; DSW(DS_span_top) = 0; fill_above_edge(ax, cx, bx, si); }
    else fill_below_edge(ax, cx, bx, si);
}

/* ---------------------------------------------------------------------------------------------------
 * Face drawing
 * --------------------------------------------------------------------------------------------------- */
static void proj(u16 v) { if ((u16)R3_SY(v) == 0x8000) vertex_project_y(v); }
static void projm(u16 v) { if ((u16)R3_MY(v) == 0x8000) vertex_project_y_mirror(v); }

/* front test of n x values: all negative / all >= 2800h / all >= 5400h (sign of differences) -> not front */
static bool front_visible(int n, const u16 *x)
{
    u16 all = 0xFFFF;
    for (int k = 0; k < n; k++) all &= x[k];
    if (r3_neg(all)) return false;
    bool any = false;
    for (int k = 0; k < n; k++) if (r3_neg((u16)(x[k] - 0x2800))) any = true;
    if (!any) return false;
    for (int k = 0; k < n; k++) if (r3_neg((u16)(x[k] - 0x5400))) return true;
    return false;
}
static bool mirror_visible(int n, const u16 *x0)
{
    u16 x[4], all = 0xFFFF, b = DSW(DS_mirror_base);
    for (int k = 0; k < n; k++) { x[k] = (u16)(x0[k] - b); all &= x[k]; }
    if (r3_neg(all)) return false;
    bool any = false;
    for (int k = 0; k < n; k++) if (r3_neg((u16)(x[k] - 0x1600))) any = true;
    if (!any) return false;
    for (int k = 0; k < n; k++) if (r3_neg((u16)(x[k] - 0x4B00))) return true;
    return false;
}

/* 3d25 face under / over the camera (the ground the car stands on, a roof, a wall) */
static void face_around_camera(u16 bx, u16 si, u16 di)
{
    u16 ax = plane_height_at_camera(bx, si, di);
    u8 dl = (u8)(DSB(DS_face_type) >> 3);
    if (dl == 0) goto fill;
    if (dl < 0x0E) {
        if (dl == 1) {                                    /* wall / rail */
            u16 v = DSW(DS_car_speed);
            if (r3_neg(v)) v = (u16)-v;
            if (v < 0x14) bounce_back();
            else { DSB(DS_crash_flag) = dl; DSB(DS_crash_type) = dl; }
        }
        goto fill;
    }
    if (dl >= 0x14 && dl < 0x1C) {
        if (dl >= 0x18) goto fill;
        u16 c = (u16)(DSW(DS_cam_y_949E) - DSW(DS_car_eye_height) - ax + DSW(DS_bridge_clearance));
        if (r3_neg(c)) { DSW(DS_roof_height) = ax; goto fill; }      /* roof over us */
    }
    DSB(DS_plane_above) = 0;                              /* ground types */
    if (ax > DSW(DS_ground_height)) {
        DSW(DS_ground_height) = ax;
        DSW(DS_ground_tri) = bx; DSW(DS_ground_tri + 2) = si; DSW(DS_ground_tri + 4) = di;
        if ((s8)DSB(DS_surface_under_car) < 0x1C) DSB(DS_surface_under_car) = dl;
    }
fill:
    fill_around_camera(bx, si, di);
}

/* 0e12:3a9e draw_face_polygon — render3d.md §4.9 (triangle BX, SI, DI) */
void draw_face_polygon(u16 a_bx, u16 b_si, u16 c_di)
{
    u16 bx = a_bx, si = b_si, di = c_di;
    u16 x[3] = { R3_SX(bx), R3_SX(si), R3_SX(di) };
    if (front_visible(3, x)) {
        proj(bx); SWAPU(si, bx); proj(bx); SWAPU(di, bx); proj(bx);   /* bx = v2, si = v0, di = v1 */
        u16 ax = R3_SX(bx), cx = R3_SX(si), dx = R3_SX(di);
        if (ax > cx) SWAPU(cx, ax);
        if (cx > dx) SWAPU(dx, cx);
        if (ax > cx) SWAPU(cx, ax);
        if (r3_neg((u16)(dx - cx - 1)) || r3_neg((u16)(cx - ax - 1)) || r3_neg((u16)(ax - dx - 1))) tri_fill(bx, si, di);
        else face_around_camera(bx, si, di);             /* all three gaps in 1..8000h: the camera is inside */
        return;
    }
    if (!DSB(DS_mirror_on)) return;
    if (!mirror_visible(3, x)) return;
    projm(bx); SWAPU(si, bx); projm(bx); SWAPU(di, bx); projm(bx);
    tri_fill_mirror(bx, si, di);
}

/* 0e12:3a7c draw_face_dispatch — render3d.md §4.9 (BX = 2*v0, SI = 2*v1, DH = w0_hi & C0h, ES:BP = &face.w1) */
void draw_face_dispatch(u16 v0_bx, u16 v1_si, u16 w0_dx, FarPtr w1_es_bp)
{
    u8 dh = (u8)(w0_dx >> 8);
    u16 es = w1_es_bp.seg, bp = w1_es_bp.off;
    u16 bx = v0_bx, si = v1_si;
    if (dh == 0x80) {                                     /* 3a91: triangle */
        bp = (u16)(bp + 2);
        u16 di = (u16)((rd16(es, bp) & 0x7FF) << 1);
        draw_face_polygon(bx, si, di);
        return;
    }
    if (dh > 0x80) {                                      /* 3b0e: quad */
        bp = (u16)(bp + 2);
        u16 di = (u16)((rd16(es, bp) & 0x7FF) << 1);
        bp = (u16)(bp + 2);
        bp = (u16)((rd16(es, bp) & 0x7FF) << 1);
        u16 x[4] = { R3_SX(bx), R3_SX(si), R3_SX(di), R3_SX(bp) };
        if (front_visible(4, x)) {                        /* 3dde */
            proj(bx); SWAPU(si, bx); proj(bx); SWAPU(di, bx); proj(bx); SWAPU(bp, bx); proj(bx);
            /* bx = v3, si = v0, di = v1, bp = v2 */
            u16 t = R3_SX(bp);                            /* 945E */
            u16 ax = R3_SX(bx), cx = R3_SX(si), dx = R3_SX(di);
            if (ax > cx) SWAPU(cx, ax);
            if (cx > dx) SWAPU(dx, cx);
            if (dx > t) SWAPU(t, dx);
            if (ax > cx) SWAPU(cx, ax);
            if (cx > dx) SWAPU(dx, cx);
            if (ax > cx) SWAPU(cx, ax);
            DSW(DS_vert_base) = t;
            if (r3_neg((u16)(t - dx - 1)) || r3_neg((u16)(dx - cx - 1)) || r3_neg((u16)(cx - ax - 1)) ||
                r3_neg((u16)(ax - t - 1))) {
                quad_fill(bx, si, di, bp, es);
                return;
            }
            /* split: (w0, w1, w2) and (w0, w2, w3) of the record at ES:BAD0 */
            u16 f = DSW(DS_face_ptr);
            draw_face_polygon((u16)((rd16(es, f) & 0x7FF) << 1), (u16)((rd16(es, (u16)(f + 2)) & 0x7FF) << 1),
                              (u16)((rd16(es, (u16)(f + 4)) & 0x7FF) << 1));
            f = DSW(DS_face_ptr);
            draw_face_polygon((u16)((rd16(es, f) & 0x7FF) << 1), (u16)((rd16(es, (u16)(f + 4)) & 0x7FF) << 1),
                              (u16)((rd16(es, (u16)(f + 6)) & 0x7FF) << 1));
            return;
        }
        if (!DSB(DS_mirror_on)) return;
        if (!mirror_visible(4, x)) return;
        projm(bx); SWAPU(si, bx); projm(bx); SWAPU(di, bx); projm(bx); SWAPU(bp, bx); projm(bx);
        quad_fill_mirror(bx, si, di, bp, es);
        return;
    }
    if (dh != 0x40) {                                     /* 3bba: point */
        bp = (u16)(bp - 2);
        u16 w0 = rd16(es, bp);
        u16 x = R3_SX(bx);
        if (!r3_neg(x) && !r3_neg((u16)(x + 0x5800)) && !r3_neg((u16)(x + 0x2C00))) {
            proj(bx);
            point_draw(bx, w0);
            return;
        }
        if (!DSB(DS_mirror_on)) return;
        u16 mm = (u16)(x - DSW(DS_mirror_base));
        if (r3_neg(mm)) return;
        mm = (u16)(mm + 0x6A00); if (r3_neg(mm)) return;
        mm = (u16)(mm - 0x3500); if (r3_neg(mm)) return;
        projm(bx);
        point_draw_mirror(bx, w0);
        return;
    }
    /* 3c0c: line */
    bp = (u16)(bp - 2);
    u16 x[2] = { R3_SX(bx), R3_SX(si) };
    if (front_visible(2, x)) {
        proj(bx); SWAPU(si, bx); proj(bx);                /* bx = v1, si = v0 */
        line_draw(bx, si, R3_SX(bx), R3_SX(si), far_make(es, bp));
        return;
    }
    if (!DSB(DS_mirror_on)) return;
    if (!mirror_visible(2, x)) return;
    projm(bx); SWAPU(si, bx); projm(bx);
    line_draw_mirror(bx, si, R3_SX(bx), R3_SX(si), far_make(es, bp));
}

/* ---------------------------------------------------------------------------------------------------
 * 0e12:373c sky_ground_draw — render3d.md §4.7
 * --------------------------------------------------------------------------------------------------- */
void sky_ground_draw(void)
{
    DSB(DS_msg_protect) = 0;
    u8 al = DSB(DS_sky_flash);
    if (al) {
        al--;
        if (al == 0) {
            u8 bl = DSB(DS_flash_msg);
            if (bl) { DSB(DS_flash_msg) = al; DSW(DS_pending_msg) = bl; }
        }
        DSB(DS_sky_flash) = al;
        DSW(DS_top_row) = 0;
        DSW(DS_top_row_prev) = 0;
    }
    DSB(DS_face_type) = 0x70;
    u16 es = DSW(DS_viewbuf_seg);
    DSW(DS_roof_height) = 0;
    DSW(DS_ground_height) = 0;
    DSB(DS_surface_under_car) = 0xFF;
    s16 save0 = R3_SY(0), save1 = R3_SY(2);
    s16 ax = DSS(DS_cam_row), dx = 0;
    u8 cl = DSB(DS_cam_roll_94A0);
    if (cl) {
        s8 ch = (s8)cl;
        if (ch < 0) cl = (u8)-ch;
        dx = sar16(DSS(DS_view_cx32), cl);
        if (ch >= 0) dx = (s16)-dx;
    }
    ax = (s16)(ax + dx);                                  /* row at the right edge */
    s16 di = DSS(DS_top_row_prev);
    if (di > ax) di = ax;
    if (ax < DSS(DS_top_row)) DSS(DS_top_row) = ax;
    dx = (s16)(ax - 2 * dx);                              /* row at the left edge */
    if (di > dx) di = dx;
    if (dx < DSS(DS_top_row)) DSS(DS_top_row) = dx;
    DSS(DS_span_top) = di;
    DSS(DS_sky_top) = di;
    ax = (s16)(ax - di);
    dx = (s16)(dx - di);
    R3_SY(2) = ax;                                        /* RELATIVE to the top for the sky pass */
    R3_SY(0) = dx;
    u8 sal = DSB(DS_sky_colours), sbl = DSB(DS_sky_colours + 2);
    if (DSB(DS_sky_alt)) { sal = DSB(DS_sky_colours + 1); sbl = DSB(DS_sky_colours + 3); }
    u8 ah = DSB(DS_sky_flash);
    if (ah) {
        DSW(DS_top_row) = 0;
        cl = 1;
        sal = (u8)(ah * 8 - 1);
        sbl = sal;
    }
    u16 pair;
    if (DSW(DS_video_mode) == 0x13) {
        pair = (u16)(sbl * 0x0101);
        DSW(DS_sky_pair_ega) = pair;
    } else {
        cl = 1;
        DSW(DS_sky_pair_ega) = (u16)(sal * 0x0101);
        pair = DSW(DS_colour_pairs + 2 * (u8)(sal | (u8)(sal << 4)));
    }
    DSW(DS_colour) = pair;
    DSW(DS_sky_pair) = pair;
    if (DSB(DS_detail) == 0 || cl != 0) {
        fill_above_edge(0, (u16)(DSW(DS_view_w32m) + 0x20), 0, 2);
    } else {                                              /* 38c7: gradient sky */
        s16 cxr = (s16)(dx - 0x14);
        if (cxr < 0) { dx = (s16)(dx - cxr); di = (s16)(di + cxr); }
        dx = (s16)(dx + di);
        if (dx >= 0) {
            if (di < 0) di = 0;
            dx = (s16)(dx - 0x14);
            u16 axw = DSW(DS_colour);
            u16 off;
            u8 bh, bl;
            if (dx < 0) {                                 /* part of the gradient is above row 0 */
                DSW(DS_top_row) = 0;
                off = 0;
                bh = 5; bl = 4;
                do {
                    if (--bl == 0) { axw = (u16)(axw + 0x0101); bh--; bl = 4; }
                } while (++dx < 0);
                goto band_rows;
            }
            if ((u16)dx < DSW(DS_top_row)) DSW(DS_top_row) = (u16)dx;
            {
                u16 nflat = (u16)(dx - di);
                off = (u16)((u16)di * 0x140);
                while (nflat) {
                    if (off >= 0x7800) goto ground;
                    for (u16 k = 0; k < 0xA0; k++) wr16(es, (u16)(off + 2 * k), axw);
                    off = (u16)(off + 0x140);
                    nflat--;
                }
            }
            bh = 5;
        next_band:
            bl = 4;
        band_rows:
            axw = (u16)(axw + 0x0101);
            do {
                if (off >= 0x7800) goto ground;
                for (u16 k = 0; k < 0xA0; k++) wr16(es, (u16)(off + 2 * k), axw);
                off = (u16)(off + 0x140);
            } while (--bl);
            if (--bh) goto next_band;
        }
    }
ground:
    di = DSS(DS_sky_top);
    R3_SY(2) = (s16)(R3_SY(2) + di);
    R3_SY(0) = (s16)(R3_SY(0) + di);
    u16 g = DSW(DS_ground_colour);
    if (DSB(DS_snow_level)) g = 0x0F0F;
    pair = DSW(DS_colour_pairs + 2 * (u8)((u8)((g >> 8) << 4) | (u8)g));
    DSW(DS_colour) = pair;
    DSW(DS_ground_pair) = pair;
    fill_below_edge(0, (u16)(DSW(DS_view_w32m) + 0x20), 0, 2);
    mirror_sky_ground();
    R3_SY(2) = save1;
    R3_SY(0) = save0;
}
