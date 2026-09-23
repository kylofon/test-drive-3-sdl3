#define RENDER3D_INTERNAL
#include "game/game.h"

/* render3d: cockpit overlays drawn into the view buffer V after the faces and sprites (render3d.md §4.12):
 * rain streaks / snow flakes, windscreen drops, wipers, headlight beams, the dashboard edge.
 * Overlay polygons use the vertex slots 630h..63Fh (byte offsets 0C60h..0C7Eh) of scr_x / scr_y and fake
 * face records in DGROUP (DS:BAD0 = DS offset, ES = DS). All state is in mem[] at its original address. */

/* ---- file-internal functions (register routines: parameters named after the registers) ---- */
/* 0e12:46dc precipitation_draw */
void precipitation_draw(void);
/* 0e12:477c rain_streaks (ES = V, AL = colour, DI = random base, SI = level - 1, BX = limit) */
void rain_streaks(u16 v_es, u8 colour_al, u16 base_di, u16 m_si, u16 limit_bx);
/* 0e12:47aa snow_flakes (same registers) */
void snow_flakes(u16 v_es, u8 colour_al, u16 base_di, u16 m_si, u16 limit_bx);
/* 0e12:47d1 windscreen_spawn (SI = level, DL = drop type) */
void windscreen_spawn(u16 n_si, u8 type_dl);
/* 0e12:487e windscreen_drops_draw */
void windscreen_drops_draw(void);
/* 0e12:48f5 drop_shape_fresh (ES:DI = V position, AX = colour pair, BX = limit) */
void drop_shape_fresh(u16 v_es, u16 pos_di, u16 colour_ax, u16 limit_bx);
/* 0e12:495c drop_shape_old (same registers) */
void drop_shape_old(u16 v_es, u16 pos_di, u16 colour_ax, u16 limit_bx);
/* 0e12:4a5c wipers_update_draw */
void wipers_update_draw(void);
/* 0e12:4b37 headlight_beam_draw */
void headlight_beam_draw(void);
/* 0e12:4bf3 dashboard_edge_draw */
void dashboard_edge_draw(void);
/* 0e12:4cb0 overlay_quads4_draw (stack argument: edge-line colour pair, 0 = no edge lines) */
void overlay_quads4_draw(u16 edge);

/* 0e12:46c0 cockpit_overlays — render3d.md §4.12 */
void cockpit_overlays(void)
{
    precipitation_draw();
    if (DSB(DS_ext_view) == 0) {
        headlight_beam_draw();
        dashboard_edge_draw();
        wipers_update_draw();
    }
}

/* 0e12:46dc precipitation_draw — render3d.md §4.12 */
void precipitation_draw(void)
{
    DSB(DS_sky_alt) = 1;
    u16 bx = DSB(DS_half_window) ? 0x5000 : 0x7800;
    u16 si = (u16)(DSW(DS_snow_level) - 1);
    if (!r3_neg(si)) {                                   /* snow (priority over rain) */
        if (DSW(DS_roof_height) != 0) goto fade;         /* under a roof: sky_alt stays 1 */
        u16 es = DSW(DS_viewbuf_seg);
        DSW(DS_top_row) = 0;
        snow_flakes(es, 0x0F, DSW(DS_rand_lo), si, bx);
        snow_flakes(es, 0x0F, DSW(DS_rand_hi), (u16)(DSW(DS_snow_level) - 1), bx);
        windscreen_spawn(DSW(DS_snow_level), 0x80);
        return;
    }
    si = (u16)(DSW(DS_rain_level) - 1);
    if (!r3_neg(si)) {                                   /* rain */
        if (DSW(DS_roof_height) != 0) goto fade;
        u16 es = DSW(DS_viewbuf_seg);
        DSW(DS_top_row) = 0;
        rain_streaks(es, 0x07, DSW(DS_rand_lo), si, bx);
        rain_streaks(es, 0x07, DSW(DS_rand_hi), (u16)(DSW(DS_rain_level) - 1), bx);
        windscreen_spawn(DSW(DS_rain_level), 0x40);
        return;
    }
    DSB(DS_sky_alt) = 0;
fade:                                                    /* no weather / roof: retire one drop slot */
    if (DSB(DS_rand_lo) & 4) return;
    {
        u8 bl = (u8)((DSB(DS_drop_ring_idx) + 1) & 0x3F);
        DSB(DS_drop_ring_idx) = bl;
        DSB(DS_drop_state + bl) = 0;
    }
}

/* 0e12:477c rain_streaks — render3d.md §4.12
 * 2n streaks of ((n+1)>>1)+3 pixels. stosb advances DI, so each pixel is one row down AND one column
 * right of the previous one (+141h: slanted streaks; the spec's +140h misses the stosb increment). */
void rain_streaks(u16 v_es, u8 colour_al, u16 base_di, u16 m_si, u16 limit_bx)
{
    u16 cx0 = (u16)((u16)(m_si + 2) >> 1) + 3;
    u16 si = (u16)((m_si << 2) + 2);                     /* byte offset into BB14[] */
    do {
        u16 di = (u16)((base_di + DSW(DS_precip_offsets + si)) & 0x7FFF);
        u16 cx = cx0;
        do {
            if (di >= limit_bx) break;
            wr8(v_es, di, colour_al);
            di = (u16)(di + 1 + 0x140);
        } while (--cx);
        si = (u16)(si - 2);
    } while (!r3_neg(si));
}

/* 0e12:47aa snow_flakes — render3d.md §4.12
 * One pixel per offset plus a copy at DI ^ 4000h (limit always 7800h). The copy's DI is taken after the
 * stosb increment when the first pixel was written (one column to the right). */
void snow_flakes(u16 v_es, u8 colour_al, u16 base_di, u16 m_si, u16 limit_bx)
{
    u16 si = (u16)((m_si << 2) + 2);
    do {
        u16 di = (u16)((base_di + DSW(DS_precip_offsets + si)) & 0x7FFF);
        if (di < limit_bx) { wr8(v_es, di, colour_al); di++; }
        di ^= 0x4000;
        if (di < 0x7800) wr8(v_es, di, colour_al);
        si = (u16)(si - 2);
    } while (!r3_neg(si));
}

/* 0e12:47d1 windscreen_spawn — render3d.md §4.12 */
void windscreen_spawn(u16 n_si, u8 type_dl)
{
    u16 bx = (u16)((u8)(DSB(DS_rand_lo) >> 2) & 7);
    if (bx < n_si) windscreen_splat(DSW(DS_rand_hi), type_dl);
    bx = (u16)(DSB(DS_rand_hi) & 7);
    if (bx < n_si) windscreen_splat(DSW(DS_rand_lo), type_dl);
}

/* 0e12:47fc windscreen_splat — render3d.md §4.12 (also called by the simulation's sprite_collisions) */
void windscreen_splat(u16 rand_ax, u8 type_dl)
{
    u8 bl = (u8)((DSB(DS_drop_ring_idx) + 1) & 0x3F);
    DSB(DS_drop_ring_idx) = bl;
    DSB(DS_drop_state + bl) = type_dl;                   /* age 0 */
    u16 bx = (u16)(bl << 1);
    u16 ax = (u16)(rand_ax >> 1);
    DSW(DS_drop_pos + bx) = ax;
    u16 dx;
    ax = div32_16(ax, 0x140, &dx);                       /* ax = row, dx = column */
    u16 cx = 0x8000;                                     /* unwipeable */
    ax = (u16)(ax - 0x60);
    if (r3_neg(ax)) {                                    /* above row 96: maybe in a wiper's reach */
        ax = (u16)-ax;                                   /* dy */
        u16 si = 0;
        dx = (u16)(dx - 0x2F);                           /* pivots at x 47 and 47+127 */
        if ((s16)dx >= 0x7F) dx = (u16)(dx - 0x7F);
        if (r3_neg(dx)) { dx = (u16)-dx; si++; }
        u16 di = dx;
        if (di > ax) di = (u16)(di + dx + dx);           /* octagonal distance */
        else di = (u16)(di + ax + ax);
        di = (u16)(di + ax);
        di >>= 2;
        if (di <= DSW(DS_wiper_radius)) {
            cx = ax;
            u16 hi = (u16)(dx >> 8);                     /* DX:AX = dx << 8 */
            u16 lo = (u16)((dx & 0xFF) << 8);
            di = 0x7FFF;
            if (hi < cx) di = (u16)(div32_16(((u32)hi << 16) | lo, cx, NULL) >> 1);
            if (si) di = (u16)-di;
            cx = di;
        }
    }
    DSW(DS_drop_key + bx) = cx;
}

/* 0e12:487e windscreen_drops_draw — render3d.md §4.12
 * Disassembly: when a drop's age wraps to 0 it is REMOVED while it rains/snows, and kept (age 8, type
 * restored) when there is no weather — the inverse of the spec's pseudocode. */
void windscreen_drops_draw(void)
{
    u16 es = DSW(DS_viewbuf_seg);
    u16 si = 0;
    u16 cx = 0x40;
    do {
        u8 dl = DSB(DS_drop_state + si);
        if (dl != 0) {
            dl++;
            u8 dh;
            for (;;) {
                dh = dl & 0x3F;
                if (dh != 0) break;
                if ((DSW(DS_rain_level) | DSW(DS_snow_level)) != 0) break;
                dl = (u8)(dl - 0x38);
            }
            if (dh == 0) {
                DSB(DS_drop_state + si) = (u8)(cx >> 8);   /* CH = 0 */
            } else {
                DSB(DS_drop_state + si) = dl;
                u8 t = dl & 0xC0;
                u16 ax;
                if (t == 0) ax = 0x0606;                 /* bug: brown */
                else if (t == 0x80) ax = 0x0F0F;         /* snow: white */
                else if (t < 0x80) ax = 0x0707;          /* rain: grey */
                else ax = 0x0000;                        /* black */
                u16 di = DSW(DS_drop_pos + (u16)(si << 1));
                DSW(DS_top_row) = 0;
                u16 bx = 0x77FB;
                if (DSB(DS_half_window)) { di = (u16)(di - 0x1428); bx = 0x4FFB; }
                if (dh > 1) drop_shape_old(es, di, ax, bx);
                else drop_shape_fresh(es, di, ax, bx);
            }
        }
        si++;
    } while (--cx);
}

/* Drop-shape pixel helpers: word writes store AL at the column and AH at the next one. */
#define DROP_ROW(delta) (di = (u16)((di + (u16)(delta)) & 0x7FFF), di <= limit_bx)
#define DROP_B(o) wr8(v_es, (u16)(di + (o)), (u8)colour_ax)
#define DROP_W(o) wr16(v_es, (u16)(di + (o)), colour_ax)

/* 0e12:48f5 drop_shape_fresh — render3d.md §4.12 (5-row blob, age <= 1) */
void drop_shape_fresh(u16 v_es, u16 pos_di, u16 colour_ax, u16 limit_bx)
{
    u16 di = pos_di;
    if (DROP_ROW(-0x3C1)) { DROP_W(0); }
    if (DROP_ROW(0x13F)) { DROP_W(0); DROP_W(2); DROP_B(4); }
    if (DROP_ROW(0x13F)) { DROP_W(0); DROP_W(2); DROP_W(4); DROP_B(6); }
    if (DROP_ROW(0x141)) { DROP_W(0); DROP_W(2); DROP_B(5); }
    if (DROP_ROW(0x141)) { DROP_W(0); DROP_B(3); }
}

/* 0e12:495c drop_shape_old — render3d.md §4.12 (speckle; black variant when the colour byte < 6) */
void drop_shape_old(u16 v_es, u16 pos_di, u16 colour_ax, u16 limit_bx)
{
    u16 di = pos_di;
    if ((u8)colour_ax < 6) {
        if (DROP_ROW(-0x77F)) { DROP_B(0); }
        if (DROP_ROW(0x27E)) { DROP_B(0); }
        if (DROP_ROW(0x281)) { DROP_B(0); DROP_B(3); }
        if (DROP_ROW(0x13D)) { DROP_B(0); DROP_B(3); }
        if (DROP_ROW(0x144)) { DROP_B(0); }
        if (DROP_ROW(0x13E)) { DROP_B(0); DROP_W(1); }
        if (DROP_ROW(0x141)) { DROP_B(0); }
        return;
    }
    if (DROP_ROW(-0x501)) { DROP_B(0); }
    if (DROP_ROW(0x13D)) { DROP_B(0); DROP_W(3); DROP_B(7); }
    if (DROP_ROW(0x13F)) { DROP_B(0); }
    if (DROP_ROW(0x143)) { DROP_W(0); DROP_B(2); DROP_B(6); }
    if (DROP_ROW(0x13E)) { DROP_B(0); DROP_W(3); DROP_B(5); DROP_W(7); }
    if (DROP_ROW(0x140)) { DROP_W(0); }
    if (DROP_ROW(0x144)) { DROP_B(0); DROP_B(4); }
    if (DROP_ROW(0x13D)) { DROP_B(0); DROP_W(3); }
}

#undef DROP_ROW
#undef DROP_B
#undef DROP_W

/* 0e12:4a5c wipers_update_draw — render3d.md §4.12 */
void wipers_update_draw(void)
{
    u8 bl = DSB(DS_wiper_phase);
    if (bl == 0) {
        if (DSB(DS_wipers_on) == 0) goto no_store;
        bl++;
    } else if (!(bl & 0x80)) {
        bl++;
        if (bl < 5) goto store;
        bl = 0x84;
        goto back;
    } else {
    back:
        bl--;
        if (bl == 0x80) bl = 0;
    }
store:
    DSB(DS_wiper_phase) = bl;
no_store:
    {
        u8 cl = bl;
        u16 bx = (u16)((bl & 7) << 1);
        u16 si = bx;
        bx = (u16)(bx - 2);
        if (cl & 0x80) { bx = (u16)(bx + 2); si = (u16)(si + 2); }
        s16 ax = DSS(0x9527 + bx);                       /* wiper_keys[-1..4] (DS:9525) */
        s16 dx = DSS(0x9527 + si);
        for (u16 i = 0; i < 0x40; i++) {                 /* drops swept by the blade this frame */
            s16 k = DSS(DS_drop_key + 2 * i);
            if (k > ax || k < dx) continue;
            DSB(DS_drop_state + i) = 0;
        }
    }
    windscreen_drops_draw();
    {
        u16 bx = (u16)((DSB(DS_wiper_phase) & 7) * 12);
        u16 si = 0xC68;
        for (u16 cx = 6; cx; cx--) {
            u16 w = DSW(DS_wiper_verts + bx);
            bx = (u16)(bx + 2);
            u16 ax = (u16)(w >> 8);                      /* y */
            u16 dx = (u16)((w & 0xFF) << 5);             /* x in 1/32 px */
            if (DSB(DS_half_window)) { ax = (u16)(ax - 0x10); dx = (u16)(dx - 0x500); }
            if ((s16)ax < DSS(DS_top_row)) DSW(DS_top_row) = ax;
            R3_SY(si) = (s16)ax;
            R3_SY(si + 0x0C) = (s16)ax;
            R3_SX(si) = dx;
            R3_SX(si + 0x0C) = (u16)(dx + DSW(DS_wiper2_dx));
            si = (u16)(si + 2);
        }
    }
    DSW(DS_colour) = 0;
    overlay_quads4_draw(0x0707);
}

/* 0e12:4b37 headlight_beam_draw — render3d.md §4.12 */
void headlight_beam_draw(void)
{
    if (DSB(DS_headlights) == 0) return;
    if (DSB(DS_colour_mode) != 0 && DSW(DS_rain_level) == 0 && DSW(DS_snow_level) == 0) return;
    for (u16 q = 0; q < 2; q++) {
        u16 bx = (u16)(q * 8);                           /* second quad starts 8 bytes in: shares 2 vertices */
        u16 di = 0xC60;
        for (u16 cx = 4; cx; cx--) {
            u16 ax = DSW(DS_beam_verts + bx);            /* y */
            bx = (u16)(bx + 2);
            u16 dx = DSW(DS_beam_verts + bx);            /* x << 5 */
            bx = (u16)(bx + 2);
            if (DSB(DS_half_window)) { ax = (u16)(ax - 0x10); dx = (u16)(dx - 0x500); }
            R3_SY(di) = (s16)ax;
            R3_SX(di) = dx;
            di = (u16)(di + 2);
        }
        if (q == 0) {
            DSW(DS_colour) = 0x0808;
            DSB(DS_face_type) = 0;                       /* OR mode: brightens V */
        }
        DSW(DS_face_ptr) = 0xBC7D;
        quad_fill(0xC66, 0xC60, 0xC62, 0xC64, DGROUP);   /* BX, SI, DI, BP */
    }
}

/* 0e12:4bf3 dashboard_edge_draw — render3d.md §4.12 */
void dashboard_edge_draw(void)
{
    u16 bx = DS_dash_verts;
    u16 edge = DSW(DS_car_cockpit_edge);
    if (DSB(DS_crashed)) { bx = DS_dash_verts_crashed; edge = 0x0707; }
    if (DSB(DS_crash_water)) return;
    u16 si = 0xC68;
    for (u16 cx = 12; cx; cx--) {
        u16 dx = DSW(bx);                                /* x */
        bx = (u16)(bx + 2);
        u16 ax = DSW(bx);                                /* y */
        bx = (u16)(bx + 2);
        dx = (u16)(dx << 5);
        if (DSB(DS_half_window)) { ax = (u16)(ax - 0x10); dx = (u16)(dx - 0x500); }
        R3_SY(si) = (s16)ax;
        R3_SX(si) = dx;
        si = (u16)(si + 2);
    }
    DSW(DS_colour) = DSW(DS_car_cockpit_colour);
    overlay_quads4_draw(edge);
}

/* 0e12:4cb0 overlay_quads4_draw — render3d.md §4.12 */
void overlay_quads4_draw(u16 edge)
{
    DSB(DS_face_type) = 0x70;
    DSW(DS_face_ptr) = 0xBC85;
    quad_fill(0xC6E, 0xC68, 0xC6A, 0xC6C, DGROUP);       /* BX, SI, DI, BP */
    DSW(DS_face_ptr) = 0xBC8D;
    quad_fill(0xC72, 0xC6C, 0xC6E, 0xC70, DGROUP);
    DSW(DS_face_ptr) = 0xBC95;
    quad_fill(0xC7A, 0xC74, 0xC76, 0xC78, DGROUP);
    DSW(DS_face_ptr) = 0xBC9D;
    quad_fill(0xC7E, 0xC78, 0xC7A, 0xC7C, DGROUP);
    DSW(DS_vert_base) = 0;
    if (edge == 0) return;
    DSW(DS_colour) = edge;
    line_draw(0xC6A, 0xC6C, R3_SX(0xC6A), R3_SX(0xC6C), ds_ptr(0xBC89));
    line_draw(0xC76, 0xC78, R3_SX(0xC76), R3_SX(0xC78), ds_ptr(0xBC99));
}
