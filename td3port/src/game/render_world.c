/* render3d (render3d.md): world build, vertex transform, projection and the face sort.
 * 0e12:70b8 obj_ranges_clear, 70cd world_build_visible, 72f8 model_place, 75c1 model_emit_vertices,
 * 7653 vertex_rotate, 7487 model_emit_faces, 394c vertices_project, 39fd vertex_project_y,
 * 35f9 face_order_reset, 361c faces_sort_keys, 7a18 faces_quicksort_all, 7a35 faces_quicksort,
 * 7b06 faces_bubble_sort.
 * All state lives in mem[] at the original DGROUP addresses; vertex arguments are byte offsets (2v). */
#define RENDER3D_INTERNAL
#include "game/game.h"

/* sar by CL: the count is masked to 5 bits (286+); counts 16..31 fill with the sign */
static s16 sar16(s16 v, u8 n) { n &= 31; return n >= 15 ? (s16)(v < 0 ? -1 : 0) : (s16)(v >> n); }

/* 0e12:70b8 obj_ranges_clear — render3d.md §4.4 (B0F9[i] = B379[i] = 0 for the 160 object slots) */
void obj_ranges_clear(void)
{
    for (s16 bx = 0x13E; bx >= 0; bx -= 2) {
        DSW(DS_obj_vert_count + bx) = 0;
        DSW(DS_obj_face_count + bx) = 0;
    }
}

/* 0e12:70cd world_build_visible — render3d.md §4.4 */
void world_build_visible(void)
{
    u16 bx = (u16)(DSW(DS_cam_heading) + 0x1000);
    bx = (u16)(((bx >> 8) & 0xE0) >> 2);                 /* octant * 8 */
    u16 tab = (u16)(bx + (bx >> 2));                      /* octant * 10 */
    u8 al = (u8)(0x0F - (u8)(DSB(DS_cam_z + 1) >> 2));
    u16 cell = (u16)((DSB(DS_cam_x + 1) >> 2) + ((u16)al << 5));
    u16 cell2 = (u16)(cell << 1);
    u16 t = DSW(DS_leg_map + cell2);
    u8 type = (u8)t;
    if (DSB(DS_detail) == 0 && type < 0x77 && !(type >= 0x70 && type <= 0x73)) {
        u16 q = (u16)(DSW(DS_cam_heading) + 0x2000);
        q = (u16)(((q >> 8) & 0xC0) >> 2);               /* quadrant * 16 */
        tab = (u16)(q + (q >> 2));                        /* quadrant * 20: the even octant rows */
    }
    if (DSW(DS_last_cell) == cell2 && DSW(DS_last_octab) == tab) {
        u8 ah = 0;                                        /* only the route id */
        if (type == DSB(DS_route_tiles)) { DSB(DS_route_index) = ah; return; }
        ah++;
        if (type == DSB(DS_route_tiles + 1)) { DSB(DS_route_index) = ah; return; }
        ah++;
        if (type == DSB(DS_route_tiles + 2)) DSB(DS_route_index) = ah;
        return;
    }
    DSW(DS_lighthouse_slot) = 0xFFFF;
    DSW(DS_gate_slot1) = 0xFFFF;
    DSW(DS_gate_slot2) = 0xFFFF;
    DSB(DS_world_rebuilt) = 1;
    DSW(DS_last_cell) = cell2;
    DSW(DS_last_octab) = tab;
    DSB(DS_faces_resort) = 1;
    obj_ranges_clear();
    DSW(DS_face_count) = 0;
    DSW(DS_vert_count) = 0;
    DSB(DS_sprite_children) = 0;
    t = DSW(DS_leg_map + cell2);
    type = (u8)t;
    u16 n = 10;
    if (DSB(DS_menu_preview) != 0) n = 3;
    else {
        u8 dl = DSB(DS_detail);
        if (dl <= 1) {
            n = 6;
            if (dl != 1) {
                if (type >= 0x77) { }
                else if (type >= 0x70 && type <= 0x73) { }
                else n = 3;
            }
        }
    }
    u8 dh = (u8)(DSB(DS_cam_x + 1) & 0x7C), dl = (u8)(DSB(DS_cam_z + 1) & 0x3C);
    u16 si = DSW(DS_last_octab);
    do {
        u16 e = (u16)(0x7004 + 4 * r3_cs8((u16)(si + 0x7068)));
        si++;
        /* 7213: one cell */
        u8 ch_x = (u8)((dh + r3_cs8(e)) & 0x7C);
        u8 cz = (u8)((dl + r3_cs8((u16)(e + 1))) & 0x3C);
        u16 d = r3_cs16((u16)(e + 2));
        u16 c = cell;                                     /* bx >> 1 */
        u8 ax_lo = (u8)(d & 0x1F);
        u8 cl = (u8)(((u8)c + ax_lo) & 0x1F);
        c = (u16)((c & 0xFFE0) | cl);
        c = (u16)((c + ((d & 0xFF00) | (u8)((u8)d ^ ax_lo))) & 0x1FF);
        DSW(DS_cur_obj_slot1) = 0;
        u16 m = DSW(DS_leg_map + 2 * c);
        DSW(DS_place_y) = (u16)(m & 0x3F00);
        DSB(DS_place_rot) = (u8)((m >> 8) & 0xC0);
        DSB(DS_place_shear) = 0;
        DSW(DS_place_z) = (u16)((u8)(cz + 2) << 8);
        DSW(DS_place_x) = (u16)((u8)(ch_x + 2) << 8);
        model_place((u8)m, 0);
        /* static objects of that cell: i = A473-1 down to A477-1 (signed compare; the original's loop
         * bound includes A477-1, one below the first static object — faithful) */
        s16 lim = (s16)(2 * DSW(DS_obj_first_static) - 2);
        for (s16 so = (s16)(2 * DSW(DS_obj_count) - 2); so >= lim; so -= 2) {
            u16 o = (u16)so;
            if ((u8)(DSB(DS_obj_x + o + 1) & 0x7C) != ch_x) continue;
            DSW(DS_place_x) = DSW(DS_obj_x + o);
            u16 z = DSW(DS_obj_z + o);
            if ((u8)((z >> 8) & 0x3C) != cz) continue;
            DSW(DS_place_z) = z;
            DSW(DS_place_y) = (u16)(DSW(DS_obj_y8 + o) >> 3);
            u16 f = DSW(DS_obj_flags + o);
            if ((f >> 8) & 0x30) continue;                /* moving / parked: drawn by 4d60 */
            if ((f & 0x3F) == 0) continue;
            DSB(DS_place_rot) = (u8)((DSW(DS_obj_heading + o) >> 8) & 0xC0);
            DSB(DS_place_shear) = (u8)(DSW(DS_obj_pitch + o) >> 8);
            DSW(DS_cur_obj_slot1) = (u16)(o + 2);
            model_place((u8)f, 1);
        }
    } while (--n);
    DSW(DS_static_vert_end) = DSW(DS_vert_count);
    DSW(DS_static_face_end) = DSW(DS_face_count);
}

/* 0e12:72f8 model_place — render3d.md §4.4 (AL = model, AH = 0 tile / 1 O object) */
void model_place(u8 model_al, u8 kind_ah)
{
    u16 es, set, bx;
    DSW(DS_model_face_cut) = 0;
    if (kind_ah) {
        es = DSW(DS_objects_set + 2); set = DSW(DS_objects_set);
        bx = (u16)(set + 2 * model_al);
    } else {
        if (model_al >= 0x40) { es = DSW(DS_tiles_scene + 2); set = DSW(DS_tiles_scene); model_al = (u8)(model_al - 0x40); }
        else                  { es = DSW(DS_tiles_shared + 2); set = DSW(DS_tiles_shared); }
        bx = (u16)(set + 2 * model_al);
        u16 e = rd16(es, bx);
        if (e <= 0x10) { DSW(DS_model_face_cut) = e; bx = (u16)(bx - 2); }   /* alias of the previous model */
    }
    u16 si = (u16)(rd16(es, bx) + set);
    DSW(DS_model_ptr) = si;
    u8 nf = rd8(es, si);
    if (nf) {
        u8 nv = rd8(es, (u16)(si + 1));
        DSB(DS_vert_overflow) = 0;
        DSB(DS_place_nverts) = nv;
        u16 p = model_emit_vertices(far_make(es, (u16)(si + 4)), nv);
        if (DSB(DS_vert_overflow) == 0) model_emit_faces(far_make(es, p), nf);
    }
    /* sprite children (road tiles): they follow the faces */
    si = DSW(DS_model_ptr);
    u16 skip = (u16)(rd8(es, si) * 8 + rd8(es, (u16)(si + 1)) * 6);
    u8 nc = rd8(es, (u16)(si + 2));
    si = (u16)(si + 4 + skip);
    if (nc == 0) return;
    u8 al = DSB(DS_sprite_children);
    u16 i = al;
    if (al >= 0xF0) return;
    al = (u8)(al + nc);                                   /* 8-bit add (can wrap: faithful) */
    if (al >= 0xF0) { al = (u8)(al - 0xF0); nc = (u8)(nc - al); al = 0xF0; }
    DSB(DS_sprite_children) = al;
    DSB(DS_place_nverts) = nc;
    i = (u16)(i + DSW(DS_sprite_count));
    u16 di = DSW(DS_veg_seed);
    u8 rot = DSB(DS_place_rot);
    u16 cx = (u16)(DSW(DS_place_x) + DSW(DS_place_z));
    u8 cnt = (u8)((cx >> 8) >> 4);
    if (cnt) di = (u16)((di << cnt) | (di >> (16 - cnt)));   /* rol di, cl (cl <= 15) */
    do {
        u16 id = rd16(es, si);
        u16 o = (u16)(2 * i);
        bool place = true;
        if ((u8)id >= 0x12) {
            u16 carry = di & 1;
            di = (u16)((di >> 1) | (carry << 15));        /* ror di, 1 */
            if (!carry) place = false;
        }
        if (!place) {
            DSW(DS_sprite_id + o) = 0;
            si = (u16)(si + 8);
        } else {
            DSW(DS_sprite_id + o) = id;
            si = (u16)(si + 2);
            u16 x = rd16(es, si); si = (u16)(si + 2);
            u16 z = rd16(es, si); si = (u16)(si + 2);
            if (rot >= 0x40) {
                if (rot == 0x40)      { u16 t = x; x = z; z = (u16)-t; }
                else if (rot == 0x80) { x = (u16)-x; z = (u16)-z; }
                else                  { u16 t = x; x = (u16)-z; z = t; }
            }
            DSW(DS_sprite_x + o) = (u16)(x + DSW(DS_place_x));
            DSW(DS_sprite_z + o) = (u16)(z + DSW(DS_place_z));
            DSW(DS_sprite_y + o) = (u16)(rd16(es, si) + DSW(DS_place_y));
            si = (u16)(si + 2);
        }
        i++;
    } while (--DSB(DS_place_nverts));
}

/* 0e12:75c1 model_emit_vertices — render3d.md §4.4 (ES:SI = &A[0], CX = nv, DS:946B = loop count) */
u16 model_emit_vertices(FarPtr a_es_si, u16 nv_cx)
{
    DSW(DS_place_x4) = (u16)(DSW(DS_place_x) << 2);
    DSW(DS_place_z4) = (u16)(DSW(DS_place_z) << 2);
    u16 base = DSW(DS_vert_count);
    DSW(DS_vert_base) = base;
    u16 slot = (u16)(DSW(DS_cur_obj_slot1) - 2);
    if (base >= 0x630 || (u16)(base + nv_cx) >= 0x630) {
        DSB(DS_vert_overflow) = 1;
        if (!r3_neg(slot)) { DSW(DS_obj_vert_base + slot) = 0; DSW(DS_obj_vert_count + slot) = 0; }
        return a_es_si.off;                               /* SI not advanced */
    }
    DSW(DS_vert_count) = (u16)(base + nv_cx);
    if (!r3_neg(slot)) { DSW(DS_obj_vert_base + slot) = base; DSW(DS_obj_vert_count + slot) = nv_cx; }
    u16 stride = (u16)(nv_cx << 1);
    u16 di = (u16)(base << 1);
    FarPtr p = a_es_si;
    do {
        s16 a, b, c;
        vertex_rotate(p, stride, &a, &b, &c);
        R3_VY(di) = (s16)(a + DSW(DS_place_y));
        R3_VX(di) = (s16)(b + DSW(DS_place_x4));
        R3_VZ(di) = (s16)(c + DSW(DS_place_z4));
        p.off = (u16)(p.off + 2);
        di = (u16)(di + 2);
    } while (--DSB(DS_place_nverts));
    return (u16)(p.off + 2 * stride);                     /* skip B[] and C[] */
}

/* 0e12:7653 vertex_rotate — render3d.md §4.4 (ES:SI = &A[i], BX = stride; BD38 shear, 946A rotation) */
void vertex_rotate(FarPtr a_es_si, u16 stride_bx, s16 *a_dx, s16 *b_ax, s16 *c_cx)
{
    s16 dx = (s16)rd16(a_es_si.seg, a_es_si.off);
    s16 ax = (s16)rd16(a_es_si.seg, (u16)(a_es_si.off + stride_bx));
    s16 cx = (s16)rd16(a_es_si.seg, (u16)(a_es_si.off + 2 * stride_bx));
    s8 sh = (s8)DSB(DS_place_shear);
    if (sh) {                                             /* pitch shear */
        u8 n = (u8)(sh < 0 ? -sh : sh);
        s16 bp = sar16((s16)(ax << 1), n);
        if (sh >= 0) bp = (s16)-bp;
        dx = (s16)(dx + bp);
    }
    u8 r = DSB(DS_place_rot);
    if (r & 0x3F) {                                       /* fine rotation, Q15 tables DS:B7CF / DS:B851 */
        u16 i2 = (u16)((r & 0x3F) * 2);
        s16 sn = DSS(DS_sin64 + i2), cs = DSS(DS_cos64 + i2);
        s16 nb = (s16)((s16)(((s32)ax * cs) >> 15) + (s16)(((s32)cx * sn) >> 15));
        s16 nc = (s16)((s16)-(s16)(((s32)ax * sn) >> 15) + (s16)(((s32)cx * cs) >> 15));
        ax = nb; cx = nc;
        r &= 0xC0;
    }
    if (r >= 0x40) {
        if (r == 0x40)      { s16 t = ax; ax = cx; cx = (s16)-t; }
        else if (r == 0x80) { ax = (s16)-ax; cx = (s16)-cx; }
        else                { s16 t = ax; ax = (s16)-cx; cx = t; }
    }
    *a_dx = dx; *b_ax = ax; *c_cx = cx;
}

/* 0e12:7487 model_emit_faces — render3d.md §4.4 (ES:SI = first face, CX = nf, DS:945E = vertex base) */
u16 model_emit_faces(FarPtr faces_es_si, u16 nf_cx)
{
    u16 cx = (u16)(nf_cx - DSW(DS_model_face_cut));
    u16 bp = DSW(DS_vert_base);
    u16 ax = DSW(DS_face_count);
    u16 slot = (u16)(DSW(DS_cur_obj_slot1) - 2);
    if (ax >= 0x640) {
        if (!r3_neg(slot)) { DSW(DS_obj_face_base + slot) = 0; DSW(DS_obj_face_count + slot) = 0; }
        return faces_es_si.off;
    }
    u16 di = (u16)(ax * 10);
    ax = (u16)(ax + cx);
    if (ax >= 0x640) { ax = (u16)(ax - 0x640); cx = (u16)(cx - ax); ax = 0x640; }
    u16 old = DSW(DS_face_count);
    DSW(DS_face_count) = ax;
    if (!r3_neg(slot)) {
        DSW(DS_obj_face_base + slot) = old;
        DSW(DS_obj_face_count + slot) = cx;
        if (DSW(DS_obj_flags + slot) == 0x10) {           /* crossing gates */
            if (DSW(DS_gate_slot1) == 0xFFFF) DSW(DS_gate_slot1) = slot; else DSW(DS_gate_slot2) = slot;
        }
        if (DSW(DS_obj_flags + slot) == 0x0F) DSW(DS_lighthouse_slot) = slot;   /* lighthouse */
    }
    di = (u16)(di + DSW(DS_face_block));
    u16 fseg = r3_face_seg();
    u16 remap = DSW(DS_remap_ptr);
    u16 es = faces_es_si.seg, si = faces_es_si.off;
    do {
        u16 w;
        w = (u16)(rd16(es, si) + bp); wr16(fseg, di, w); si += 2; di += 2;
        w = (u16)(rd16(es, si) + bp); u8 c1 = (u8)((w >> 8) & 0xF8); wr16(fseg, di, (u16)(w & 0x07FF)); si += 2; di += 2;
        w = (u16)(rd16(es, si) + bp); u8 c2 = (u8)((w >> 8) & 0xF8); wr16(fseg, di, (u16)(w & 0x07FF)); si += 2; di += 2;
        w = (u16)(rd16(es, si) + bp); wr16(fseg, di, w); si += 2; di += 2;
        u8 cl = rd8(fseg, (u16)(remap + (c1 >> 3)));      /* remap at face_seg:[E5B0] */
        u8 ch = rd8(fseg, (u16)(remap + (c2 >> 3)));
        u16 pair;
        if (DSW(DS_video_mode) != 0x13) pair = (u16)(((u16)ch << 8 | cl) & 0x0F0F);
        else if (!(ch & 0x10) && !(cl & 0x10)) pair = DSW(DS_colour_pairs + 2 * (u8)((u8)(ch << 4) | cl));
        else {
            if (ch & 0x10) ch = DSB(DS_colour_direct + (ch & 0x0F));
            if (cl & 0x10) cl = DSB(DS_colour_direct + (cl & 0x0F));
            pair = (u16)((u16)ch << 8 | cl);
        }
        wr16(fseg, di, pair); di += 2;
    } while (--cx);
    return si;
}

/* 0e12:394c vertices_project — render3d.md §4.5 */
void vertices_project(void)
{
    u16 end = (u16)(DSW(DS_vert_count) << 1);
    for (u16 si = 0; si < end; si = (u16)(si + 2)) {
        u16 o = 0;
        u16 dx = (u16)(R3_VX(si) - DSW(DS_cam_x4));
        if (r3_neg(dx)) { dx = (u16)-dx; o = 6; }
        u16 cx = (u16)(R3_VZ(si) - DSW(DS_cam_z4));
        if (r3_neg(cx)) { cx = (u16)-cx; o ^= 2; }
        u16 ax;
        if (dx < cx) goto divide;
        ax = 0x1FF;
        if (dx == cx) goto have;
        { u16 t = dx; dx = cx; cx = t; } o ^= 8;
    divide:
        ax = 0;
        if (cx != 0) {
            u16 q = (u16)(((u32)dx << 16) / cx);          /* dx < cx: no overflow */
            ax = (u16)((q >> 7) & 0x1FF);
        }
    have:;
        u16 mx = cx;                                      /* xchg dx, cx: DX = max for the distance */
        u16 bx = (u16)(0x12F6 + 2 * ax);
        u16 a = r3_cs16(bx);
        u16 sx = (u16)(DSW(DS_oct_base + o) + r3_cs16((u16)(bx + DSW(DS_oct_half + o))) - DSW(DS_cam_heading));
        R3_SX(si) = sx;
        u16 cosv = r3_cs16((u16)(0x10F6 + (a >> 4)));
        u32 q = cosv ? ((u32)mx << 16) / cosv : 0xFFFFFFFFu;
        if (q > 0xFFFF) q = 0xFFFF;                       /* PORT: DIV overflow (INT 0) clamps (render3d.md §9.7) */
        u16 dist = (u16)q;
        R3_DIST(si) = dist;
        u16 dy = (u16)(R3_VY(si) - DSW(DS_cam_y_949E));
        if (r3_neg(dy)) dy = (u16)-dy;
        R3_DEPTH(si) = (u16)(dy + dist);
        R3_SY(si) = (s16)0x8000;
        R3_MY(si) = (s16)0x8000;
    }
}

/* 0e12:39fd vertex_project_y — render3d.md §4.5 (BX = 2v) */
void vertex_project_y(u16 v_bx)
{
    u16 bx = R3_DIST(v_bx);
    u16 bp = 2;
    u16 dx = (u16)(R3_VY(v_bx) - DSW(DS_cam_y_949E));
    if (r3_neg(dx)) { dx = (u16)-dx; bp = 0; }
    if (dx >= bx) { u16 t = dx; dx = bx; bx = t; bp ^= 8; }
    u32 num = (u32)dx << 9;
    u16 idx = ((num >> 16) >= bx) ? 0x1FF : (u16)(num / bx);
    s16 y = (s16)(r3_cs16((u16)(0x1AFA + DSW(DS_oct_half + bp) + 2 * idx)) + DSW(DS_oct_y + bp) + DSW(DS_cam_row));
    s8 r = (s8)DSB(DS_cam_roll_94A0);
    if (r) {
        u8 n = (u8)(r < 0 ? -r : r);
        s16 t = sar16((s16)(R3_SX(v_bx) - DSW(DS_view_cx32)), n);
        if (r >= 0) t = (s16)-t;
        y = (s16)(y + t);
    }
    R3_SY(v_bx) = y;
    if (y < DSS(DS_top_row)) DSS(DS_top_row) = y;
}

/* 0e12:35f9 face_order_reset — render3d.md §4.6 (order[i] = E5B8 + 10*i, 640h entries) */
void face_order_reset(void)
{
    u16 es = r3_face_seg(), di = DSW(DS_order_ofs), ax = DSW(DS_face_block);
    for (u16 k = 0; k < 0x640; k++) { wr16(es, di, ax); di = (u16)(di + 2); ax = (u16)(ax + 10); }
}

/* 0e12:361c faces_sort_keys — render3d.md §4.6 */
void faces_sort_keys(void)
{
    if (DSB(DS_faces_resort) || DSB(DS_world_rebuilt)) {
        face_order_reset();
        DSW(DS_top_row_prev) = 0;
    }
    u16 es = r3_face_seg();
    u16 end = (u16)(DSW(DS_face_count) << 1);
    for (u16 si = 0; si < end; ) {
        u16 sp = (u16)(si + DSW(DS_order_ofs));
        u16 bx = rd16(es, sp);
        u16 w0 = rd16(es, bx);
        u16 cx = R3_DEPTH((w0 & 0x7FF) << 1);
        bx = (u16)(bx + 2);
        u16 di = (u16)(rd16(es, bx) << 1);
        u8 ah = (u8)((w0 >> 8) & 0xE0);
        if (ah & 0xC0) {
            if (ah & 0x20) {                              /* farthest vertex */
                ah &= 0xC0;
                u16 ax = R3_DEPTH(di);
                if (ah >= 0x80) {
                    if (ah != 0x80) {
                        if (cx < ax) cx = ax;
                        bx = (u16)(bx + 2);
                        ax = R3_DEPTH(rd16(es, bx) << 1);
                    }
                    if (cx < ax) cx = ax;
                    bx = (u16)(bx + 2);
                    ax = R3_DEPTH((rd16(es, bx) & 0x7FF) << 1);
                }
                if (cx < ax) cx = ax;
            } else {
                u32 s = (u32)cx + R3_DEPTH(di);
                cx = (u16)s;
                if (ah < 0x80) cx >>= 1;                  /* carry of the add is lost */
                else if (ah == 0x80) {
                    bx = (u16)(bx + 2);
                    s = (u32)cx + R3_DEPTH(rd16(es, bx) << 1);
                    cx = (u16)(s >> 1);                   /* rcr: 17-bit sum */
                    u16 a = (u16)(cx >> 2);
                    cx = (u16)(cx - a);
                    a >>= 2;
                    cx = (u16)(cx - a);
                } else {
                    bx = (u16)(bx + 2);
                    u16 ax = R3_DEPTH(rd16(es, bx) << 1);
                    bx = (u16)(bx + 2);
                    ax = (u16)(ax + R3_DEPTH((rd16(es, bx) & 0x7FF) << 1));
                    s = (u32)cx + ax;
                    cx = (u16)(s >> 1);
                    cx >>= 1;
                }
            }
        }
        sp = (u16)(sp + 0xC80);
        wr16(es, sp, cx);
        si = (u16)(sp - DSW(DS_key_ofs) + 2);
    }
    if (DSB(DS_faces_resort) == 0 && DSB(DS_world_rebuilt) == 0) {
        u16 lo = DSW(DS_key_ofs);
        u16 hi = (u16)(((u16)(DSW(DS_face_count) - 1) << 1) + lo);
        faces_bubble_sort(lo, hi);
        return;
    }
    faces_quicksort_all();
    DSB(DS_faces_resort) = 0;
}

/* 0e12:7a18 faces_quicksort_all — render3d.md §4.6 */
void faces_quicksort_all(void)
{
    u16 lo = DSW(DS_key_ofs);
    u16 hi = (u16)(((u16)(DSW(DS_face_count) - 1) << 1) + lo);
    faces_quicksort(lo, hi);
}

/* swaps two key entries and their order entries (-C80h) in the face segment */
static void face_swap(u16 es, u16 si, u16 di)
{
    u16 t = rd16(es, di); wr16(es, di, rd16(es, si)); wr16(es, si, t);
    si = (u16)(si - 0xC80); di = (u16)(di - 0xC80);
    t = rd16(es, di); wr16(es, di, rd16(es, si)); wr16(es, si, t);
}

static void face_sub_sort(u16 lo, u16 hi)
{
    s16 d = (s16)(hi - lo);
    if (d <= 0) return;
    if ((u16)d > 0x28) faces_quicksort(lo, hi); else faces_bubble_sort(lo, hi);
}

/* 0e12:7a35 faces_quicksort — render3d.md §4.6 (ascending; lo/hi = byte offsets into the key array) */
void faces_quicksort(u16 lo, u16 hi)
{
    u16 es = r3_face_seg();
    u16 si = lo, di = hi;
    u16 dx = rd16(es, si);
    si = (u16)(si + 2);
    for (;;) {
        /* 7a43 */
        if (dx < rd16(es, si)) {
            /* 7aa9 */
            for (;;) {
                if (dx > rd16(es, di)) {                  /* 7ade */
                    face_swap(es, si, di);
                    si = (u16)(si + 2);
                    if (di <= si) goto meet;
                    di = (u16)(di - 2);
                    break;                                /* back to 7a43 */
                }
                di = (u16)(di - 2);
                if (di < si) goto meet;
            }
            continue;
        }
        si = (u16)(si + 2);
        if (si <= di) continue;
        /* si > di: pivot position p = di */
        si = lo;
        if (si != di) face_swap(es, si, di);
        goto left;
    }
meet:                                                     /* 7ab4 */
    di = (u16)(si - 2);
    si = lo;
    if (si == di) { si = (u16)(si + 2); goto right; }     /* 7a8c */
    face_swap(es, si, di);
left:                                                     /* 7a72 */
    di = (u16)(di - 2);
    face_sub_sort(si, di);
    si = (u16)(di + 4);
right:                                                    /* 7a8e */
    face_sub_sort(si, hi);
}

/* 0e12:7b06 faces_bubble_sort — render3d.md §4.6 (ascending, passes end at the last swap; QUIRK: stops when
 * the remaining range is <= 2 elements) */
void faces_bubble_sort(u16 lo, u16 hi)
{
    u16 es = r3_face_seg();
    u16 di = (u16)(hi + 2);
    u16 si = lo;
    for (;;) {
        /* 7b14: one pass */
        u16 bp = (u16)(di - 4);
        u16 dx = rd16(es, si);
        si = (u16)(si + 2);
        bool swapped = false;
        u16 last = 0;
        for (;;) {
            /* 3-step unrolled part: signed compare before the first swap, unsigned after */
            bool more = swapped ? (si < bp) : ((s16)si < (s16)bp);
            int steps = more ? 3 : 0;
            if (!more) { if (!(si < di)) break; steps = 1; }
            for (int k = 0; k < steps; k++) {
                u16 ax = rd16(es, si);
                si = (u16)(si + 2);
                if (dx > ax) {                            /* 7b80: swap with the previous entry */
                    si = (u16)(si - 2);
                    last = si;
                    u16 big = rd16(es, (u16)(si - 2));
                    wr16(es, (u16)(si - 2), ax);
                    wr16(es, si, big);
                    u16 o = (u16)(si - 0xC82);
                    u16 t = rd16(es, o);
                    wr16(es, o, rd16(es, (u16)(o + 2)));
                    wr16(es, (u16)(o + 2), t);
                    si = (u16)(si + 2);
                    dx = big;                             /* carry keeps the larger key */
                    swapped = true;
                    break;                                /* re-enter the loop at the bound check */
                }
                dx = ax;
            }
        }
        if (!swapped) return;
        di = last;
        si = lo;
        if (!(di > (u16)(si + 4))) return;
    }
}
