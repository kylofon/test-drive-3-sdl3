/* Simulation module: car physics (0977:0008) and its helpers in segment 0e12 (simulation.md §4.2, §4.3, §4.8).
 *
 * Everything reads and writes the original globals in mem[] (DGROUP). The car constants DS:1200..1244 come
 * from the car .LST file (descriptions.md). The MSC long helpers (_aFlmul, _aFldiv, _aFuldiv, _aFNaldiv)
 * are modelled by the small inline functions below; a zero divisor faults like the runtime (R6003). */
#include "game/game.h"

/* ---- car constants (descriptions.md, "car[0xNNNN]") ---- */
#define CAR(o)      DSS(o)                    /* signed word at DS:o */
#define CARU(o)     DSW(o)                    /* the same word unsigned */

/* ---- MSC long arithmetic helpers ---- */
/* 1940:0998 _aFlmul: 32 x 32 -> low 32 bits */
static inline s32 lmul(s32 a, s32 b) { return (s32)((u32)a * (u32)b); }
/* 1940:08fc _aFldiv: signed, truncates toward zero; a zero divisor faults (DIV by 0 -> R6003) */
static inline s32 ldiv32(s32 a, s32 b)
{
    if (b == 0) div_error();
    if (a == INT32_MIN && b == -1) return a;          /* PORT: the runtime's unsigned core wraps here */
    return a / b;
}
/* 1940:0a14 _aFuldiv: unsigned */
static inline u32 uldiv32(u32 a, u32 b)
{
    if (b == 0) div_error();
    return a / b;
}
/* sign-preserving "abs >> n" used by the compiled code: cdq; xor; sub; sar n; xor; sub */
static inline s16 sgn_shr(s16 v, int n)
{
    s16 d = (s16)(v < 0 ? -1 : 0);
    s16 a = (s16)((v ^ d) - d);
    a = (s16)(a >> n);
    return (s16)((a ^ d) - d);
}
static inline s16 abs16(s16 v) { return (s16)(v < 0 ? -v : v); }

/* 0977:0008 car_physics — simulation.md §4.2 (compiled C; checked against the disassembly line by line) */
void car_physics(void)
{
    s16 squeal = 0;                                          /* bp-8 */
    s16 t, a, old, lim_slide, lim_grip, m;
    u16 ang, ax_, bz_;
    s32 floor_;
    int i;

    /* 1. engine inertia term */
    DSSL(DS_rpm_inertia) = ldiv32(DSSL(DS_rpm_inertia), 4);          /* _aFNaldiv(&12A4, 4) */
    DSSL(DS_rpm_inertia) += lmul((s32)(s16)(DSW(DS_rpm_prev) - DSW(DS_engine_rpm)), 6);
    DSW(DS_rpm_prev) = DSW(DS_engine_rpm);

    /* 2. position history */
    for (i = 0; i < 3; i++) {
        DSL(DS_pos_hist3 + 4 * i) = DSL(DS_pos_hist2 + 4 * i);
        DSL(DS_pos_hist2 + 4 * i) = DSL(DS_pos_hist1 + 4 * i);
        DSL(DS_pos_hist1 + 4 * i) = DSL(DS_pos_x + 4 * i);
    }

    /* 3. last road positions (F6) */
    if (DSB(DS_on_ground) && DSB(DS_surface_under_car) >= 0x10 && DSB(DS_surface_under_car) <= 0x14
        && DSB(DS_surface_under_car) != 0x12) {
        if (!(DSB(DS_frame_counter) & 1)) {
            for (i = 0; i < 4; i++) DSW(0xB9CB + 2 * i) = DSW(0xB9C3 + 2 * i);   /* road_mem[2] <- [1] */
            for (i = 0; i < 4; i++) DSW(0xB9C3 + 2 * i) = DSW(0xB9BB + 2 * i);   /* road_mem[1] <- [0] */
            DSW(0xB9BB) = DSW(DS_sprite_x);
            DSW(0xB9BD) = DSW(DS_sprite_z);
            DSW(0xB9BF) = DSW(DS_sprite_y);
            DSW(0xB9C1) = DSW(DS_view_heading);
        }
        DSW(DS_offroad_frames) = 0;
        DSB(DS_f6_allowed) = 0;
    }

    /* 4. wind gusts */
    if (DSW(DS_wind_level) == 0) {
        DSW(DS_wind_speed) = 0;
    } else {
        s16 lo = (s16)(DSW(DS_wind_level) << 9);
        t = (s16)(DSW(DS_rand_lo) & 0x9C);
        if (DSW(DS_rand_lo) & 0x4000) t = (s16)-t;
        t = (s16)(t + DSS(DS_wind_speed));
        if ((s16)(lo << 1) < t) t = (s16)(lo << 1);
        if (t < lo) t = lo;
        DSS(DS_wind_speed) = t;
    }

    DSW(DS_view_heading) = DSW(DS_body_heading) & 0xFFC0;
    ang = DSW(DS_course) & 0xFC00;
    DSL(DS_y_before) = DSL(DS_pos_y);

    /* 5. collision bump */
    if (DSB(DS_bump_frames)) {
        u32 b = (u32)lmul((s32)(DSW(DS_rand_lo) & 0x1C), 0x60) >> 2;
        DSL(DS_pos_y) += b - 0xC0;
        DSB(DS_bump_frames)--;
    }

    /* 6. velocity vector */
    t = (s16)ldiv32(DSSL(DS_speed_long), 16);
    if (DSSL(DS_speed_long) < 0) { ang = (u16)(ang + 0x8000); t = (s16)-t; }
    polar(ang, (u16)t);
    DSSL(DS_vel_x) = DSS(0x9460);                       /* sin_out */
    DSSL(DS_vel_z) = DSS(0x9462);                       /* cos_out */
    if (DSB(DS_crashed)) { DSSL(DS_vel_y) = 0; DSSL(DS_vel_z) = 0; DSSL(DS_vel_x) = 0; }

    /* 7. speed magnitude */
    ax_ = DSW(DS_vel_x); if ((s16)ax_ < 0) ax_ = (u16)-ax_;
    bz_ = DSW(DS_vel_z); if ((s16)bz_ < 0) bz_ = (u16)-bz_;
    DSW(DS_car_speed) = ax_;
    if (ax_ < bz_) DSW(DS_car_speed) = bz_;
    {
        u16 sum = (u16)(ax_ + bz_ + (u16)(DSW(DS_car_speed) << 1));
        DSW(DS_speed_oct) = (u16)(sum / 12);
        DSW(DS_car_speed) = (u16)(sum / 0x180);
    }

    /* 8. integrate: half the vector */
    DSSL(DS_vel_x) = ldiv32(lmul(DSSL(DS_vel_x), 6), 12);
    DSSL(DS_vel_z) = ldiv32(lmul(DSSL(DS_vel_z), 6), 12);
    for (i = 0; i < 3; i++) DSL(DS_pos_x + 4 * i) += DSL(DS_vel_x + 4 * i);

    /* 9. publish the player position */
    DSW(DS_sprite_x) = DSW(DS_obj_x) = (u16)(DSL(DS_pos_x) >> 7) & 0x7FFF;
    DSB(DS_pos_frac_x) = (u8)((DSB(DS_pos_x) & 0x7F) << 1);
    DSW(DS_sprite_z) = DSW(DS_obj_z) = (u16)(DSL(DS_pos_z) >> 7) & 0x3FFF;
    DSB(DS_pos_frac_z) = (u8)((DSB(DS_pos_z) & 0x7F) << 1);
    DSW(DS_sprite_y) = (u16)((u16)(DSL(DS_pos_y) >> 7) + DSW(DS_car_eye_height));
    DSW(DS_obj_y8) = (u16)((u16)(DSW(DS_sprite_y) - DSW(DS_car_eye_height)) << 3);
    DSW(DS_obj_heading) = (u16)((((u16)(DSW(DS_view_heading) - 0x4000) & 0xFF00) >> 8) * 0x101);

    /* 10. ground contact */
    floor_ = (s32)((u32)(s32)DSS(DS_ground_height) << 7);
    DSB(DS_on_ground) = 1;
    if (floor_ < DSSL(DS_pos_y)) {
        s32 lim = (s16)(DSW(DS_leg_gravity) * 4);
        if (DSSL(DS_pos_y) - floor_ > lim || DSSL(DS_vel_y) > (s32)DSS(DS_leg_gravity) || DSSL(DS_vel_y) < 0) {
            DSB(DS_on_ground) = 0;
            DSSL(DS_vel_y) -= (s16)((s16)(DSW(DS_leg_gravity) * 3) / 2);
            DSS(DS_pitch_rate) = -3;
            if (DSSL(DS_speed_long) < 0) DSS(DS_pitch_rate) = 3;
        } else if (DSSL(DS_vel_y) >= 0) {
            DSSL(DS_vel_y) = 0;
            DSSL(DS_pos_y) = floor_;
        }
    }
    if (floor_ == DSSL(DS_pos_y) && DSSL(DS_vel_y) == 0) {
        DSB(DS_view_roll) = DSB(DS_ground_roll);
        if (!(DSB(DS_damage) & 4))   /* imul; cdq keeps only the low word of the product; idiv */
            DSS(DS_pitch_rate) = idiv32_16((s16)(CAR(0x1240) * DSS(DS_ground_pitch)), CAR(0x1242), NULL);
        else
            DSS(DS_pitch_rate) = DSS(DS_ground_pitch);
    }
    if (floor_ > DSSL(DS_pos_y)) {
        s16 u;
        s32 pen;
        DSB(DS_view_roll) = DSB(DS_ground_roll);
        if (!(DSB(DS_damage) & 4))
            DSS(DS_pitch_rate) = idiv32_16((s16)(CAR(0x1240) * DSS(DS_ground_pitch)), CAR(0x1242), NULL);
        else
            DSS(DS_pitch_rate) = DSS(DS_ground_pitch);
        u = sgn_shr(DSS(0x94BD) /* car_impact */, 4);
        pen = floor_ - DSSL(DS_pos_y) - DSSL(DS_vel_y);
        if (pen >= (s32)(s16)(u * 5)) sfx_play(0x13);
        u = sgn_shr(DSS(0x94BD), 4);
        if (pen >= (s32)(s16)((s16)(0x24 - (s16)(DSW(DS_skill_level) << 1)) * u)) {
            DSB(DS_crash_flag) = 1;
            DSSL(DS_vel_y) = 0; DSSL(DS_vel_z) = 0; DSSL(DS_vel_x) = 0;
            DSW(DS_sprite_y) = (u16)((u16)(DSL(DS_pos_y) >> 7) + DSW(DS_car_eye_height) - 0x10);
        } else {
            u = sgn_shr(DSS(0x94BD), 4);
            pen = floor_ - DSSL(DS_pos_y) - DSSL(DS_vel_y);
            if (pen >= (s32)(s16)((s16)(0x12 - DSW(DS_skill_level)) * u)) landing_damage();
        }
        if (DSSL(DS_vel_y) >= 0) DSSL(DS_vel_y) += ldiv32(floor_ - DSSL(DS_pos_y), 8);
        else                     DSSL(DS_vel_y) = ldiv32((s32)(0u - DSL(DS_vel_y)), 8);
        DSSL(DS_pos_y) = (s32)((u32)floor_ + 1);
    }

    /* 11. nose pitch: the original's clamp (t > FFh && t < -FFh) can never hold, so it always stores */
    t = (s16)(DSS(DS_pitch) + DSS(DS_pitch_rate));
    if (DSB(DS_on_ground)) t = (s16)(t + DSS(DS_accel_smooth));
    if (!(t > 0xFF && t < -0xFF)) DSS(DS_pitch) = t;
    DSW(DS_obj_pitch) = (u16)(pitch_shear(DSW(DS_pitch)) << 8);

    DSW(DS_speed_sq) = (u16)(DSW(DS_car_speed) * DSW(DS_car_speed));   /* MUL, AX only */
    DSW(DS_speed_sq + 2) = 0;

    /* 12. engine speed from the wheels */
    if (DSB(DS_on_ground) && DSB(DS_gear) != 1)
        DSW(DS_engine_rpm) = (u16)uldiv32((u32)lmul(DSS(DS_gear_ratios + 2 * DSB(DS_gear)), DSW(DS_car_speed)),
                                          (u32)(s32)CAR(0x122A));

    /* 13. surface grip and bumps */
    if (DSB(DS_on_ground)) {
        u8 susp;
        DSW(DS_grip) = (u16)(0x7D - (u16)(DSW(DS_snow_level) * 6 + DSW(DS_rain_level) * 3));
        susp = (u8)((DSB(DS_damage) & 4) >> 1);
        switch (DSB(DS_surface_under_car)) {
        case 0x0E:
            DSW(DS_grip) = 0;
            break;
        case 0x0F: case 0xFF:
            DSW(DS_grip) = (u16)((u16)(DSW(DS_grip) * 3) >> 2);
            if (!(DSB(DS_rand_lo) & 0x60) && DSW(DS_car_speed) && !DSB(DS_shake))
                DSB(DS_shake) = (u8)(((DSB(DS_rand_lo) & 0x1C) >> 2) + susp + 1);
            break;
        case 0x11:
            if (!(DSB(DS_frame_counter) & 1) && DSW(DS_car_speed)) sfx_play(0x11);
            DSW(DS_grip) = (u16)((u16)(DSW(DS_grip) << 2) / 5);
            if (!(DSB(DS_rand_lo) & 0x70) && DSW(DS_car_speed) && !DSB(DS_shake))
                DSB(DS_shake) = (u8)(susp + 1);
            break;
        case 0x12:
            if (!(DSB(DS_frame_counter) & 1) && DSW(DS_car_speed)) sfx_play(0x11);
            DSW(DS_grip) = (u16)((u16)(DSW(DS_grip) << 2) / 5);
            if (DSW(DS_car_speed) && !DSB(DS_shake)) DSB(DS_shake) = 4;
            break;
        default:
            break;
        }
    }

    /* 14. forces; repeats only for the "throttle released" cruise search (B706) */
    for (;;) {
        s32 d;
        d = lmul(CAR(0x1230), DSB(DS_throttle)) + CAR(0x122E);
        DSSL(DS_drive) = d;
        d = (s32)((u32)lmul(d, (s32)((u32)DSW(DS_engine_rpm) + 0x200)) >> 10);
        d = (s32)((u32)d - (u16)((u16)(DSW(DS_engine_rpm) * DSW(DS_engine_rpm)) / 0x1E));  /* 16-bit square */
        DSSL(DS_drive) = d;
        DSSL(DS_drive) += DSSL(DS_rpm_inertia);
        if (DSW(DS_car_top_speed) <= DSW(DS_car_speed)) DSSL(DS_drive) = 0;
        if (DSB(DS_damage) & 0x20) DSSL(DS_drive) = ldiv32(DSSL(DS_drive), 4);
        if (DSW(DS_damage) & (u16)(0x100u << (DSB(DS_gear) & 0x1F))) DSSL(DS_drive) = ldiv32(DSSL(DS_drive), 4);

        DSSL(DS_engine_drag) = lmul(DSW(DS_engine_rpm), DSW(DS_engine_rpm));
        DSSL(DS_engine_drag) += lmul(CAR(0x1202), DSW(DS_engine_rpm));
        DSSL(DS_engine_drag) = ldiv32(DSSL(DS_engine_drag), CAR(0x1208));
        DSL(DS_resist) = uldiv32(DSW(DS_car_speed), (u32)(s32)CAR(0x120E));

        polar((u16)(DSW(DS_body_heading) - DSW(DS_wind_dir)) & 0xFFC0, DSW(DS_wind_speed));
        DSSL(DS_resist) += idiv32_16((s16)(DSS(0x9462) * 12), CAR(0x123E), NULL);
        DSS(DS_crosswind) = 0;
        if (DSSL(DS_speed_long) != 0) {
            t = idiv32_16(DSS(0x9460), CAR(0x123E), NULL);
            DSS(DS_crosswind) = sgn_shr(t, 4);
            DSL(DS_resist) += 0x30;
            if (DSB(DS_yaw_slide) || DSB(DS_skid)) {
                DSL(DS_resist) += 0x280;
                if (DSW(DS_snow_level)) DSL(DS_resist) += 0x28;
                if (DSW(DS_rain_level)) DSL(DS_resist) += 0x14;
            }
            if (DSB(DS_on_ground)) {
                s32 b = lmul(CAR(0x1226), DSB(DS_brake));
                if (!(DSB(DS_damage) & 0x18))              DSL(DS_resist) += (u32)b << 2;
                else if ((DSB(DS_damage) & 0x18) == 0x18)  DSL(DS_resist) += (u32)b;
                else                                       DSL(DS_resist) += (u32)b << 1;
            }
            if (DSB(DS_on_ground)) {
                switch (DSB(DS_surface_under_car)) {          /* MUL, DX cleared */
                case 0x0E:            DSL(DS_resist) += (u16)(0xA0 * DSW(DS_car_speed)); break;
                case 0x0F: case 0xFF: DSL(DS_resist) += (u16)(0x50 * DSW(DS_car_speed)); break;
                case 0x11:            DSL(DS_resist) += (u16)(0x1E * DSW(DS_car_speed)); break;
                case 0x12:            DSL(DS_resist) += (u16)(0x37 * DSW(DS_car_speed)); break;
                default: break;
                }
            }
        }
        if (DSB(DS_throttle) == 0 || DSB(DS_throttle_released) == 0) break;
        DSW(DS_accel) = (u16)(DSW(DS_drive) - DSW(DS_engine_drag) - DSW(DS_resist));
        if (DSS(DS_accel) > 0) { DSB(DS_throttle)--; continue; }
        if (DSB(DS_throttle) < 0x1D) DSB(DS_throttle) += 2;
        DSB(DS_throttle_released) = 0;
    }
    DSB(DS_throttle_released) = 0;

    /* 15. climbing resistance */
    if (DSB(DS_on_ground) && DSW(DS_car_speed))
        DSSL(DS_resist) += ldiv32(lmul(DSSL(DS_pos_y) - DSSL(DS_y_before), DSS(DS_leg_gravity)),
                                  lmul(CAR(0x1210), DSW(DS_car_speed)));

    /* 16. net force */
    if (DSB(DS_gear) == 0 && DSSL(DS_speed_long) == 0) DSSL(DS_speed_long) = -1;
    if (DSB(DS_gear) == 1 || !DSB(DS_on_ground)) {
        DSW(DS_accel) = (u16)-DSW(DS_resist);
        if (DSSL(DS_drive) != 0) {
            s32 div = (DSB(DS_gear) == 1) ? CAR(0x1236) : (s16)(CARU(0x1236) << 1);
            DSW(DS_engine_rpm) = (u16)(DSW(DS_engine_rpm) + (u16)ldiv32(DSSL(DS_drive) - DSSL(DS_engine_drag), div));
        }
    } else {
        DSW(DS_accel) = (u16)(DSW(DS_drive) - DSW(DS_engine_drag) - DSW(DS_resist));
        if ((DSB(DS_gear) == 0 && DSSL(DS_speed_long) > 0) || (DSB(DS_gear) != 0 && DSSL(DS_speed_long) < 0))
            DSW(DS_accel) = (u16)((u16)-DSW(DS_engine_drag) - DSW(DS_drive) - DSW(DS_resist));
    }
    if (DSB(DS_on_ground)) {
        polar((u16)(DSW(DS_course) - DSW(DS_body_heading)), 0x101);
        DSS(DS_accel) = (s16)(lmul(DSS(DS_accel), DSS(0x9462)) >> 8);      /* sar of the long */
        if ((u16)((u16)(DSW(DS_course) - DSW(DS_body_heading) + 0x2000) & 0x7F00) > 0x4000) {
            DSW(DS_accel) -= 0xF0;
            if (DSW(DS_snow_level)) DSW(DS_accel) += 0x28;
            if (DSW(DS_rain_level)) DSW(DS_accel) += 0x14;
        }
    }

    /* 17. traction limit */
    a = abs16(DSS(DS_accel));
    if (DSB(DS_on_ground)) {
        lim_slide = (s16)uldiv32((u32)lmul(lmul(CAR(0x1204), DSS(DS_leg_gravity)), DSW(DS_grip)), 0x4B0);
        lim_grip  = (s16)uldiv32((u32)lmul(lmul(CAR(0x1206), DSS(DS_leg_gravity)), DSW(DS_grip)), 0x4B0);
        if (a > lim_slide) DSB(DS_skid) = 1;
        else if (a <= lim_grip) DSB(DS_skid) = 0;
        if (a >= lim_grip) squeal = 1;
        if (DSB(DS_yaw_slide) == 1 || DSB(DS_skid) == 1) a = lim_grip;
    } else {
        DSB(DS_skid) = 0;
    }

    DSS(DS_accel_pitch) = sgn_shr(a, 9);
    if ((DSB(DS_gear) != 0 && DSS(DS_accel) < 0) || (DSB(DS_gear) == 0 && DSS(DS_accel) > 0))
        DSS(DS_accel_pitch) = (s16)-DSS(DS_accel_pitch);
    DSS(DS_accel_smooth) = (s16)(DSS(DS_accel_smooth) + (s16)((s16)(DSS(DS_accel_pitch) - DSS(DS_accel_smooth)) / 2));

    old = DSS(DS_accel);
    DSS(DS_accel) = (s16)ldiv32((s32)a * CAR(0x1228), 4);
    if (DSSL(DS_speed_long) >= 0) {
        if (old < 0) {
            if ((s32)DSS(DS_accel) >= DSSL(DS_speed_long)) DSSL(DS_speed_long) = 0;
            else DSSL(DS_speed_long) -= DSS(DS_accel);
        } else {
            DSSL(DS_speed_long) += DSS(DS_accel);
        }
    } else {
        if (old < 0) {
            if ((s32)(0u - DSL(DS_speed_long)) <= (s32)DSS(DS_accel)) DSSL(DS_speed_long) = 0;
            else DSSL(DS_speed_long) += DSS(DS_accel);
        } else {
            DSSL(DS_speed_long) -= DSS(DS_accel);
        }
    }

    /* 18. steering (uses DS:B70E = ticks the last frame took) */
    t = (s16)(DSB(DS_steer_wheel) * 2 - 0x20);
    t = (s16)(t + idiv32_16((s16)((s16)(t * DSB(DS_brake)) * CAR(0x123A)), CAR(0x123C), NULL));
    t = (s16)(t + DSS(DS_crosswind));
    if (DSB(DS_damage) & 1) t = (s16)(t + 4);
    else if (DSB(DS_damage) & 2) t = (s16)(t - 4);
    DSB(DS_speedo_step) = (DSSL(DS_speed_long) >= 0) ? (u8)(DSW(DS_car_speed) >> 2) : 0;
    {
        u16 x = (u16)((u16)(DSB(DS_frame_ticks) + 0x11) * CARU(0x1222));
        x = (u16)(x * 7);
        m = (s16)ldiv32(lmul(lmul(x, t), 0x50), (s16)(0x2B8 * CAR(0x1224)));
        m = (s16)ldiv32(lmul((s16)((s16)(3 * DSC(DS_steering_response)) + 0x10), m), 0x24);
        if (DSW(DS_car_speed) < 0x0C) m = (s16)ldiv32(lmul(m, DSW(DS_car_speed)), 0x0C);
        if (DSB(DS_steer_wheel) == 0x20 || DSB(DS_steer_wheel) == 0) m = (s16)(m << 1);
    }
    if (DSB(DS_on_ground) && DSSL(DS_speed_long) != 0) {
        s16 d, ad, m1, m2;
        if (DSSL(DS_speed_long) >= 0) DSW(DS_body_heading) += (u16)m;
        else                          DSW(DS_body_heading) -= (u16)m;
        d = (s16)(DSW(DS_body_heading) - DSW(DS_course));
        ad = abs16(d);
        m1 = (s16)grip_yaw_limit(CARU(0x1204));
        if (m1 < ad) DSB(DS_yaw_slide) = 1;
        m2 = (s16)grip_yaw_limit(CARU(0x1206));
        if (m2 >= ad) DSB(DS_yaw_slide) = 0;
        if (ad >= m2) squeal = 1;
        if (DSB(DS_yaw_slide)) ad = m2;
        if (d < 0) DSW(DS_course) -= (u16)ad;
        else       DSW(DS_course) += (u16)ad;
    }

    /* 19. gauges */
    DSB(DS_tach_step) = (u8)(DSW(DS_engine_rpm) >> 5);
    if (DSB(DS_speedo_step) > 0x1F) DSB(DS_speedo_step) = 0x1F;
    if (DSW(DS_car_tach_max) < (u16)DSB(DS_tach_step)) DSB(DS_tach_step) = DSB(DS_car_tach_max);

    /* 20. tyre noise */
    if (DSB(DS_yaw_slide) == 1 || DSB(DS_skid) == 1 || (squeal && DSB(DS_on_ground))) {
        u8 s = DSB(DS_surface_under_car);
        if (s == 0x0E || s == 0x0F || s == 0xFF) sfx_play(0x10);
        else if (s == 0x11 || s == 0x12)         sfx_play(0x11);
        else                                     sfx_play(0x0F);
    } else if (DSB(DS_sfx_15_16_started)) {
        sfx_play(0x8F);
        sfx_play(0x90);
        DSB(DS_sfx_15_16_started) = 0;
    }

    /* 21. off-road timer */
    DSW(DS_offroad_frames) = (u16)(DSW(DS_offroad_frames) + 1) & 0x1FF;
    if (DSW(DS_offroad_frames) == 0x40) {
        message_box(0x0C);                            /* "Press F6 to return to the road" */
        DSB(DS_f6_allowed) = 1;
    }

    /* 22. rev limiter */
    if (CARU(0x122C) < DSW(DS_engine_rpm)) {
        DSW(DS_overrev_count)++;
        if ((u16)(CARU(0x122C) + 0x1E) < DSW(DS_engine_rpm)) DSW(DS_overrev_count)++;
        if ((u16)(CARU(0x122C) + 0x3C) < DSW(DS_engine_rpm)) DSW(DS_overrev_count) += 2;
        if (DSS(DS_overrev_count) >= 0x10) {
            DSW(DS_overrev_count) = 0;
            overrev_damage();
        }
    } else {
        DSW(DS_overrev_count) = 0;
    }
}

/* CL after `call 0c1c:111d` (sfx_play_ax). The routine does `mov cl, bl` (= the effect id) on every path
 * past its early exits (sound not initialised, effect blocked in the external view, sounds off, stop
 * request, id > 17h) and keeps CX otherwise (its callees save CX). Callers below then shift by CL, so the
 * damage bit depends on it (PORT: register leak modelled explicitly; see the report / TODO(verify)). */
static u8 sfx_play_ax_cl(u16 id, u8 cl)
{
    u8 a = (u8)id;
    bool reaches = DSB(DS_snd_on) == 1 && id != 0
                   && (a == 2 || a == 6 || DSB(DS_ext_view) == 0)
                   && DSB(DS_sfx_off) == 0 && !(a & 0x80) && a <= 0x17;
    sfx_play_ax(id);
    return reaches ? a : cl;
}

/* 0e12:0e74 overrev_damage — simulation.md §4.8 */
void overrev_damage(void)
{
    u8 cl;
    if (DSW(DS_skill_level) < 3 || DSB(DS_invulnerable)) return;
    DSB(DS_overrev_events) = (u8)((DSB(DS_overrev_events) + 1) & 3);
    if (DSB(DS_overrev_events)) return;
    if (DSB(DS_gear) == 1 || (DSB(DS_rand_hi + 1) & 3) == 0) {     /* test ah,3 on word DS:00D4 */
        sfx_play_ax(0x0A);
        DSW(DS_damage) |= 0x20;
        return;
    }
    cl = DSB(DS_gear);
    if (cl == 1) return;
    cl = sfx_play_ax_cl(0x0B, cl);
    DSW(DS_damage) |= (u16)(0x100u << (cl & 0x1F));   /* 16-bit: CL = 0Bh after the sfx gives 0 */
}

/* 0e12:0ec5 landing_damage — simulation.md §4.8 (falls into random_damage) */
void landing_damage(void)
{
    u8 cl;
    if (DSW(DS_skill_level) < 3 || DSB(DS_invulnerable)) return;
    if ((DSB(DS_rand_lo + 1) & 0xF8) == 0) {                         /* test ah,0F8h on DS:00D2 */
        cl = DSB(DS_gear);
        if (cl == 1) return;
        cl = sfx_play_ax_cl(0x0B, cl);
        DSW(DS_damage) |= (u16)(0x100u << (cl & 0x1F));
        return;
    }
    random_damage();
}

/* 0e12:0edb random_damage — simulation.md §4.8 */
void random_damage(void)
{
    u16 ax = (u16)(DSW(DS_rand_lo) >> 2);
    u8 h = (u8)(ax >> 8);
    u8 cl = (u8)(ax & 1);
    if ((h & 0x65) == 0) {
        cl = sfx_play_ax_cl(0x0C, cl);
        DSW(DS_damage) |= (u16)(4u << (cl & 0x1F));     /* suspension (4) / brakes (8); 4000h after the sfx */
        return;
    }
    if (h & 0x1F) return;
    message_box(0x2E);                                    /* "Your alignment is out" (CX saved around it) */
    DSW(DS_damage) |= (u16)(1u << cl);
}

/* 0e12:0fd6 bounce_back — simulation.md §4.3 */
void bounce_back(void)
{
    int i;
    DSB(DS_shake) = 4;
    for (i = 0; i < 6; i++) DSW(DS_pos_x + 2 * i) = DSW(DS_pos_hist3 + 2 * i);
}

/* 0e12:23b1 grip_yaw_limit — simulation.md §4.3 */
u16 grip_yaw_limit(u16 g)
{
    u16 t = (u16)((u16)(g * CARU(0x1200)) >> 2);           /* MUL, AX only */
    u32 p;
    t = div32_16((u32)t * DSW(DS_grip), 0xFA, NULL);
    p = (u32)t * CARU(0x1238);
    if (DSW(DS_car_speed) <= (u16)(p >> 16)) return 0x7FFF;
    return (u16)(div32_16(p, DSW(DS_car_speed), NULL) >> 1);
}

/* 0e12:6686 pitch_shear — simulation.md §4.3 (AL = shear code from the table DS:BCE1) */
u8 pitch_shear(u16 p)
{
    u8 orig = (u8)p, al = orig, dl;
    if (al & 0x80) al = (u8)-al;
    dl = DSB(DS_pitch_shear_tab + ((al & 0x7F) >> 2));
    if (!(orig & 0x80)) dl = (u8)-dl;
    return dl;
}
