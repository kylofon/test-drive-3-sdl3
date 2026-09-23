/* Simulation module: replay recorder / player / wreck animation (segment 0ab4, simulation.md §4.11),
 * replay_start 0e12:4c51 and the per-life reset 0e12:2465 (§4.10, §4.14).
 *
 * The ring buffer of 800h words stays in the code segment 0ab4 at offset 020Fh (in mem[]); byte indices
 * DS:16D4 (position) and DS:16D6 (end of the last recorded frame) are masked with FFFh. */
#include "game/game.h"

#define RING_SEG   0x0AB4
#define RING_OFF   0x020F
#define RB(b)      SEGW(RING_SEG, (u16)(RING_OFF + (b)))

static u16 replay_play_frame(void);
static u16 replay_rec_objects(u16 bx);
static u16 replay_play_objects(u16 bx);
static void debris_sprites(u16 bx, u16 *ax, u16 *dx, u16 *cx);

/* stores one word (FFFFh is kept for the frame marker: stored as FFFEh) and advances BX */
static u16 rec_put(u16 bx, u16 v)
{
    if (v == 0xFFFF) v--;
    RB(bx) = v;
    return (u16)((bx + 2) & 0x0FFF);
}

/* 0ab4:120f replay_clear — simulation.md §4.11 */
void replay_clear(void)
{
    u16 i;
    for (i = 0; i < 0x800; i++) SEGW(RING_SEG, (u16)(RING_OFF + 2 * i)) = 0;
}

/* 0ab4:1220 replay_update — simulation.md §4.11 */
void replay_update(void)
{
    u16 bx = DSW(DS_replay_pos);
    u8 st = DSB(DS_replay_state);

    if (st == 0) {                                            /* record */
        RB(bx) = 0xFFFF;
        bx = (u16)((bx + 2) & 0x0FFF);
        bx = rec_put(bx, DSW(DS_sprite_x));
        bx = rec_put(bx, DSW(DS_sprite_z));
        bx = rec_put(bx, DSW(DS_sprite_y));
        bx = rec_put(bx, DSW(DS_view_heading));
        bx = rec_put(bx, DSW(DS_pitch));
        bx = replay_rec_objects(bx);
        DSW(DS_replay_pos) = bx;
        DSW(DS_replay_end) = bx;
        return;
    }
    DSB(DS_clock_running) = 0;
    if (DSB(DS_replay_paused)) return;

    if (st >= 2) {                                            /* wreck animation */
        if (--DSB(DS_wreck_timer) != 0) {
            u16 ax, dx, o;
            if (DSB(DS_crash_water) == 1) {
                DSW(DS_obj_y8) = (u16)((u16)(DSW(DS_sprite_y) - DSW(DS_car_eye_height)) << 3);
                return;
            }
            o = DSW(DS_crash_object);
            ax = DSW(DS_vel_x + 1);                           /* bytes 1253/1254: vel_x >> 8 */
            dx = DSW(DS_vel_z + 1);                           /* bytes 1257/1258 */
            if (o) {
                u8 m = (u8)(DSB(DS_obj_flags + o) & 0x3F);
                if (m < 0x12 || m > 0x14) {
                    DSW(DS_obj_x + o) += ax;
                    DSW(DS_obj_z + o) += dx;
                }
            }
            ax = (u16)(DSW(DS_sprite_x) - ax);
            DSW(DS_sprite_x) = ax; DSW(DS_obj_x) = ax;
            dx = (u16)(DSW(DS_sprite_z) - dx);
            DSW(DS_sprite_z) = dx; DSW(DS_obj_z) = dx;
            return;
        }
        if (DSB(DS_replay_request) == 0) {
            life_lost(0);
            if (DSB(DS_replay_request) == 0) return;
        }
        goto restart;
    }

    bx = replay_play_frame();
    if (bx != DSW(DS_replay_end)) return;
    if (DSB(DS_crashed)) {                                    /* crash replay finished */
        u16 ax, dx, cx, o;
        DSB(DS_replay_state) = 2;
        DSB(DS_wreck_timer) = 0x14;
        if (DSB(DS_crash_water) == 1) {
            ax = (u16)(DSW(DS_sprite_y) - 0x10);
            DSW(DS_sprite_y) = ax;
            DSW(DS_obj_y8) = (u16)((u16)(ax - DSW(DS_car_eye_height)) << 3);
            return;
        }
        DSW(DS_sprite_id) = 0x10B;                            /* smoke replaces the car */
        ax = DSW(DS_sprite_x);
        dx = DSW(DS_sprite_z);
        cx = (u16)(DSW(DS_sprite_y) - 0x10);
        DSW(DS_sprite_y) = cx;
        cx = (u16)(cx + 0x0C);
        debris_sprites(2, &ax, &dx, &cx);
        ax = (u16)(ax + (u16)(((DSW(DS_rand_lo) >> 1) & 0x7F) - 0x40));
        dx = (u16)(dx + (u16)((DSW(DS_rand_lo + 1) & 0x7F) - 0x40));   /* word at DS:00D3 */
        debris_sprites(8, &ax, &dx, &cx);
        o = DSW(DS_crash_object);
        if (o) {
            DSW(DS_sprite_x + 14) = DSW(DS_obj_x + o);        /* sprite 7 on the object hit */
            DSW(DS_sprite_z + 14) = DSW(DS_obj_z + o);
            DSW(DS_sprite_y + 14) = (u16)((DSW(DS_obj_y8 + o) >> 3) + 0x1C);
            DSW(DS_sprite_id + 14) = 0x10B;
        }
        return;
    }
    if (DSB(DS_replay_request) == 0) {
        DSB(DS_kbd_key) = 0;
        panel_erase(0);
        message_box(0x2B);                                    /* "Replay Completed" */
        if (DSB(DS_replay_request) == 0) {
            DSB(DS_replay_state) = 0;
            DSB(DS_ext_view) = 0;
            DSW(DS_race_state) = 2;
            return;
        }
    }
restart:
    replay_panel_draw();
    DSB(DS_replay_request) = 0;
    DSB(DS_replay_state) = 1;
    DSW(DS_replay_pos) = (u16)((DSW(DS_replay_end) + 2) & 0x0FFF);
    {
        int i;
        for (i = 0; i < 8; i++) DSW(DS_sprite_id + 2 * i) = 0;
    }
}

/* 0ab4:1460 debris_sprites — simulation.md §4.11 (BX = 2k, AX = X, DX = Z, CX = Y, all in/out) */
static void debris_sprites(u16 bx, u16 *ax, u16 *dx, u16 *cx)
{
    u16 di = bx, si;
    DSW(DS_sprite_id + di) = 8;
    bx = (u16)(bx + 2);
    si = bx;
    DSW(DS_sprite_id + si) = 7;
    bx = (u16)(bx + 2);
    DSW(DS_sprite_id + bx) = 6;
    *ax = (u16)(*ax + 8);  DSW(DS_sprite_x + bx) = *ax;
    *ax = (u16)(*ax - 6);  DSW(DS_sprite_x + si) = *ax;
    *ax = (u16)(*ax - 4);  DSW(DS_sprite_x + di) = *ax;
    DSW(DS_sprite_z + bx) = *dx;
    *dx = (u16)(*dx + 3);  DSW(DS_sprite_z + si) = *dx;
    *dx = (u16)(*dx - 7);  DSW(DS_sprite_z + di) = *dx;
    *cx = (u16)(*cx + 4);  DSW(DS_sprite_y + bx) = *cx;
    *cx = (u16)(*cx - 8);  DSW(DS_sprite_y + si) = *cx;
    *cx = (u16)(*cx + 0x14); DSW(DS_sprite_y + di) = *cx;
}

/* 0ab4:14b9 replay_play_frame — simulation.md §4.11 (returns BX = the new DS:16D4) */
static u16 replay_play_frame(void)
{
    u16 bx = DSW(DS_replay_pos), ax;
    u16 n = 0;
    do {                                                      /* skip to the next frame marker */
        ax = RB(bx);
        bx = (u16)((bx + 2) & 0x0FFF);
        /* PORT: the original spins forever when the ring holds no marker (cannot happen: every recorded
         * frame and replay_start write one); stop after one full turn instead of hanging. */
        if (++n > 0x800) return DSW(DS_replay_pos);
    } while (ax != 0xFFFF);

    ax = RB(bx); DSW(DS_sprite_x) = ax; DSW(DS_obj_x) = ax;
    bx = (u16)((bx + 2) & 0x0FFF);
    ax = RB(bx); DSW(DS_sprite_z) = ax; DSW(DS_obj_z) = ax;
    bx = (u16)((bx + 2) & 0x0FFF);
    ax = RB(bx); DSW(DS_sprite_y) = ax;
    DSW(DS_obj_y8) = (u16)((u16)(ax - DSW(DS_car_eye_height)) << 3);
    bx = (u16)((bx + 2) & 0x0FFF);
    ax = RB(bx); DSW(DS_view_heading) = ax;
    {
        u8 h = (u8)((ax >> 8) - 0x40);
        DSW(DS_obj_heading) = (u16)(h << 8 | h);
    }
    bx = (u16)((bx + 2) & 0x0FFF);
    ax = RB(bx); DSW(DS_pitch) = ax;
    bx = (u16)((bx + 2) & 0x0FFF);
    DSW(DS_obj_pitch) = (u16)(pitch_shear(ax) << 8);
    bx = replay_play_objects(bx);
    DSW(DS_replay_pos) = bx;
    return bx;
}

/* 0ab4:1544 replay_rec_objects — simulation.md §4.11 (BX = ring index in/out) */
static u16 replay_rec_objects(u16 bx)
{
    u16 si = (u16)(DSW(DS_obj_a475) << 1);
    for (;;) {
        si = (u16)(si - 2);
        if (si == 0) return bx;
        if (!(DSW(DS_obj_flags + si) & 0x2000) || DSW(DS_obj_face_count + si) == 0) continue;
        bx = rec_put(bx, (u16)((DSW(DS_obj_pitch + si) & 0xFF00) | (u8)(si >> 1)));
        bx = rec_put(bx, DSW(DS_obj_flags + si));
        bx = rec_put(bx, DSW(DS_obj_x + si));
        bx = rec_put(bx, DSW(DS_obj_z + si));
        bx = rec_put(bx, DSW(DS_obj_y8 + si));
        bx = rec_put(bx, DSW(DS_obj_heading + si));
        bx = rec_put(bx, DSW(DS_obj_vel + si));
        bx = rec_put(bx, DSW(DS_obj_cell + si));
        bx = rec_put(bx, DSW(DS_obj_waypoint + si));
    }
}

/* 0ab4:1604 replay_play_objects — simulation.md §4.11 (BX = ring index in/out; stops at the next marker) */
static u16 replay_play_objects(u16 bx)
{
    for (;;) {
        u16 ax = RB(bx), si;
        if (ax == 0xFFFF) return bx;
        si = (u16)((ax & 0xFF) << 1);
        DSW(DS_obj_pitch + si) = ax & 0xFF00;
        bx = (u16)((bx + 2) & 0x0FFF); DSW(DS_obj_flags + si) = RB(bx);
        bx = (u16)((bx + 2) & 0x0FFF); DSW(DS_obj_x + si) = RB(bx);
        bx = (u16)((bx + 2) & 0x0FFF); DSW(DS_obj_z + si) = RB(bx);
        bx = (u16)((bx + 2) & 0x0FFF); DSW(DS_obj_y8 + si) = RB(bx);
        bx = (u16)((bx + 2) & 0x0FFF); DSW(DS_obj_heading + si) = RB(bx);
        bx = (u16)((bx + 2) & 0x0FFF); DSW(DS_obj_vel + si) = RB(bx);
        bx = (u16)((bx + 2) & 0x0FFF); DSW(DS_obj_cell + si) = RB(bx);
        bx = (u16)((bx + 2) & 0x0FFF); DSW(DS_obj_waypoint + si) = RB(bx);
        bx = (u16)((bx + 2) & 0x0FFF);
    }
}

/* 0e12:4c51 replay_start — simulation.md §4.10 */
void replay_start(void)
{
    if (DSB(DS_ext_view) != 0) return;
    DSB(DS_replay_state) = 1;
    SEGW(RING_SEG, (u16)(RING_OFF + DSW(DS_replay_pos))) = 0xFFFF;   /* end marker */
    DSW(DS_replay_pos) = (u16)((DSW(DS_replay_pos) + 2) & 0x07FF);   /* sic: 7FFh, not FFFh */
    DSB(DS_ext_view) = 1;
    DSW(DS_cam_pitch) = 0xFFF0;
    DSB(DS_cam_roll_94AD) = 0;
    DSB(DS_world_rebuilt) = 1;
    DSW(DS_cam_dist) = (u16)(DSW(DS_car_eye_height) + 0x26);
    DSB(DS_cam_elev) = 0x30;
    DSB(DS_cam_yaw) = (u8)((DSW(DS_course) >> 8) + 0x10);
}

/* 0e12:2465 life_reset — simulation.md §4.14 */
void life_reset(void)
{
    int i;
    if (DSB(DS_return_to_road) != 0 || DSB(DS_crashed) != 0) {       /* respawn at road_mem[2] */
        u16 ax = DSW(0xB9CB);
        DSW(DS_sprite_x) = ax; DSW(DS_obj_x) = ax;
        ax = DSW(0xB9CD);
        DSW(DS_sprite_z) = ax; DSW(DS_obj_z) = ax;
        ax = DSW(0xB9CF);
        DSW(DS_sprite_y) = ax;
        DSW(DS_obj_y8) = (u16)((u16)(ax - DSW(DS_car_eye_height)) << 3);
        ax = DSW(0xB9D1);
        DSW(DS_view_heading) = ax;
        {
            u8 h = (u8)((ax >> 8) - 0x40);
            DSW(DS_obj_heading) = (u16)(h << 8 | h);
        }
    }
    build_colour_remap();
    detail_apply();
    DSW(DS_damage) = 0;
    DSB(DS_overrev_events) = 0;
    DSB(DS_return_to_road) = 0;
    DSB(DS_replay_paused) = 0;
    DSB(DS_replay_request) = 0;
    DSB(DS_steer_hold) = 0;
    DSB(DS_sky_flash) = 0;
    DSB(DS_gate_pos) = 0;
    DSB(DS_train_near) = 0;
    DSB(DS_view_roll) = 0;
    DSB(DS_ground_roll) = 0;
    DSB(DS_clock_frames) = 0;
    DSB(DS_prev_bits) = 0;
    DSB(DS_throttle_released) = 0;
    DSB(DS_throttle) = 0;
    DSW(DS_pitch_rate) = 0;
    DSW(DS_ground_pitch) = 0;
    DSB(DS_replay_state) = 0;
    DSB(DS_crash_flag) = 0;
    DSB(DS_crashed) = 0;
    DSB(DS_ext_view) = 0;
    DSB(DS_shake) = 0;
    DSW(DS_sprite_vis_count) = 0;
    DSB(DS_clock_running) = 0;
    DSB(DS_clock_drawn) = 0xFF;
    DSB(DS_radar_drawn) = 0xFF;
    DSB(DS_wiper_phase) = 0;
    DSB(DS_light_timer) = 1;
    DSB(DS_gear) = 1;                                          /* neutral */
    DSB(DS_invulnerable) = 0x32;
    for (i = 0x3F; i >= 0; i--) DSB(DS_drop_state + i) = 0;
    for (i = 0; i < 9; i++) DSW(DS_sprite_id + 2 * i) = 0;
    police_pursuit_clear();
}
