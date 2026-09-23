#define RENDER3D_INTERNAL
#include "game/game.h"

/* Render3d: sprites (render3d.md §4.13) — load-time pre-scaling into the far sprite cache, per-frame list /
 * projection / sort / animation, the face + sprite painter merge loop (0e12:323e) and the transparent blitters
 * into the view V (DS:90D0) and the mirror M (DS:90D2).
 *
 * Everything lives in mem[]: the sprite file at DS:2500, the cache block at segment DS:B9EB (ES of the build
 * routines, DS/ES of the blitters through the copy DS:B9ED), the sprite arrays DS:8902/8A32/8B62/8C92 (stride
 * 130h), the 0e12 CS tables (atan 12F6 / 1AFA, cos 10F6, scale masks 267E) and the immediates view_setup
 * patches into 0e12:2b93 / 2b9a. Register routines take their registers as parameters named after them. */

/* ---- file-internal routines (register interfaces) ---- */
/* 0e12:276e: DI = first pixel byte, DX = end, SI = remap table (DS offsets) */
void sprite_pixels_remap(u16 di, u16 dx, u16 si);
/* 0e12:2810: AL = sprite id, DI = cache write offset -> new DI */
u16  sprite_prescale_one(u8 id_al, u16 di);
/* 0e12:289a: DI = cache write offset (B9D6/B9D7 scale, B9D8 id) -> new DI */
u16  sprite_scale_setup(u16 di);
/* 0e12:28eb: BX = k (visible index) */
void sprite_draw(u16 k_bx);
/* 0e12:2999 / 2b74: BX = k, SI = first stored pixel row of the copy */
void sprite_blit_mirror(u16 k_bx, u16 si);
void sprite_blit_main(u16 k_bx, u16 si);
/* 0e12:2a9e / 2c7c: AL = top row (>= 0), SI = pixel row */
void sprite_rows_mirror(u8 y_al, u16 si);
void sprite_rows_main(u8 y_al, u16 si);
/* 0e12:2d54 / 2dc6: SI = row-pointer table, CX = row-length table (DS offsets), DI -> new DI */
u16  sprite_scale_rows(u16 ptrs_si, u16 lens_cx, u16 di);
u16  sprite_scale_rows_up(u16 ptrs_si, u16 lens_cx, u16 di);
/* 0e12:2e49: BX = 2k, DX = depth key */
void sprite_pick_scale(u16 k2_bx, u16 key_dx);
/* 0e12:2eb6 */
void sprite_scale_patterns(void);
/* 0e12:2f84 / 3004: BX = source pixels (DS), CL = row-length byte, DI -> new DI (2f84 also leaves DX = DI at entry) */
u16  sprite_scale_row(u16 src_bx, u8 len_cl, u16 di);
u16  sprite_scale_row_up(u16 src_bx, u8 len_cl, u16 di);
/* 0e12:3085, 312e, 3147, 316c, 3352, 78a7 */
void sprite_list_build(void);
void sprite_sort(void);
void sprite_sort_bubble(void);
void sprite_project(void);
void sprite_animate(void);
void sprite_sort_quick(void);
/* 0e12:78cc: [bp+6] = first ptr, [bp+4] = last ptr (DS offsets into 8902), BX = 2 */
void sprite_qsort(u16 lo_ptr, u16 hi_ptr);
/* 0e12:794b: SI, DI = pointers into 8902 */
void sprite_rec_swap(u16 si, u16 di);
/* 0e12:797b: [bp+6] = first ptr, [bp+4] = last ptr */
void sprite_bubble_sort(u16 first_ptr, u16 last_ptr);

#define SPR_STRIDE 0x130   /* 8902 -> 8A32 -> 8B62 -> 8C92 */

/* rol byte ptr [B9EF + bx], 1 -> CF (old bit 7) */
static inline bool hmask_rol(u16 bx)
{
    u8 v = DSB(DS_hmask + bx);
    bool c = (v & 0x80) != 0;
    DSB(DS_hmask + bx) = (u8)(v << 1 | (c ? 1 : 0));
    return c;
}

/* mov al,[BA0C]; shl al,1; rcl [BA0A]; rcl [BA0B]; rcl [BA0C] -> CF = old bit 7 of BA0C */
static inline bool vmask_next(void)
{
    u8 a = DSB(DS_vmask), b = DSB(DS_vmask + 1), c = DSB(DS_vmask + 2);
    DSB(DS_vmask)     = (u8)(a << 1 | c >> 7);
    DSB(DS_vmask + 1) = (u8)(b << 1 | a >> 7);
    DSB(DS_vmask + 2) = (u8)(c << 1 | b >> 7);
    return (c & 0x80) != 0;
}

/* ============================================================================================ load time */

/* 0e12:276e sprite_pixels_remap — render3d.md §4.13 (ES = DS) */
void sprite_pixels_remap(u16 di, u16 dx, u16 si)
{
    if ((u8)DSW(DS_video_mode) == 0x13) return;
    do {
        u8 al = DSB(di);
        if (al >= 0x10) al = DSB((u16)(al + si - 0x10));
        DSB(di) = al;
        di++;
    } while (di < dx);
}

/* 0e12:2789 sprites_prescale — render3d.md §4.13 */
void sprites_prescale(void)
{
    u16 di = (u16)(DSW(0x2502) /* spr_pix_start */ + 0x2500);
    u16 dx = (u16)(DSW(DS_spr_remap_off) + 0x2500);
    sprite_pixels_remap(di, dx, dx);

    DSW(DS_sprite_cache_seg) = (u16)((DSW(DS_sprite_cache) >> 4) + 1 + DSW(DS_sprite_cache + 2));
    u16 bx = (u16)(DSB(DS_sprite_scale_count) - 1);                 /* bl = n, bh = 0, dec bx */
    DSB(DS_sprite_max_size) = DSB((u16)(DS_step_to_size + bx));
    bx = 0;
    di = 0x40;
    u16 es = DSW(DS_sprite_cache_seg);
    DSW(DS_cache_limit) = 0xD000;
    do {
        wr16(es, (u16)(bx << 1), di);
        if (bx != 0) {
            u16 old = di;
            di = sprite_prescale_one((u8)bx, di);                   /* AX = BX (AH = 0) */
            if (di < old || di > DSW(DS_cache_limit)) {
                message_box(0x1A);
                return;
            }
        }
        bx++;
    } while (bx < DSW(DS_spr_count));
}

/* 0e12:2810 sprite_prescale_one — render3d.md §4.13 (ES = cache seg) */
u16 sprite_prescale_one(u8 id_al, u16 di)
{
    u16 es = DSW(DS_sprite_cache_seg);
    DSB(DS_spr_num) = id_al;
    u16 cx = (u16)(DSB(DS_sprite_scale_count) << 1);
    u8 mode = DSB((u16)(DS_sprite_kind + id_al)) & 7;               /* BX = AX, AH = 0 from 2789 */

    if (mode == 0) {
        u16 bx = di;
        di = (u16)(di + cx);
        DSW(DS_last_wh) = 0xFFFF;
        u8 ah = 0;
        do {
            wr16(es, bx, di);
            DSB(DS_spr_scale) = DSB((u16)(DS_step_to_size + ah));
            DSB(DS_spr_grow) = 0;
            u16 save = di;
            di = sprite_scale_setup(di);
            u16 wh = rd16(es, DSW(DS_copy_hdr));
            if (wh == DSW(DS_last_wh)) {                             /* same (W, H): share the previous copy */
                di = save;
                wr16(es, bx, rd16(es, (u16)(bx - 2)));
            } else {
                DSW(DS_last_wh) = wh;
            }
            ah++;
            bx = (u16)(bx + 2);
        } while (ah < DSB(DS_sprite_scale_count));
    } else if (mode == 1) {
        DSB(DS_spr_grow) = 0;
        DSB(DS_spr_scale) = 0x17;
        u16 bx = di;
        di = (u16)(di + cx);
        cx = (u16)((cx & 0xFF00) | DSB(DS_sprite_scale_count));    /* mov cl,[95BF] (CH kept) */
        do {                                                         /* loop: CX = 0 runs 65536 times */
            wr16(es, bx, di);
            bx = (u16)(bx + 2);
        } while (--cx != 0);
        di = sprite_scale_setup(di);
    }
    return di;                                                       /* mode >= 2: unchanged */
}

/* 0e12:289a sprite_scale_setup — render3d.md §4.13 */
u16 sprite_scale_setup(u16 di)
{
    u16 es = DSW(DS_sprite_cache_seg);
    sprite_scale_patterns();
    u16 si = (u16)(DSB(DS_spr_num) << 3);
    u16 ax = DSW((u16)(0x2500 + si));                                /* w | h << 8 */
    DSB(DS_spr_w) = (u8)ax;
    DSB(DS_spr_rows) = (u8)(ax >> 8);
    ax >>= 8;
    wr8(es, di, (u8)ax);
    di++;
    DSW(DS_copy_hdr) = di;
    di = (u16)(di + 2);
    DSW(DS_rep_ptr) = di;
    di = (u16)(di + ax);
    u16 cx = (u16)(DSW((u16)(0x2504 + si)) + 0x2500);                /* row lengths */
    si = (u16)(DSW((u16)(0x2502 + si)) + 0x2500);                    /* row pointers */
    if (DSB(DS_spr_grow) >= 1) return sprite_scale_rows_up(si, cx, di);
    return sprite_scale_rows(si, cx, di);
}

/* 0e12:2eb6 sprite_scale_patterns — render3d.md §4.13 */
void sprite_scale_patterns(void)
{
    u8 ah = DSB(DS_spr_scale);
    if (ah >= 0x18) {
        ah = (u8)(ah - 0x18);
        DSB(DS_spr_grow)++;
        if (ah >= 0x18) {
            DSB(DS_spr_grow)++;
            ah = (u8)(ah - 0x18);
        }
    }
    u16 bx = 0;
    if (ah < 0x18) {
        ah = (u8)(0x17 - ah);                                        /* neg ah; add ah,17h */
        bx = (u16)((u8)(ah * 5) << 1);
    }
    bx = (u16)(bx + 0x267E);
    /* The word stores fill B9EF..B9F5, B9F6..B9FC, B9FD..BA03 with entry bytes 0-6 and BA04..BA08 with bytes
     * 0-4 (BA09 not written). */
    for (u16 i = 0; i < 7; i++) {
        u8 e = r3_cs8((u16)(bx + i));
        DSB(DS_hmask + i) = e;
        DSB(DS_hmask + 7 + i) = e;
        DSB(DS_hmask + 14 + i) = e;
        if (i < 5) DSB(DS_hmask + 21 + i) = e;
    }
    bx = (u16)(bx + 6);                                              /* last word loaded: entry bytes 6, 7 */
    if (ah >= 0x18) {                                                /* only if B9D6 >= 48h: unreachable */
        ah = (u8)(ah - 0x18);
        DSB(DS_spr_grow) = 3;
        bx = 0;
        if (ah < 0x18) {
            ah = (u8)(0x17 - ah);
            bx = (u16)((u8)(ah * 5) << 1);
        }
        bx = (u16)(bx + 0x2684);
    }
    DSB(DS_vmask)     = r3_cs8((u16)(bx + 1));                       /* ch of the word at bx */
    DSB(DS_vmask + 1) = r3_cs8((u16)(bx + 2));
    DSB(DS_vmask + 2) = r3_cs8((u16)(bx + 3));
}

/* 0e12:2d54 sprite_scale_rows — render3d.md §4.13 (shrink; the initial "mov al,[cx]" is dead) */
u16 sprite_scale_rows(u16 ptrs_si, u16 lens_cx, u16 di)
{
    u16 es = DSW(DS_sprite_cache_seg);
    u16 si = ptrs_si, cx = lens_cx, dx = 0;
    DSB(DS_rows_out) = 0;
    for (;;) {
        if (vmask_next()) {
            DSB(DS_rows_out)++;
            u16 bx = (u16)(DSW(si) + 0x2500);
            wr8(es, DSW(DS_rep_ptr), 1);
            u16 start = di;
            di = sprite_scale_row(bx, DSB(cx), di);
            DSW(DS_rep_ptr)++;
            dx = (u16)(di - start);
        }
        cx = (u16)(cx + 2);
        si = (u16)(si + 2);
        if (--DSB(DS_spr_rows) == 0) break;
        /* PORT: faithful — when the next row would not fit, DI is returned still advanced by the last width
         * (the spec pseudocode returns it unadvanced); 2789 then sees DI >= D000h (fatal unless exactly D000h). */
        di = (u16)(di + dx);
        if (di >= DSW(DS_cache_limit)) break;
        di = (u16)(di - dx);
    }
    u16 hdr = DSW(DS_copy_hdr);
    wr8(es, hdr, (u8)dx);
    wr8(es, (u16)(hdr + 1), DSB(DS_rows_out));
    return di;
}

/* 0e12:2dc6 sprite_scale_rows_up — render3d.md §4.13 (grow) */
u16 sprite_scale_rows_up(u16 ptrs_si, u16 lens_cx, u16 di)
{
    u16 es = DSW(DS_sprite_cache_seg);
    u16 si = ptrs_si, cx = lens_cx, dx = 0;
    DSB(DS_rows_out) = 0;
    for (;;) {
        u16 start = di;
        DSB(DS_rows_out) = (u8)(DSB(DS_rows_out) + DSB(DS_spr_grow));
        di = sprite_scale_row_up((u16)(DSW(si) + 0x2500), DSB(cx), di);
        dx = (u16)(di - start);
        u16 rp = DSW(DS_rep_ptr);
        wr8(es, rp, DSB(DS_spr_grow));
        if (vmask_next()) {
            DSB(DS_rows_out)++;
            wr8(es, rp, (u8)(rd8(es, rp) + 1));
        }
        cx = (u16)(cx + 2);
        DSW(DS_rep_ptr)++;
        si = (u16)(si + 2);
        if (--DSB(DS_spr_rows) == 0) break;
        di = (u16)(di + dx);                                         /* PORT: DI stays advanced on exit, as 2d54 */
        if (di >= DSW(DS_cache_limit)) break;
        di = (u16)(di - dx);
    }
    u16 hdr = DSW(DS_copy_hdr);
    wr8(es, hdr, (u8)dx);
    wr8(es, (u16)(hdr + 1), DSB(DS_rows_out));
    return di;
}

/* 0e12:2f84 sprite_scale_row — render3d.md §4.13 (shrink: a pad/pixel is stored where the mask bit is 1) */
u16 sprite_scale_row(u16 src_bx, u8 len_cl, u16 di)
{
    u16 es = DSW(DS_sprite_cache_seg);
    DSB(DS_row_m) = len_cl & 0x3F;
    u16 si = src_bx, bx = 0, cx;
    u8 dl = 8;
    DSB(DS_pad) = (u8)((u8)(DSB(DS_spr_w) - DSB(DS_row_m)) >> 1);

    cx = DSB(DS_pad);
    if (cx != 0) {
        do {
            if (hmask_rol(bx)) wr8(es, di++, 0);
            if (--dl == 0) { bx++; dl = 8; }
        } while (--cx != 0);
    }
    cx = DSB(DS_row_m);
    /* TODO(verify): a row length of 0 (len & 3Fh) runs this LOOP 65536 times as in the original; the shipped
     * sprite files never have one. */
    do {
        bool c = hmask_rol(bx);
        u8 p = DSB(si);
        si++;
        if (c) wr8(es, di++, p);
        if (--dl == 0) { bx++; dl = 8; }
    } while (--cx != 0);
    cx = DSB(DS_pad);
    if (cx != 0) {
        do {
            if (hmask_rol(bx)) wr8(es, di++, 0);
            if (--dl == 0) { bx++; dl = 8; }
        } while (--cx != 0);
    }
    cx = dl;                                                         /* complete the byte: mask restored */
    do { hmask_rol(bx); } while (--cx != 0);
    return di;
}

/* 0e12:3004 sprite_scale_row_up — render3d.md §4.13 (grow: every pad/pixel once, again where the bit is 1) */
u16 sprite_scale_row_up(u16 src_bx, u8 len_cl, u16 di)
{
    u16 es = DSW(DS_sprite_cache_seg);
    DSB(DS_row_m) = len_cl & 0x3F;
    u16 si = src_bx, bx = 0, cx;
    u8 dl = 8;
    DSB(DS_pad) = (u8)((u8)(DSB(DS_spr_w) - DSB(DS_row_m)) >> 1);

    cx = DSB(DS_pad);
    if (cx != 0) {
        do {
            bool c = hmask_rol(bx);
            wr8(es, di++, 0);
            if (c) wr8(es, di++, 0);
            if (--dl == 0) { bx++; dl = 8; }
        } while (--cx != 0);
    }
    cx = DSB(DS_row_m);
    do {                                                             /* TODO(verify): as 2f84 for length 0 */
        bool c = hmask_rol(bx);
        u8 p = DSB(si);
        si++;
        wr8(es, di++, p);
        if (c) wr8(es, di++, p);
        if (--dl == 0) { bx++; dl = 8; }
    } while (--cx != 0);
    cx = DSB(DS_pad);
    if (cx != 0) {
        do {
            bool c = hmask_rol(bx);
            wr8(es, di++, 0);
            if (c) wr8(es, di++, 0);
            if (--dl == 0) { bx++; dl = 8; }
        } while (--cx != 0);
    }
    cx = dl;
    do { hmask_rol(bx); } while (--cx != 0);
    return di;
}

/* ============================================================================================ per frame */

/* 0e12:3085 sprite_list_build — render3d.md §4.13 */
void sprite_list_build(void)
{
    DSW(DS_spr_inst) = 0;
    u16 si = 2;
    for (u16 n = 4; n != 0; n--) {                                   /* instances 0..8 always listed */
        DSW(DS_spr_inst + si) = si; si = (u16)(si + 2);
        DSW(DS_spr_inst + si) = si; si = (u16)(si + 2);
    }
    u16 di = 0x10;
    const u8 bl = 0xFC, bh = 8;
    u8 dh = (u8)((DSB(DS_sprite_x + 1) & bl) - 4);                   /* car cell column - 1 */
    u8 dl = (u8)((DSB(DS_sprite_z + 1) & bl) - 4);                   /* car cell row - 1 */
    DSB(DS_place_nverts) = 8;
    u16 cx = (u16)(DSW(DS_sprite_count) << 1);
    for (di = (u16)(di + 2); di < cx; di = (u16)(di + 2)) {
        if (DSB(DS_sprite_id + di) == 0) continue;
        u8 al = (u8)((DSB(DS_sprite_x + 1 + di) & bl) - dh);
        if (al > bh) continue;
        u8 ah = (u8)((DSB(DS_sprite_z + 1 + di) & bl) - dl);
        if (ah > DSB(DS_place_nverts)) continue;
        DSW(DS_spr_inst + si) = di;
        si = (u16)(si + 2);
        if (si >= 0x130) goto done;
    }
    cx = DSB(DS_sprite_children);                                    /* tile children of this rebuild */
    if (cx != 0) {
        di = (u16)(DSW(DS_sprite_count) << 1);
        do {
            if (DSB(DS_sprite_id + di) != 0) {
                DSW(DS_spr_inst + si) = di;
                si = (u16)(si + 2);
                if (si >= 0x130) goto done;
            }
            di = (u16)(di + 2);
        } while (--cx != 0);
    }
done:
    DSW(DS_sprite_vis_count) = (u16)(si >> 1);
}

/* 0e12:316c sprite_project — render3d.md §4.13 */
void sprite_project(void)
{
    u16 di = (u16)(DSW(DS_sprite_vis_count) << 1);
    for (u16 bx = 0; bx != di; bx = (u16)(bx + 2)) {
        u16 si = DSW(DS_spr_inst + bx);
        u16 bp = 0;
        u16 id = DSW(DS_sprite_id + si);
        if ((u8)id == 0) {
            DSW(DS_spr_key + bx) = 0xFFFF;                           /* 8A32 / 8C92 left stale */
            continue;
        }
        DSB(DS_spr_num) = (u8)id;
        u16 cx = (u16)((u16)(DSW(DS_sprite_x + si) << 2) - DSW(DS_cam_x4));
        if (r3_neg(cx)) { cx = (u16)-cx; bp = 6; }
        u16 dx = (u16)((u16)(DSW(DS_sprite_z + si) << 2) - DSW(DS_cam_z4));
        if (r3_neg(dx)) { dx = (u16)-dx; bp ^= 2; }
        u16 mx, mn;                                                  /* after the xchg pair: CX = max, DX = min */
        if (cx < dx) { mx = dx; mn = cx; }
        else         { bp ^= 8; mx = cx; mn = dx; }
        u16 ax = 0x1FF;
        if (mn != mx) {
            ax = 0;
            if (mx != 0) {                                           /* mn < mx: no overflow */
                u16 q = (u16)(((u32)mn << 16) / mx);
                ax = (u16)(q >> 7);                                  /* rol ax,1; xchg ah,al; and ah,1 */
            }
        }
        u16 t = (u16)((ax << 1) + 0x12F6);
        DSW(DS_spr_angle + bx) = (u16)(DSW(DS_oct_base + bp) + r3_cs16((u16)(DSW(DS_oct_half + bp) + t))
                                       - DSW(DS_cam_heading));
        u16 cosv = r3_cs16((u16)((r3_cs16(t) >> 4) + 0x10F6));
        u16 dist = div32_16((u32)mx << 16, cosv, NULL);              /* mx / cos(atan) */
        u16 dy = (u16)(DSW(DS_sprite_y + si) - DSW(DS_cam_y_949E));
        if (r3_neg(dy)) dy = (u16)-dy;
        DSW(DS_spr_key + bx) = (u16)(dy + dist);
        if (DSB(DS_spr_num) > 8 && dist < DSW(DS_sprite_min_proj_dist)) dist = DSW(DS_sprite_min_proj_dist);
        DSW(DS_spr_dist + bx) = dist;
    }
}

/* 0e12:312e sprite_sort — render3d.md §4.13 */
void sprite_sort(void)
{
    if (DSW(DS_sprite_vis_count) == 0) return;
    if (DSB(DS_world_rebuilt) != 0) sprite_sort_quick();
    else sprite_sort_bubble();
}

/* 0e12:3147 sprite_sort_bubble — render3d.md §4.13 (entry 0, the car, excluded unless the external camera) */
void sprite_sort_bubble(void)
{
    u16 cx = (u16)((u16)((u16)(DSW(DS_sprite_vis_count) - 1) << 1) + DS_spr_key);
    u16 si = (DSB(DS_ext_view) == 0) ? 2 : 0;
    sprite_bubble_sort((u16)(si + DS_spr_key), cx);
}

/* 0e12:78a7 sprite_sort_quick — render3d.md §4.13 */
void sprite_sort_quick(void)
{
    u16 cx = (u16)((u16)((u16)(DSW(DS_sprite_vis_count) - 1) << 1) + DS_spr_key);
    u16 si = (DSB(DS_ext_view) == 0) ? 2 : 0;
    sprite_qsort((u16)(si + DS_spr_key), cx);
}

/* 0e12:794b sprite_rec_swap — render3d.md §4.13 (key, dist, inst, angle; SI/DI restored) */
void sprite_rec_swap(u16 si, u16 di)
{
    for (u16 i = 0; i < 4; i++) {
        u16 a = (u16)(si + i * SPR_STRIDE), b = (u16)(di + i * SPR_STRIDE);
        u16 t = DSW(a);
        DSW(a) = DSW(b);
        DSW(b) = t;
    }
}

/* Recursion tail of 78cc (78ef..7902 / 7909..791f): sort [a, b] if non-empty. */
static void sprite_qsort_sub(u16 a, u16 b)
{
    if ((s16)b <= (s16)a) return;                                    /* sub di,si; jle */
    if ((u16)(b - a) > 0x28) sprite_qsort(a, b);                     /* > 20 elements */
    else sprite_bubble_sort(a, b);
}

/* 0e12:78cc sprite_qsort — render3d.md §4.13 (descending, pivot = first element) */
void sprite_qsort(u16 lo_ptr, u16 hi_ptr)
{
    u16 si = lo_ptr, di = hi_ptr;
    u16 dx = DSW(si);                                                /* pivot key */
    si = (u16)(si + 2);
scan_i:                                                              /* 78d9 */
    for (;;) {
        if (dx > DSW(si)) goto scan_j;
        si = (u16)(si + 2);
        if (si > di) break;
    }
    si = lo_ptr;                                                     /* p = di */
    if (si != di) sprite_rec_swap(si, di);
    goto rec;
scan_j:                                                              /* 7924 */
    for (;;) {
        if (dx < DSW(di)) {                                          /* 793e */
            sprite_rec_swap(si, di);
            si = (u16)(si + 2);
            if (di <= si) goto meet;
            di = (u16)(di - 2);
            goto scan_i;
        }
        di = (u16)(di - 2);
        if (di < si) goto meet;
    }
meet:                                                                /* 792e: p = si - 1 */
    di = (u16)(si - 2);
    si = lo_ptr;
    if (si == di) {                                                  /* 7907 */
        sprite_qsort_sub((u16)(si + 2), hi_ptr);
        return;
    }
    sprite_rec_swap(si, di);
rec:                                                                 /* 78ed */
    di = (u16)(di - 2);
    sprite_qsort_sub(si, di);                                        /* [lo, p-1] */
    sprite_qsort_sub((u16)(di + 4), hi_ptr);                         /* [p+1, hi] */
}

/* 0e12:797b sprite_bubble_sort — render3d.md §4.13 (descending; next pass ends at the last swap) */
void sprite_bubble_sort(u16 first_ptr, u16 last_ptr)
{
    u16 cx = last_ptr, di = 0;
    for (;;) {                                                       /* 7987 */
        u16 si = first_ptr;
        u16 dx = DSW(si);
        si = (u16)(si + 2);
        bool swapped = false;
        while (si <= cx) {                                           /* unrolled 3x in the original */
            u16 ax = dx;
            dx = DSW(si);
            si = (u16)(si + 2);
            if (ax < dx) {                                           /* 79e7 */
                DSW((u16)(si - 2)) = ax;
                DSW((u16)(si - 4)) = dx;
                di = (u16)(si - 4);
                dx = ax;
                for (u16 i = 1; i < 4; i++) {                        /* dist, inst, angle */
                    u16 a = (u16)(di + i * SPR_STRIDE);
                    u16 t = DSW(a);
                    DSW(a) = DSW((u16)(a + 2));
                    DSW((u16)(a + 2)) = t;
                }
                swapped = true;
            }
        }
        if (!swapped) return;
        cx = di;
        if (!(cx > first_ptr)) return;
    }
}

/* 0e12:3352 sprite_animate — render3d.md §4.13 */
void sprite_animate(void)
{
    u16 si = (u16)(DSW(DS_sprite_vis_count) << 1);
    for (;;) {
        si = (u16)(si - 2);
        if (r3_neg(si)) return;
        u16 bx = DSW(DS_spr_inst + si);
        u16 ax = DSW(DS_sprite_id + bx);
        if (ax & 0xC0) {
            if (ax & 0x40) DSW(DS_sprite_z + bx) = (u16)(DSW(DS_sprite_z + bx) + DSW(DS_sprite_drift_z));
            if (ax & 0x80) DSW(DS_sprite_x + bx) = (u16)(DSW(DS_sprite_x + bx) + DSW(DS_sprite_drift_x));
        }
        u16 cx = ax;
        u8 ah = (u8)(ax >> 8) & 0x1F;
        if (ah != 0) {                                               /* animation countdown */
            DSB(DS_sprite_id + 1 + bx)--;
            if (--ah != 0) continue;
            u16 n = DSW((u16)(DS_sprite_anim_next + ((ax & 0x3F) << 1)));
            DSW(DS_sprite_id + bx) = (u16)(n | (cx & 0xE0C0));      /* keep class + drift bits */
            continue;
        }
        u8 al = (u8)ax;                                              /* crash debris ids 6..8 */
        if (al < 6 || al > 8) continue;
        u16 j = (u16)(al - 6);
        u8 f = (u8)(DSB(DS_wreck_timer) - 9);
        if (f & 0x80) continue;
        u16 t = (u8)(0x0B - f);                                      /* neg al; add al,0Bh */
        DSW(DS_sprite_x + bx) = (u16)(DSW(DS_sprite_x + bx) + (u16)(s16)(s8)DSB(DS_debris_vx + j));
        DSW(DS_sprite_z + bx) = (u16)(DSW(DS_sprite_z + bx) + (u16)(s16)(s8)DSB(DS_debris_vz + j));
        j = (u16)(j + 3 * t);
        DSW(DS_sprite_y + bx) = (u16)(DSB((u16)(DS_debris_h + j)) + DSW(DS_sprite_y) - DSW(DS_car_eye_height));
    }
}

/* 0e12:2e49 sprite_pick_scale — render3d.md §4.13 (B9D6 = hi(atan(size/depth)), 8-px units) */
void sprite_pick_scale(u16 k2_bx, u16 key_dx)
{
    u16 dx = key_dx;
    if (DSB(DS_spr_view) & 0x80) dx = (u16)(dx << 1);                /* mirror: half size */
    u16 bx = DSW(DS_spr_inst + k2_bx);
    u8 s = DSB(DS_sprite_id + bx) & 0x3F;
    DSB(DS_spr_num) = s;
    u16 cx = (u16)((DSB((u16)(DS_sprite_kind + s)) & 0xF8) << 3);   /* world size */
    u16 ax = 0x1FF;
    DSB(DS_swap_flag) = 0;
    if (cx != dx) {
        u16 num, den;
        if (cx < dx) { num = cx; den = dx; }
        else         { DSB(DS_swap_flag) = 4; num = dx; den = cx; }
        if (den == 0) { DSB(DS_spr_scale) = 0; return; }            /* unreachable */
        u16 q = (u16)(((u32)num << 16) / den);                       /* num < den: no overflow */
        ax = (u16)(q >> 7);
    }
    ax = r3_cs16((u16)((ax << 1) + 0x12F6));
    if (DSB(DS_swap_flag) != 0) ax = (u16)((u16)-ax + 0x4000);       /* neg ax; add ah,40h */
    DSB(DS_spr_scale) = (u8)(ax >> 8);
}

/* 0e12:28eb sprite_draw — render3d.md §4.13 */
void sprite_draw(u16 k_bx)
{
    u16 es = DSW(DS_sprite_cache_seg);
    DSW(DS_seg2) = es;
    u16 si = DSB(DS_spr_num);
    u8 mode = DSB((u16)(DS_sprite_kind + si)) & 7;
    if (mode >= 2) {                                                 /* 2: id-1's copies, >= 3: id-2's */
        if (mode != 2) si--;
        si--;
    }
    si = rd16(es, (u16)(si << 1));
    u16 bl = DSB(DS_spr_scale);
    if (bl > DSB(DS_sprite_max_size)) bl = DSB(DS_sprite_max_size);
    bl = DSB((u16)(DS_size_to_step + bl));
    si = (u16)(si + (bl << 1));
    si = rd16(es, si);
    DSW(DS_copy_h) = rd8(es, si);
    si++;
    u16 ax = rd16(es, si);
    if ((u8)ax == 0 || (ax >> 8) == 0) return;
    DSB(DS_spr_w) = (u8)ax;
    DSB(DS_spr_rows) = (u8)(ax >> 8);
    DSB(DS_spr_h) = (u8)(ax >> 8);                                   /* low byte only; B9DC stays 0 */
    u16 dx = DSW(DS_spr_angle + (u16)(k_bx << 1));
    si = (u16)(si + 2);
    DSW(DS_rep_ptr) = si;
    si = (u16)(si + DSW(DS_copy_h));                                 /* first stored row */
    if (DSB(DS_spr_view) & 0x80) {
        dx = (u16)(dx - DSW(DS_mirror_base));
        dx = (u16)((s16)dx >> 6);
        DSW(DS_spr_x) = (u16)((u16)-dx + 0x58);
        sprite_blit_mirror(k_bx, si);
    } else {
        dx = (u16)(dx << 2);
        dx = (u16)(dx << 1 | dx >> 15);                              /* rol dx,1 */
        dx = (u16)(dx << 8 | dx >> 8);                               /* xchg dh,dl */
        DSW(DS_spr_x) = dx;
        sprite_blit_main(k_bx, si);
    }
}

/* Shared vertical projection of 2b74 / 2999 (2bbd..2c09 / 29e2..2a2e): bottom row angle from the height
 * difference and the (clamped) distance -> AX (table 1AFA + octant row). */
static u16 sprite_row_angle(u16 k2)
{
    u16 cx = DSW(DS_spr_dist + k2);
    u16 bp = DSW(DS_spr_inst + k2);
    u16 dx = DSW(DS_sprite_y + bp);
    bp = 2;
    dx = (u16)(dx - DSW(DS_cam_y_949E));
    if (r3_neg(dx)) { dx = (u16)-dx; bp = 0; }
    if (!(dx < cx)) { u16 t = dx; dx = cx; cx = t; bp ^= 8; }
    u16 ax = 0;
    if (cx != 0) ax = (u16)(((u32)dx << 9) / cx);                    /* num <= den: 0..200h, no overflow */
    u16 tb = (u16)(0x1AFA + DSW(DS_oct_half + bp) + (u16)(ax << 1));
    return (u16)(r3_cs16(tb) + DSW(DS_oct_y + bp));
}

/* 0e12:2b74 sprite_blit_main — render3d.md §4.13 (view V) */
void sprite_blit_main(u16 k_bx, u16 si)
{
    u16 es = DSW(DS_sprite_cache_seg);                               /* ES from 28eb */
    DSW(DS_clip_left) = 0;
    u16 cx = DSB(DS_spr_w);
    u16 ax = (u16)(DSW(DS_spr_x) - ((u8)cx >> 1));
    DSW(DS_spr_x) = ax;
    DSB(DS_spr_vis_w) = (u8)cx;
    if (ax < r3_cs16(0x2B94) /* PORT: patched immediate (view width px) */) {
        ax = (u16)(r3_cs16(0x2B9B) /* PORT: patched immediate (view width px) */ - ax);
        if (ax < cx) DSB(DS_spr_vis_w) = (u8)ax;
    } else {
        ax = (u16)((ax | 0xFE00) + cx);                              /* assumes x in -256..-1 */
        if (r3_neg(ax) || ax == 0) return;
        DSB(DS_spr_vis_w) = (u8)ax;
        DSW(DS_clip_left) = (u8)(cx - (u8)ax);                       /* sub cl,al; CH = 0 */
    }

    u16 k2 = (u16)(k_bx << 1);
    ax = sprite_row_angle(k2);
    u16 dx = 0;
    s8 roll = DSC(DS_cam_roll_94A0);
    if (roll != 0) {
        u8 cl = (u8)roll;
        if (roll < 0) cl = (u8)-cl;
        dx = (u16)(DSW(DS_spr_angle + k2) - DSW(DS_view_cx32));
        dx = (u16)((s16)dx >> (cl & 31));                            /* PORT: 286+ shift count mask */
        if (roll > 0) dx = (u16)-dx;
    }
    ax = (u16)(ax + 1 + dx - DSW(DS_spr_h) + DSW(DS_cam_row));      /* top row = bottom - H + 1 */
    if ((s16)ax < DSS(DS_top_row)) DSW(DS_top_row) = ax;
    if ((s16)ax >= DSS(DS_view_rows)) return;

    u16 di = DSW(DS_rep_ptr);                                        /* top clip */
    u8 dh = rd8(es, di);
    cx = DSB(DS_spr_w);
    while (r3_neg(ax)) {
        if (--dh == 0) {
            di++;
            dh = rd8(es, di);
            si = (u16)(si + cx);
        }
        if (--DSB(DS_spr_rows) == 0) return;
        ax++;
    }
    DSB(DS_rep_count) = dh;
    DSW(DS_rep_ptr) = di;
    sprite_rows_main((u8)ax, si);
}

/* 0e12:2999 sprite_blit_mirror — render3d.md §4.13 (mirror M: 88 wide, 19 rows, half vertical scale, roll
 * term around the fixed centre 9400h, pitch 948F instead of 9496, no BAD4 update) */
void sprite_blit_mirror(u16 k_bx, u16 si)
{
    u16 es = DSW(DS_sprite_cache_seg);
    DSW(DS_clip_left) = 0;
    u16 cx = DSB(DS_spr_w);
    u16 ax = (u16)(DSW(DS_spr_x) - ((u8)cx >> 1));
    DSW(DS_spr_x) = ax;
    DSB(DS_spr_vis_w) = (u8)cx;
    if (ax < 0x58) {
        ax = (u16)(0x58 - ax);
        if (ax < cx) DSB(DS_spr_vis_w) = (u8)ax;
    } else {
        ax = (u16)((ax | 0xFE00) + cx);
        if (r3_neg(ax) || ax == 0) return;
        DSB(DS_spr_vis_w) = (u8)ax;
        DSW(DS_clip_left) = (u8)(cx - (u8)ax);
    }

    u16 k2 = (u16)(k_bx << 1);
    ax = sprite_row_angle(k2);
    u16 dx = 0;
    s8 roll = DSC(DS_cam_roll_94A0);
    if (roll != 0) {
        u8 cl = (u8)roll;
        if (roll < 0) cl = (u8)-cl;
        dx = (u16)(DSW(DS_spr_angle + k2) - 0x9400);
        dx = (u16)((s16)dx >> 1);
        dx = (u16)((s16)dx >> (cl & 31));                            /* PORT: 286+ shift count mask */
        if (roll > 0) dx = (u16)-dx;
    }
    ax = (u16)(ax + 1 - dx - DSW(DS_spr_h) - DSW(DS_pitch));
    ax = (u16)(((s16)ax >> 1) + 7);
    if ((s16)ax >= 0x13) return;

    u16 di = DSW(DS_rep_ptr);
    u8 dh = rd8(es, di);
    cx = DSB(DS_spr_w);
    while (r3_neg(ax)) {
        if (--dh == 0) {
            di++;
            dh = rd8(es, di);
            si = (u16)(si + cx);
        }
        if (--DSB(DS_spr_rows) == 0) return;
        ax++;
    }
    DSB(DS_rep_count) = dh;
    DSW(DS_rep_ptr) = di;
    sprite_rows_mirror((u8)ax, si);
}

/* Row copy shared by 2c7c / 2a9e: `vis` bytes, colour 0 transparent (the original's unrolled word copy with
 * per-byte zero tests writes exactly the non-zero bytes). */
static void sprite_rows_copy(u16 dseg, u16 di, u16 si, u16 end_bp, u16 stride)
{
    u8 dl = DSB(DS_spr_vis_w), dh = DSB(DS_rep_count);
    u16 cseg = DSW(DS_seg2);
    for (;;) {
        for (u16 c = 0; c < dl; c++) {
            u8 p = rd8(cseg, (u16)(si + c));
            if (p != 0) wr8(dseg, (u16)(di + c), p);
        }
        di = (u16)(di + stride);
        if (--dh == 0) {
            si = (u16)(si + DSB(DS_spr_w));
            DSW(DS_rep_ptr)++;
            dh = rd8(cseg, DSW(DS_rep_ptr));
        }
        if (di >= end_bp) return;
        if (--DSB(DS_spr_rows) == 0) return;
    }
}

/* 0e12:2c7c sprite_rows_main — render3d.md §4.13 (seg 90D0, stride 140h, end BA93) */
void sprite_rows_main(u8 y_al, u16 si)
{
    u16 bp = DSW(DS_view_bytes);
    u16 di = DSW((u16)(DS_row_ofs + (y_al << 1)));
    u16 bx = DSW(DS_spr_x);
    if (DSW(DS_clip_left) != 0) {
        bx = 0;
        si = (u16)(si + DSW(DS_clip_left));
    }
    di = (u16)(di + bx);
    if (di >= bp) return;
    sprite_rows_copy(DSW(DS_viewbuf_seg), di, si, bp, 0x140);
}

/* 0e12:2a9e sprite_rows_mirror — render3d.md §4.13 (seg 90D2, stride 58h, end 688h) */
void sprite_rows_mirror(u8 y_al, u16 si)
{
    u16 bp = 0x688;
    u16 di = DSW((u16)(DS_mrow_ofs + (y_al << 1)));
    u16 bx = DSW(DS_spr_x);
    if (DSW(DS_clip_left) != 0) {
        bx = 0;
        si = (u16)(si + DSW(DS_clip_left));
    }
    di = (u16)(di + bx);
    if (di >= bp) return;
    sprite_rows_copy(DSW(DS_mirrorbuf_seg), di, si, bp, 0x58);
}

/* ============================================================================================ merge loop */

/* 0e12:323e draw_faces_and_sprites — render3d.md §4.13 / §4.9 */
void draw_faces_and_sprites(void)
{
    if (DSB(DS_world_rebuilt) != 0) sprite_list_build();
    sprite_project();
    sprite_sort();
    sprite_animate();
    DSB(DS_world_rebuilt) = 0;
    sprite_collisions();

    u16 f = DSW(DS_face_count);                                      /* SI */
    u16 k = DSW(DS_sprite_vis_first);                                /* BX */
    while (k < DSW(DS_sprite_vis_count) || f != 0) {
        u16 es = DSW(DS_face_block + 2);
        u16 k2 = (u16)(k << 1);
        u8 cl = DSB(DS_sprite_id + DSW(DS_spr_inst + k2));           /* read even when k >= 94AE */
        u16 key = DSW(DS_spr_key + k2);
        bool face = k >= DSW(DS_sprite_vis_count);
        if (!face) {
            if (cl == 0) { k++; continue; }
            u16 fo = (u16)(f << 1);
            if (fo != 0 && key <= rd16(es, (u16)(fo + DSW(DS_key_ofs) - 2))) face = true;   /* face farther or equal */
        }
        if (!face) {
            u8 v = (u8)(DSB(DS_spr_angle + 1 + k2) + 8);
            DSB(DS_spr_view) = v;
            bool draw = true;
            if (v >= 0x40) {                                         /* mirror: hi(angle) 84h..A3h, mirror on */
                if ((u8)(v + 0x74) >= 0x20 || DSB(DS_mirror_on) == 0) draw = false;
            }
            if (draw) {
                u16 dx = DSW(DS_spr_key + k2);
                if (dx <= 0x10) draw = false;
                else if (cl > 5) { if (dx >= DSW(DS__4) /* B6E2 sprite_far_limit */) draw = false; }
                else if (cl <= 3 && (cl & 1)) { if (dx >= 0x980) draw = false; }
                if (draw) {
                    sprite_pick_scale(k2, dx);
                    sprite_draw(k);
                }
            }
            k++;
            continue;
        }

        /* face (3a7c): 32fa..3343 */
        f--;
        u16 bp = rd16(es, (u16)((u16)(f << 1) + DSW(DS_order_ofs)));
        DSW(DS_face_ptr) = bp;
        u16 dx = rd16(es, bp);                                       /* w0 */
        u16 ax = rd16(es, (u16)(bp + 8));                            /* colour pair */
        u16 cx = rd16(es, (u16)(bp + 6));                            /* w3 */
        DSB(DS_face_type) = (u8)(cx >> 8) & 0xF8;
        u16 si = (u16)(rd16(es, (u16)(bp + 2)) << 1);                /* 2 * w1 */
        u16 bx = (u16)((dx & 0x7FF) << 1);                           /* 2 * v0 */
        dx &= 0xC0FF;                                                /* and dh,0C0h */
        if (ax == 0x10F && (DSW(DS_frame_counter) & 1)) ax = (u16)(ax << 8 | ax >> 8);   /* blinking pair */
        DSW(DS_colour) = ax;
        draw_face_dispatch(bx, si, dx, far_make(es, (u16)(bp + 2)));
    }
}
