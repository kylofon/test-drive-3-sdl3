/* Simulation module: sprite and near-face collisions, crash start, the camera position (segment 0e12,
 * simulation.md §4.9, §4.10, §4.11 end; render3d.md §4.12, §4.14 for the same functions seen from the
 * drawing side).
 *
 * Face records live in the far face segment (DS:E5BA; far DS:E5B8 = first record, 10 bytes each), the sort
 * keys at DS:CC8E in that segment with the face pointers 0C80h bytes below them. Object arrays are u16[160]
 * at DS:A479.. indexed by the slot bx = 2*i. */
#include "game/game.h"

#define OW(arr, bx) DSW((u16)((arr) + (bx)))
#define OB(arr, bx) DSB((u16)((arr) + (bx)))

static void knockover_hit(u8 type_ch, FarPtr sortptr_es_di);
static void polar_small(u8 angle_bl, u8 dist_dl, u16 *cx, u16 *dx);

/* 0e12:33f7 sprite_collisions — simulation.md §4.9 (near -> far through the visible sprite list) */
void sprite_collisions(void)
{
    u16 si;

    if (DSB(DS_ext_view) != 0) return;
    si = (u16)(DSW(DS_sprite_vis_count) << 1);
    while ((s16)(si = (u16)(si - 2)) >= 0) {
        u16 bx, ax;
        u8 ah, al;
        if (DSW((u16)(DS_spr_key + si)) >= DSW(DS_sprite_hit_dist)) return;
        bx = DSW((u16)(DS_spr_inst + si));
        ax = OW(DS_sprite_id, bx);
        ah = (u8)((ax >> 8) & 0xE0);                            /* collision class */
        al = (u8)ax;
        if (ah == 0) continue;
        if (ah == 0x60) {                                       /* trees, rocks, crossing barrier */
            u16 cx = DSW(DS_car_speed);
            if ((s16)cx < 0) cx = (u16)-cx;
            if (cx < 0x14) { bounce_back(); return; }
            if (al == 0x10 && DSB(DS_gate_pos) < 0x30) return;  /* barrier sprite while the gate is up */
            DSB(DS_crash_flag) = 1;
            return;
        }
        if (ah < 0x60) {                                        /* 20h / 40h: knock over */
            DSB(DS_bump_frames) = 1;
            sfx_play_ax(3);
            DSB(DS_shake) = 5;
            OW(DS_sprite_id, bx) |= 1;
            if (ah <= 0x20) OW(DS_sprite_id, bx) = 0x001F;
            else random_damage();                               /* 40h keeps its class */
            continue;
        }
        if (ah < 0xA0) {                                        /* 80h: windscreen splat */
            u8 dl = 0;
            OW(DS_sprite_id, bx) = 0;
            if (DSB(DS_rand_hi) & 8) dl = 0xC0;
            windscreen_splat(DSW(DS_rand_lo), dl);              /* 47fc */
            continue;
        }
        if (ah == 0xA0) {                                       /* "Go back to the main road!" */
            OW(DS_sprite_id, bx) = 0;
            DSW(DS_pending_msg) = 0x0D;
            continue;
        }
        if (ah < 0xE0) {                                        /* C0h: "WRONG WAY!" */
            OW(DS_sprite_id, bx) = 0;
            DSW(DS_pending_msg) = 0x18;
            continue;
        }
        OW(DS_sprite_id, bx) = 0;                               /* E0h */
        sfx_play_ax(0x0D);
    }
}

/* 0e12:34f1 near_face_hits — simulation.md §4.9, render3d.md §4.14 (sorted faces, nearest first) */
void near_face_hits(void)
{
    u16 depth = DSW(DS_near_depth);                             /* AX, kept on the stack */
    u16 es = DSW((u16)(DS_face_block + 2));                     /* DS:E5BA face segment */
    u16 si = DSW(DS_key_ofs);
    u16 end = (u16)((u16)(DSW(DS_face_count) << 1) + si);

    for (; si < end; si = (u16)(DSW(DS_vert_base) + 2)) {
        u16 di, bp, w0, cx;
        u8 ch, cl = 0;

        if (depth < rd16(es, si)) return;
        DSW(DS_vert_base) = si;                                 /* 945E: scratch for the key pointer */
        di = (u16)(si - 0xC80);                                 /* face pointer entry */
        bp = rd16(es, di);
        w0 = rd16(es, bp);
        bp = (u16)(bp + 6);
        cx = (u16)(rd16(es, bp) & 0xF800);
        ch = (u8)(cx >> 8);
        if (cx != 0) {
            ch = (u8)(ch >> 3);                                 /* face type */
            if (ch >= 0x18) {
                if (ch <= 0x1B) {
                    if (ch == 0x1A) plate_fold_down(far_make(es, bp));   /* 4590: ES:BP = &w3 */
                    else knockover_hit(ch, far_make(es, di));            /* 4631 keeps CX */
                }
            } else if (ch <= 2) {                               /* solid (1) / vehicle (2) */
                u8 n = (u8)(w0 >> 14);
                u16 lo = 0xFFFF, hi = 0, y0, ndy, bot, top;
                cl = ch;
                bp = (u16)(bp - 6);
                do {
                    u16 y = DSW((u16)(DS_vert_y + (u16)((rd16(es, bp) & 0x7FF) << 1)));
                    if (y > hi) hi = y;
                    if (y < lo) lo = y;
                    bp = (u16)(bp + 2);
                    n--;
                } while (!(n & 0x80));
                y0 = DSW(DS_sprite_y);                          /* eye height of the car */
                ndy = DSW(DS_near_dy);
                bot = (u16)(y0 - ndy);
                if ((s16)bot < 0) bot = 0;
                top = (u16)(y0 + ndy);
                if (lo > top || hi < bot) cl = 0;
            }
        }
        if (cl) {
            u16 spd = DSW(DS_car_speed);
            if ((s16)spd < 0) spd = (u16)-spd;
            if (ch == 1 && spd < 0x14) bounce_back();
            else {
                u16 rec;
                DSB(DS_crash_flag) = cl;
                DSB(DS_crash_type) = ch;
                rec = rd16(es, (u16)(DSW(DS_vert_base) - 0xC80));
                DSW(DS_crash_face) = div32_16((u16)(rec - DSW(DS_face_block)), 10, NULL);
            }
        }
    }
}

/* 0e12:4631 knockover_hit — simulation.md §4.9 (object_face_hit), render3d.md §4.14 (CH = type 18h/19h/1Bh,
 * ES:DI = &face pointer of the hit face) */
static void knockover_hit(u8 type_ch, FarPtr sortptr_es_di)
{
    u16 f = div32_16((u16)(rd16(sortptr_es_di.seg, sortptr_es_di.off) - DSW(DS_face_block)), 10, NULL);
    u16 bx = (u16)(DSW(DS_obj_count) << 1);                     /* A473, not A475 */

    while ((bx = (u16)(bx - 2)) != 0) {
        u16 cnt = OW(DS_obj_face_count, bx), first, snd;
        u8 ah;
        if (cnt == 0) continue;
        first = OW(DS_obj_face_base, bx);
        if (f < first) continue;
        if (f >= (u16)(first + cnt)) continue;
        ah = (u8)(OB(DS_obj_flags, bx) | 1);
        if (type_ch <= 0x19) ah = (type_ch == 0x19) ? 0x0B : 0x09;   /* knocked-down sign models */
        OB(DS_obj_flags, bx) = ah;
        DSW(DS_last_cell) = 0xFFFF;                             /* force a world rebuild next frame */
        ah &= 0x3F;
        snd = 5;
        if (ah == 0x39 && type_ch == 0x1B) {                    /* seagull takes off */
            OW(DS_obj_flags, bx) = 0x3039;
            OW(DS_obj_vel, bx) = 0;
            OW(DS_obj_waypoint, bx) = 0;
            snd = 0x12;
        }
        sfx_play_ax(snd);
        DSB(DS_shake) = 5;
        DSB(DS_bump_frames) = 5;
        random_damage();
        return;
    }
}

/* 0e12:0f31 crash_start — simulation.md §4.10 */
void crash_start(void)
{
    u16 dx, bx;

    if (DSB(DS_crashed) != 0) return;
    DSB(DS_crashed) = 1;
    music_stop(0);
    DSB(DS_shake) = 0x10;
    DSW(DS_crash_object) = 0;
    dx = DSW(DS_crash_face);
    bx = (u16)(DSW(DS_obj_a475) << 1);
    while ((bx = (u16)(bx - 2)) != 0) {
        u16 cnt = OW(DS_obj_face_count, bx), first;
        if (cnt == 0) continue;
        first = OW(DS_obj_face_base, bx);
        if (dx < first) continue;
        if (dx >= (u16)(first + cnt)) continue;
        DSW(DS_crash_object) = bx;
        return;
    }
}

/* 0e12:6e92 camera_pos_update — simulation.md §4.11, render3d.md §4.12 */
void camera_pos_update(void)
{
    u16 cx = 0, dx = 0, ax;
    u8 al, bl;

    if (DSB(DS_replay_state) == 0) {
        if (DSB(DS_ext_view) == 0) {
            DSW(DS_cam_y_94A7) = DSW(DS_sprite_y);
            DSW(DS_cam_pitch) = DSW(DS_pitch);
        }
        DSW(0x94B4) = DSW(DS_sprite_x);                         /* car_x_saved */
        DSW(0x94B6) = DSW(DS_sprite_z);                         /* car_z_saved */
    } else if (DSB(DS_replay_state) == 2) {
        u16 bx = DSW(DS_crash_object);
        if (bx != 0) {                                          /* sprite 7 on the object hit */
            DSW(DS_sprite_7_x) = OW(DS_obj_x, bx);
            DSW(DS_z_9F81) = OW(DS_obj_z, bx);
            DSW(DS_y_A201) = (u16)((OW(DS_obj_y8, bx) >> 3) + 0x30);
        }
    }
    if (DSB(DS_ext_view) != 0) {
        polar_small(DSB(DS_cam_yaw), DSB(DS_cam_elev) /* 94AC: orbit distance */, &cx, &dx);
        cx = (u16)(cx << 1);
        dx = (u16)(dx << 1);
    }
    cx = (u16)((u16)(cx + DSW(DS_sprite_x)) & 0x7FFF);
    dx = (u16)((u16)(dx + DSW(DS_sprite_z)) & 0x3FFF);
    DSW(DS_cam_x) = cx;
    DSW(DS_cam_z) = dx;

    ax = 0;                                                     /* sprite 8: none / sun (5) / moon (4) */
    if (DSB(DS_sky_alt) == 0) {
        ax = 5;
        if (DSB(DS_colour_mode) == 0) ax--;
    }
    DSW(DS_sprite_8_type) = ax;
    DSW(DS_x) = cx;                                             /* 9D03 sprite 8 x */
    DSW(DS_z_9F83) = dx;
    al = (u8)(DSB(0x95BD) & 7);                                 /* leg heading octant */
    if (al > 4) DSW(DS_x) = (u16)(DSW(DS_x) - 0x900);
    else if (al < 4 && al != 0) DSW(DS_x) = (u16)(DSW(DS_x) + 0x900);
    al = (u8)((al + 1) & 7);
    if (al < 3) DSW(DS_z_9F83) = (u16)(DSW(DS_z_9F83) + 0x900);
    else if (al > 3 && al < 7) DSW(DS_z_9F83) = (u16)(DSW(DS_z_9F83) - 0x900);
    DSW(DS_y_A203) = (u16)(DSW(DS_cam_y_94A7) + 0x3C0);

    /* camera X/Z * 4 with the top 2 fraction bits (shl bl / rcl) */
    bl = DSB(DS_pos_frac_x);
    cx = (u16)(cx << 2 | bl >> 6);
    bl = DSB(DS_pos_frac_z);
    dx = (u16)(dx << 2 | bl >> 6);
    DSW(DS_cam_x4) = cx;
    DSW(DS_cam_z4) = dx;
}

/* 0e12:6f9c polar_small — render3d.md §4.12 (BL = angle, DL = distance -> CX = dX, DX = dZ:
 * -2 * dist * sin/cos from the high bytes of the table 0e12:0FF2, rotated by the quadrant) */
static void polar_small(u8 bl, u8 dl, u16 *cx_out, u16 *dx_out)
{
    u8 q = 0, s;
    u16 bx, cx, dx;

    if (bl & 0x80) q = (u8)(q + 2);
    if (bl & 0x40) q = (u8)(q + 1);
    bl = (u8)(bl << 2);
    DSB(DS_swap_flag) = q;                                      /* 947D: quadrant */
    bx = bl;
    s = SEGB(0x0E12, (u16)(0x0FF2 + bx + 1));                   /* hi byte of sin_tab word at byte offset bx */
    cx = (u16)((u16)-(u16)(((u16)dl * s) >> 8) << 1);
    bx = (u16)(0x100 - bx);
    s = SEGB(0x0E12, (u16)(0x0FF2 + bx + 1));
    dx = (u16)((u16)-(u16)(((u16)dl * s) >> 8) << 1);
    if (q == 1) { u16 t = dx; dx = (u16)-cx; cx = t; }
    else if (q == 2) { cx = (u16)-cx; dx = (u16)-dx; }
    else if (q == 3) { u16 t = dx; dx = cx; cx = (u16)-t; }
    *cx_out = cx;
    *dx_out = dx;
}
