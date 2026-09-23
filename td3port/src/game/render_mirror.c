/* render3d (render3d.md §4.5, §4.10, §4.12): the rear-view mirror pass into buffer M (seg DS:90D2, 88 x 19,
 * row table DS:B993). The rasterizers are clones of the view ones (render_raster.c) with the mirror x
 * transform (1600h - (sx - BD3D)) >> 1, rows my[] (DS:7C82), 13h rows and the fixed clip constants
 * B00h / 8B00h / AE0h (not patched). Each original function calls the shared body.
 * 0e12:7b9b mirror_sky_ground, 7c21 mirror_frame_draw, 7cd9 mirror_fill_above_edge, 7d94 mirror_fill_below_edge,
 * 7e11 fill_spans_mirror, 8016 blob_spans_mirror, 80fb line_thin_spans_mirror, 81c7 point_draw_mirror,
 * 82d6 line_draw_mirror, 836e tri_fill_mirror (+ 839c / 8413 / 8490), 85a9 quad_fill_mirror,
 * 8b5f vertex_project_y_mirror. */
#define RENDER3D_INTERNAL
#include "game/game.h"

static const u16 M_CLIP[5]  = { 0x0B00, 0x8B00, 0x0AE0, 0x0B00, 0x0AE0 };   /* 7e59.., 7f96.., 804f.. */
static const u16 M_THIN[5]  = { 0x0AE0, 0x8AE0, 0x0AE0, 0x0AE0, 0x0AE0 };   /* 8164.. */

/* sar by CL: the count is masked to 5 bits (286+); counts 16..31 fill with the sign */
static s16 sar16(s16 v, u8 n) { n &= 31; return n >= 15 ? (s16)(v < 0 ? -1 : 0) : (s16)(v >> n); }

/* 0e12:7e11 fill_spans_mirror — render3d.md §4.10 */
void fill_spans_mirror(u16 xl_si, u16 xr_bp)
{
    r3_fill_spans(DSW(DS_mirrorbuf_seg), DS_mrow_ofs, 0x13, M_CLIP, M_CLIP, xl_si, xr_bp);
}

/* 0e12:8016 blob_spans_mirror — render3d.md §4.10 */
void blob_spans_mirror(u16 xl_si, u16 xr_bp)
{
    r3_blob_spans(DSW(DS_mirrorbuf_seg), DS_mrow_ofs, 0x13, M_CLIP, xl_si, xr_bp);
}

/* 0e12:80fb line_thin_spans_mirror — render3d.md §4.10 */
void line_thin_spans_mirror(u16 x_bp, u16 step_si)
{
    r3_thin_spans(DSW(DS_mirrorbuf_seg), DS_mrow_ofs, 0x13, M_THIN, x_bp, step_si);
}

/* 0e12:81c7 point_draw_mirror — render3d.md §4.10 */
void point_draw_mirror(u16 v_bx, u16 w0_si) { r3_point_draw(true, v_bx, w0_si); }

/* 0e12:82d6 line_draw_mirror — render3d.md §4.10 */
void line_draw_mirror(u16 p_bx, u16 q_si, u16 xp_ax, u16 xq_dx, FarPtr w0_es_bp)
{
    r3_line_draw(true, p_bx, q_si, xp_ax, xq_dx, w0_es_bp);
}

/* 0e12:836e tri_fill_mirror — render3d.md §4.10 */
void tri_fill_mirror(u16 a_bx, u16 b_si, u16 c_di) { r3_tri_fill(true, a_bx, b_si, c_di); }
/* 0e12:839c tri_fill_mirror_839c (one row) — render3d.md §4.10 */
void tri_fill_mirror_839c(u16 a_bx, u16 b_si, u16 c_di) { r3_tri_row(true, a_bx, b_si, c_di); }
/* 0e12:8413 tri_fill_mirror_8413 (flat bottom) — render3d.md §4.10 */
void tri_fill_mirror_8413(u16 a_bx, u16 b_si, u16 c_di) { r3_tri_flat_bottom(true, a_bx, b_si, c_di); }
/* 0e12:8490 tri_fill_mirror_8490 (flat top) — render3d.md §4.10 */
void tri_fill_mirror_8490(u16 a_bx, u16 b_si, u16 c_di) { r3_tri_flat_top(true, a_bx, b_si, c_di); }
/* 0e12:85a9 quad_fill_mirror — render3d.md §4.10 */
void quad_fill_mirror(u16 a_bx, u16 b_si, u16 c_di, u16 d_bp, u16 rec_es) { r3_quad_fill(true, a_bx, b_si, c_di, d_bp, rec_es); }

/* 0e12:7cd9 mirror_fill_above_edge — render3d.md §4.12 (as 3fe5, rows my[] >> 1) */
void mirror_fill_above_edge(u16 x0_ax, u16 x1_cx, u16 a_bx, u16 b_si) { r3_fill_above_edge(true, x0_ax, x1_cx, a_bx, b_si); }
/* 0e12:7d94 mirror_fill_below_edge — render3d.md §4.12 (as 3f6c, rows my[] >> 1, bottom 13h) */
void mirror_fill_below_edge(u16 x0_ax, u16 x1_cx, u16 a_bx, u16 b_si) { r3_fill_below_edge(true, 0x13, x0_ax, x1_cx, a_bx, b_si); }

/* 0e12:8b5f vertex_project_y_mirror — render3d.md §4.5 (BX = 2v: mirror row, half scale, centre row 0Ah) */
void vertex_project_y_mirror(u16 v_bx)
{
    u16 bx = R3_DIST(v_bx);
    u16 bp = 2;
    u16 dx = (u16)(R3_VY(v_bx) - DSW(DS_cam_y_949E));
    if (r3_neg(dx)) { dx = (u16)-dx; bp = 0; }
    if (dx >= bx) { u16 t = dx; dx = bx; bx = t; bp ^= 8; }
    u32 num = (u32)dx << 9;
    u16 idx = ((num >> 16) >= bx) ? 0x1FF : (u16)(num / bx);
    s16 y = (s16)(r3_cs16((u16)(0x1AFA + DSW(DS_oct_half + bp) + 2 * idx)) + DSW(DS_oct_y + bp));
    s8 r = (s8)DSB(DS_cam_roll_94A0);
    if (r) {                                              /* roll sign ignored, fixed centre 9400h (faithful) */
        u8 n = (u8)(r < 0 ? -r : r);
        s16 t = sar16((s16)((s16)(u16)(R3_SX(v_bx) - 0x9400) >> 1), n);
        y = (s16)(y - t);
    }
    y = (s16)(y - DSS(DS_pitch));                         /* car pitch 948F (not 9496) */
    R3_MY(v_bx) = (s16)((y >> 1) + 0x0A);
}

/* 0e12:7b9b mirror_sky_ground — render3d.md §4.12 */
void mirror_sky_ground(void)
{
    if (DSB(DS_external_panel_on) != 0) return;
    if (DSB(DS_mirror_on) == 0) return;
    DSB(DS_lzw_mirror_dirty) = 0;
    s16 s0 = R3_MY(0), s1 = R3_MY(2);
    DSW(DS_span_top) = 0xFFFF;
    s16 ax = (s16)-DSS(DS_pitch), dx = 0;
    u8 cl = DSB(DS_cam_roll_94A0);
    if (cl) {                                             /* roll sign ignored (faithful) */
        if ((s8)cl < 0) cl = (u8)-cl;
        dx = sar16(0x580, cl);
        ax = (s16)(ax - dx);
    }
    ax = (s16)((ax >> 1) + 0x0F);
    dx = (s16)((dx << 1) + ax);
    R3_MY(2) = dx;
    R3_MY(0) = ax;
    DSW(DS_colour) = DSW(DS_sky_pair);
    mirror_fill_above_edge(0, 0xB00, 0, 2);
    DSW(DS_colour) = DSW(DS_ground_pair);
    mirror_fill_below_edge(0, 0xB00, 0, 2);
    R3_MY(2) = s1;
    R3_MY(0) = s0;
}

/* 0e12:7c21 mirror_frame_draw — render3d.md §4.12 (grey glass when invalid / switched off, border, rim into V) */
void mirror_frame_draw(void)
{
    if (DSB(DS_external_panel_on) != 0) return;
    u16 es = DSW(DS_mirrorbuf_seg);
    bool clear;
    if (DSB(DS_lzw_mirror_dirty) != 0) clear = true;
    else if (DSB(DS_mirror_on) != 0) { DSB(DS_mirror_dirty) = 1; clear = false; }
    else clear = DSB(DS_mirror_dirty) != 0;
    if (clear) {
        DSB(DS_lzw_mirror_dirty) = 0;
        for (u16 k = 0; k < 0x344; k++) wr16(es, (u16)(2 * k), 0x0707);
    }
    u16 di = 0x108;                                       /* rows 3..5: 8 0 8 ... 8 0 8 */
    for (int k = 0; k < 3; k++) {
        wr8(es, di, 8); di++;
        wr16(es, di, 0x0800); di = (u16)(di + 0x54);
        wr16(es, di, 0x0008); di = (u16)(di + 2);
        wr8(es, di, 8); di++;
    }
    for (int k = 0; k < 12; k++) {                        /* rows 6..17: 8 0 ... 0 8 */
        wr16(es, di, 0x0008); di = (u16)(di + 0x56);
        wr16(es, di, 0x0800); di = (u16)(di + 2);
    }
    wr16(es, di, 0x0008); di = (u16)(di + 2);             /* row 18 */
    wr8(es, di, 8); di = (u16)(di + 0x53);
    wr16(es, di, 0x0008); di = (u16)(di + 2);
    wr8(es, di, 8);
    es = DSW(DS_viewbuf_seg);                             /* lower rim into V rows 14/15 */
    di = 0x1229;
    if (DSB(DS_half_window) != 0) di = (u16)(di - 0x28);
    wr8(es, di, 8); di++;
    for (int k = 0; k < 0x2A; k++) { wr16(es, di, 0); di = (u16)(di + 2); }
    wr8(es, di, 8);
    di = (u16)(di + 0xEC);
    for (int k = 0; k < 0x2A; k++) { wr16(es, di, 0x0808); di = (u16)(di + 2); }
}
