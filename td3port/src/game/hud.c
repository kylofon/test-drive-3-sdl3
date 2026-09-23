/* HUD module (hud.md): race_input, cockpit set-up, steering wheel rim and marker, needles, rolling
 * odometer, gear lever. Segment 0792 (per-frame HUD parts). The top bar (0e12) is in hud_topbar.c,
 * the chase/replay panel and the crash overlays in hud_overlay.c.
 *
 * All state lives in mem[] at its DGROUP address (PORTING.md). Car-file fields (descriptions.md):
 * gauges DS:CC64.., marker DS:E77E.., colours DS:CEAE.., picture pair counts DS:0B50.., lever DS:E564.. */
#include <string.h>
#include "game/game.h"

/* page(p) of hud.md: gfx_set_page(p) with the DS:009A mirror written first, as the original does. */
static void hud_page(s16 p)
{
    DSW(DS_page_cur) = (u16)p;
    gfx_set_draw_page(p);
}

/* Car gauge block words (descriptions.md "08A gauges"), DS:CC64 + n. */
#define GAUGE(off) DSS(DS_car_gauges + ((off) - 0xCC64))
/* Steering-wheel marker block (descriptions.md "076"), DS:E77E + n. */
#define MARKER(off) DSW(DS_car_steer_marker + ((off) - 0xE77E))
/* Dashboard colours (descriptions.md "080"), DS:CEAE + n. */
#define CCOL(off) DSB(DS_car_colours + ((off) - 0xCEAE))
/* Picture pair counts (descriptions.md "013"), DS:0B50 + n. */
#define PICPAIRS(off) DSW(DS_car_pic_pairs + ((off) - 0x0B50))

#define POLAR_X DSS(DS_dxl2)    /* DS:9460 polar x (hud.md §3.2) */
#define POLAR_Y DSS(DS_dxr2)    /* DS:9462 polar y */

#define LEVER_SHOWN DSB(DS_look_rect_dirty)   /* DS:CC52 lever_shown (hud.md §3.2) */

/* load_pic(ext) of hud.md §4.3: strcpy(DS:0AC1, ext) on "A:<car>" (DS:0ABA), load into the per-race
 * picture buffer (far DS:CC5C), unpack to DS:2500. ext_ds is the DGROUP string the original copies. */
static void hud_load_pic(u16 ext_ds)
{
    strcpy(ds_str((u16)(DS_path_car + 7)) /* DS:0AC1 */, ds_str(ext_ds));
    file_load_far(DS_path_car, ds_far(DS_view_block) /* DS:CC5C pic_file_buf */);
    lzw_decode(ds_far(DS_view_block), ds_ptr(DS_sprite_set) /* DS:2500 */);
}

/* marker angle = E782 * (2w - 32), computed as the original: mul (low word), shl 1, minus E782 << 5. */
static u16 hud_marker_angle(u8 w)
{
    u16 step = MARKER(0xE782);
    return (u16)((u16)((u16)(step * w) << 1) - (u16)(step << 5));
}

/* 0792:0414 race_input — hud.md §4.2 */
void race_input(s16 phase)
{
    u16 k = 0;
    if (phase == 2) {
        key_poll(&k);                               /* 0000:0f80 */
        key_dispatch();                             /* 0e12:0084 */
    } else {
        if (DSW(DS_joystick_on) != 0) {
            if (phase != 0 && !(phase == 1 && DSB(DS_frame_ticks) > 0x28)) goto tail;
            k = joystick_keys(&k);                  /* 0000:1e44 */
        }
        controls_poll_far();                        /* 0e12:0080 */
    }
tail:
    (void)k;
    if (DSB(DS_crashed) == 0 && DSB(DS_ext_view) == 0) {
        if ((DSW(DS_speed_long) | DSW(DS_speed_long + 2)) != 0 &&
            DSB(DS_steer_hold) == 0 && DSB(DS_mouse_on) == 0 && DSB(DS_wheel_centring) != 0 &&
            !(DSB(DS_frame_counter) & 1)) {
            if (DSB(DS_steer_wheel) >= 0x0E && DSB(DS_steer_wheel) <= 0x12) DSB(DS_steer_wheel) = 0x10;
            if (DSB(DS_steer_wheel) < 0x0E) DSB(DS_steer_wheel) = (u8)(DSB(DS_steer_wheel) + 2);
            if (DSB(DS_steer_wheel) > 0x12) DSB(DS_steer_wheel) = (u8)(DSB(DS_steer_wheel) - 2);
        }
        wheel_update();
    }
}

/* 0792:0922 cockpit_setup — hud.md §4.3 */
void cockpit_setup(void)
{
    DSB(DS_look_rect_dirty) = 0;                    /* lever_shown */
    DSB(DS_unused_e330) = 0;
    DSB(DS_lever_hold) = 0;
    DSB(DS_lever_target) = DSB(DS_car_lever_step + DSB(DS_gear));   /* DS:E565[gear] */
    DSB(DS_lever_step) = (u8)(DSB(DS_lever_target) + 1);
    DSB(DS_marker_pos) = 0xFF;
    DSB(DS_speedo_drawn) = 0xFF;
    DSB(DS_tach_drawn) = 0xFF;
    DSB(DS_odo_drawn) = 0xFF;
    DSB(DS_compass_drawn) = 0xFF;
    DSB(DS_rim_shown) = 1;
    DSB(DS_odo_roll_tens) = 6;
    DSB(DS_odo_roll_units) = 6;

    DSB(DS_colour_offset) = 0;
    strcpy(ds_str(0x0A8C), ds_str(0x118A) /* "OTWCOL.BIN" */);     /* into "A:" buffer DS:0A8A */
    pal_load(0x0A8A);
    DSB(DS_colour_offset) = 0x80;
    strcpy(ds_str((u16)(DS_path_car + 7)), ds_str(0x1195) /* "COL.BIN" */);
    pal_load(DS_path_car);
    ega_pal_black();                                /* 01f4:1c00, VGA: EGA palette untouched */

    hud_page(1);
    gfx_set_colour(0);
    gfx_fill_rect(0, 319, 16, 111);
    cockpit_pictures_draw(199);
    hud_page(1);
    hud_load_pic(0x119D /* ".ETC" */);
    pic_draw(DS_sprite_set, PICPAIRS(0x0B64), 56, 264, 175, 0);

    s16 o = (DSW(DS_video_mode) == 0x13) ? 0x80 : 0;
    gfx_move_to(280, 156);
    gfx_set_colour((s16)(o + 0x0F));
    gfx_read_bitmap(DS_marker_mask, 1, 5);
    gfx_move_to(288, 175);
    gfx_read_bitmap(DS_knob_masks + 0x00, 4, 24);  /* E090, colour o+0Fh still set */
    gfx_set_colour((s16)(o + 7));    gfx_read_bitmap(DS_knob_masks + 0x60, 4, 24);   /* E0F0 */
    gfx_set_colour((s16)(o + 8));    gfx_read_bitmap(DS_knob_masks + 0xC0, 4, 24);   /* E150 */
    gfx_set_colour((s16)(o + 0));    gfx_read_bitmap(DS_knob_masks + 0x120, 4, 24);  /* E1B0 */
    gfx_set_colour((s16)(o + 0x0C)); gfx_read_bitmap(DS_knob_masks + 0x180, 4, 24);  /* E210 */
    gfx_set_colour((s16)(o + 4));    gfx_read_bitmap(DS_knob_masks + 0x1E0, 4, 24);  /* E270 */

    gfx_copy_rect(0, 319, 112, 199, 0, 103, 0, 1);                  /* centre cockpit -> page 1 */
    s16 tx = GAUGE(0xCC7C), ty = GAUGE(0xCC7E), sx = GAUGE(0xCC80), sy = GAUGE(0xCC82);
    gfx_copy_rect(tx, (s16)(tx + 39), (s16)(ty - 32), ty, 168, 136, 0, 1);
    gfx_copy_rect((s16)(tx + 40), (s16)(tx + GAUGE(0xCC84) - 1), (s16)(ty - 32), ty, 168, 169, 0, 1);
    gfx_copy_rect(sx, (s16)(sx + GAUGE(0xCC86) - 1), (s16)(sy - 32), sy, 264, 199, 0, 1);

    hud_load_pic(0x11A2 /* "L.BOT" */);
    pic_draw(DS_sprite_set, PICPAIRS(0x0B60), 168, 0, 151, 0);
    hud_load_pic(0x11A8 /* "R.BOT" */);
    pic_draw(DS_sprite_set, PICPAIRS(0x0B62), 168, 0, 199, 0);
    load_opponent_pob_nonvga();
    DSB(DS_colour_offset) = 0;
    hud_page(0);
    view_first_frame();
    ega_pal_restore();                              /* 01f4:1c34, VGA no-op */
    pal_apply();                                    /* 01f4:1c48: the cockpit appears */
}

/* 0792:0cec view_first_frame — hud.md §4.3 */
void view_first_frame(void)
{
    hud_page(0);
    text_set_colours(15, 0);
    frame_update();
    frame_draw();
    if (DSB(DS_ext_view) == 0) view_present();
    hud_page(0);
}

/* 0792:10a6 hud_reset_topbar — hud.md §4.3 */
void hud_reset_topbar(void)
{
    DSB(DS_clock_drawn) = 0xFF;
    if (DSW(DS_race_state) != 2) DSW(DS_last_cell) = 0xFFFF;     /* DS:BD34 render3d cache */
    hud_page(0);
}

/* 0792:10c8 shifter_update — hud.md §4.8 */
void shifter_update(void)
{
    if (DSB(DS_knob_hidden) /* B6DB lever_always */ != 0 && LEVER_SHOWN == 0) shift_lever_draw();
    if (DSB(DS_lever_hold) == 0 && DSB(DS_lever_target) != DSB(DS_lever_step)) {
        if (DSB(DS_lever_target) > DSB(DS_lever_step)) DSB(DS_lever_step)++;
        else DSB(DS_lever_step)--;
        shift_lever_draw();
        if (DSB(DS_lever_step) == DSB(DS_lever_target)) DSB(DS_lever_hold) = 0x10;
    }
    if (DSB(DS_knob_hidden) == 0 && DSB(DS_lever_hold) == 0 && LEVER_SHOWN == 1) {
        gfx_copy_rect(208, 263, 104, 151, 248, 171, 1, 0);       /* put the cockpit back */
        LEVER_SHOWN = 0;
    }
    if (DSB(DS_lever_hold) != 0) DSB(DS_lever_hold)--;
}

/* 0792:1156 shift_lever_draw — hud.md §4.8 */
void shift_lever_draw(void)
{
    if (LEVER_SHOWN == 0) {
        gfx_copy_rect(248, 303, 124, 171, 208, 151, 0, 1);       /* save the screen under the lever */
        LEVER_SHOWN = 1;
    }
    gfx_copy_rect(264, 319, 104, 151, 208, 199, 1, 1);           /* fresh gate into the compose area */
    hud_page(1);
    u16 si = (u16)(DSB(DS_lever_step) * 2);
    s16 x = (s16)(DSB(DS_car_lever_path + si) + 208);            /* DS:E56D + 2*step, u8 */
    s16 y = (s16)(DSB(DS_car_lever_path + si + 1) + 175);        /* DS:E56E + 2*step, u8 */
    gfx_move_to(x, y);
    s16 o = (DSW(DS_video_mode) == 0x13) ? 0x80 : 0;
    gfx_set_colour((s16)(o + 0x0F)); gfx_draw_bitmap(DS_knob_masks + 0x00, 4, 24);
    gfx_set_colour((s16)(o + 7));    gfx_draw_bitmap(DS_knob_masks + 0x60, 4, 24);
    gfx_set_colour((s16)(o + 8));    gfx_draw_bitmap(DS_knob_masks + 0xC0, 4, 24);
    gfx_set_colour((s16)(o + 0));    gfx_draw_bitmap(DS_knob_masks + 0x120, 4, 24);
    gfx_set_colour((s16)(o + 0x0C)); gfx_draw_bitmap(DS_knob_masks + 0x180, 4, 24);
    gfx_set_colour((s16)(o + 4));    gfx_draw_bitmap(DS_knob_masks + 0x1E0, 4, 24);
    hud_page(0);
    gfx_copy_rect(208, 263, 152, 199, 248, 171, 1, 0);           /* lever to the screen */
    DSB(DS_lever_hold) = 1;
}

/* 0792:1310 wheel_update — hud.md §4.4 */
void wheel_update(void)
{
    s16 rim_changed = 0;
    u8 w = DSB(DS_steer_wheel);
    DSB(DS_rim_wanted) = (w < 8) ? 0 : 1;
    if (w > 0x18) DSB(DS_rim_wanted) = 2;
    if (DSB(DS_rim_wanted) != DSB(DS_rim_shown)) {
        switch (DSB(DS_rim_wanted)) {
        case 0:  wheel_rim_blit(0x30, 0); break;             /* L.BOT */
        case 2:  wheel_rim_blit(0, 0); break;                /* R.BOT */
        default: wheel_rim_blit(0x60, 0x20); break;          /* centre */
        }
        DSB(DS_rim_shown) = DSB(DS_rim_wanted);
        DSB(DS_speedo_drawn) = 0xFF;
        DSB(DS_tach_drawn) = 0xFF;
        DSB(DS_odo_drawn) = (u8)(DSB(DS_odo_sixtieths) ^ 0x80);
        rim_changed = 1;
    }
    if (DSB(DS_steer_wheel) != DSB(DS_marker_pos)) {
        s16 x, y;
        if (DSB(DS_marker_pos) != 0xFF) {                     /* erase: restore the saved 16x5 */
            polar(hud_marker_angle(DSB(DS_marker_pos)), MARKER(0xE780));
            x = (s16)(MARKER(0xE784) + POLAR_X);
            y = (s16)(MARKER(0xE786) - POLAR_Y);
            gfx_copy_rect(264, 279, 152, 156, x, (s16)(y + 4), 1, 0);
        }
        polar(hud_marker_angle(DSB(DS_steer_wheel)), MARKER(0xE780));
        x = (s16)(MARKER(0xE784) + POLAR_X);
        y = (s16)(MARKER(0xE786) - POLAR_Y);
        gfx_copy_rect(x, (s16)(x + 15), y, (s16)(y + 4), 264, 156, 0, 1);   /* save under the marker */
        hud_page(0);
        steer_marker_draw();
    }
    if (rim_changed) {
        tach_draw();
        odometer_draw();
    }
}

/* 0792:1488 wheel_rim_blit — hud.md §4.4 (page-1 source = screen - (32 - dx, dy)) */
void wheel_rim_blit(s16 dy, s16 dx)
{
    gfx_copy_rect(dx, (s16)(dx + 0xA7), (s16)(0x98 - dy), (s16)(0xC7 - dy), 32, 199, 1, 0);
    s16 si = (s16)(dx + GAUGE(0xCC7C));
    gfx_copy_rect((s16)(si - 0x20), (s16)(si + 7), (s16)(0x98 - dy), (s16)(GAUGE(0xCC7E) - dy),
                  168, 136, 1, 1);
    gfx_copy_rect((s16)(si + 8), (s16)(GAUGE(0xCC84) + si - 0x21), (s16)(0x98 - dy),
                  (s16)(GAUGE(0xCC7E) - dy), 168, 169, 1, 1);
    si = (s16)(dx + GAUGE(0xCC80));
    gfx_copy_rect((s16)(si - 0x20), (s16)(GAUGE(0xCC86) + si - 0x21), (s16)(0x98 - dy),
                  (s16)(GAUGE(0xCC82) - dy), 264, 199, 1, 1);
}

/* 0792:155e steer_marker_draw — hud.md §4.4 (current page; every caller has page 0 set) */
void steer_marker_draw(void)
{
    polar(hud_marker_angle(DSB(DS_steer_wheel)), MARKER(0xE780));
    s16 x = (s16)(MARKER(0xE784) + POLAR_X);
    s16 y = (s16)(MARKER(0xE786) - POLAR_Y);
    gfx_move_to(x, (s16)(y + 4));
    gfx_set_colour((s16)MARKER(0xE77E));           /* pushed as a word; the library keeps the low byte */
    gfx_draw_bitmap(DS_marker_mask, 1, 5);
    DSB(DS_marker_pos) = DSB(DS_steer_wheel);
}

/* 0792:15d4 tach_draw — hud.md §4.5 */
void tach_draw(void)
{
    if ((u16)(GAUGE(0xCC68) + GAUGE(0xCC6A)) == 0) return;       /* car without tach */
    if (DSB(DS_tach_step) == DSB(DS_tach_drawn)) return;
    hud_page(0);
    gfx_copy_rect(168, 207, 104, 136, GAUGE(0xCC7C), GAUGE(0xCC7E), 1, 0);
    gfx_copy_rect(168, (s16)(GAUGE(0xCC84) + 0x7F), 137, 169, (s16)(GAUGE(0xCC7C) + 40), GAUGE(0xCC7E), 1, 0);
    gfx_set_colour(CCOL(0xCEB4));
    gfx_move_to(GAUGE(0xCC68), GAUGE(0xCC6A));
    u16 ang = (u16)((u16)((u16)GAUGE(0xCC66) * DSB(DS_tach_step)) - (u16)(0x500 * (u16)GAUGE(0xCC8A)));
    polar(ang, (u16)GAUGE(0xCC64));
    gfx_line_to((s16)(GAUGE(0xCC68) + POLAR_X), (s16)(GAUGE(0xCC6A) - POLAR_Y));
    DSB(DS_tach_drawn) = DSB(DS_tach_step);
    steer_marker_draw();
}

/* 0792:16c4 odometer_draw — hud.md §4.6 (odometer at CC78/CC7A in page-1 speedo-backing coordinates) */
void odometer_draw(void)
{
    if ((u16)(GAUGE(0xCC70) + GAUGE(0xCC72)) == 0) return;       /* car without speedo */
    if (DSB(DS_odo_sixtieths) != DSB(DS_odo_drawn)) {
        DSB(DS_odo_roll_tens) = 0;
        DSB(DS_odo_roll_units) = 0;
        if (DSB(DS_odo_sixtieths) > 0x36) {
            DSB(DS_odo_roll_units) = (u8)(DSB(DS_odo_sixtieths) - 0x36);
            u16 qr = div16_8(DSB(DS_odo_units), 10);             /* div cl: AH = remainder */
            if ((qr >> 8) == 9) DSB(DS_odo_roll_tens) = DSB(DS_odo_roll_units);
        }
        s16 x = GAUGE(0xCC78), y = GAUGE(0xCC7A);
        if (y != 0) {
            hud_page(1);
            gfx_set_colour(CCOL(0xCEB3));                        /* digit background */
            gfx_fill_rect(x, (s16)(x + 7), (s16)(y - 4), y);
            gfx_fill_rect((s16)(x + 10), (s16)(x + 13), (s16)(y - 4), y);
            gfx_set_colour(CCOL(0xCEB2));                        /* digits */
            u16 qr = div16_8(DSB(DS_odo_units), 10);
            gfx_move_to(x, y);
            gfx_draw_bitmap((u16)(DS_odo_digit_strip + (qr & 0xFF) * 6 + DSB(DS_odo_roll_tens)), 1, 5);
            gfx_move_to((s16)(x + 4), y);
            qr = div16_8(DSB(DS_odo_units), 10);
            gfx_draw_bitmap((u16)(DS_odo_digit_strip + (qr >> 8) * 6 + DSB(DS_odo_roll_units)), 1, 5);
            gfx_move_to((s16)(x + 10), y);
            gfx_draw_bitmap((u16)(DS_odo_digit_strip + DSB(DS_odo_sixtieths)), 1, 5);
            gfx_put_pixel((s16)(x + 8), y);                     /* decimal point */
            if (DSB(DS_speedo_step) == DSB(DS_speedo_drawn)) {  /* needle not redrawn below */
                gfx_copy_rect(x, (s16)(x + 13), (s16)(y - 4), y,
                              (s16)(x + GAUGE(0xCC80) - 0x108), (s16)(y + GAUGE(0xCC82) - 0xC7), 1, 0);
                speedo_draw();
                steer_marker_draw();
            }
        }
        DSB(DS_odo_drawn) = DSB(DS_odo_sixtieths);
    }
    if (DSB(DS_speedo_step) != DSB(DS_speedo_drawn)) {
        gfx_copy_rect(264, (s16)(GAUGE(0xCC86) + 0x107), 167, 199, GAUGE(0xCC80), GAUGE(0xCC82), 1, 0);
        speedo_draw();
        steer_marker_draw();
        DSB(DS_speedo_drawn) = DSB(DS_speedo_step);
    }
}

/* 0792:18da speedo_draw — hud.md §4.5 */
void speedo_draw(void)
{
    hud_page(0);
    gfx_set_colour(CCOL(0xCEB5));
    gfx_move_to(GAUGE(0xCC70), GAUGE(0xCC72));
    u16 ang = (u16)((u16)((u16)GAUGE(0xCC6E) * DSB(DS_speedo_step)) - (u16)(0x500 * (u16)GAUGE(0xCC88)));
    polar(ang, (u16)GAUGE(0xCC6C));
    gfx_line_to((s16)(GAUGE(0xCC70) + POLAR_X), (s16)(GAUGE(0xCC72) - POLAR_Y));
}

/* 0792:1952 odometer_update — hud.md §4.7 */
void odometer_update(void)
{
    u32 v = DSL(DS_speed_long);
    if ((s32)v < 0) v = (u32)0 - v;                  /* neg/adc/neg (80000000h stays itself) */
    DSL(DS_odo_accum) += v;
    while (DSL(DS_odo_accum) >= DSL(DS_car_odo_step)) {
        DSL(DS_odo_accum) -= DSL(DS_car_odo_step);
        u8 old = DSB(DS_odo_sixtieths)++;
        if (old < 0x3C) continue;
        if (DSB(DS_odo_units) < 0x63) DSB(DS_odo_units)++;
        DSB(DS_odo_sixtieths) = (u8)(DSB(DS_odo_sixtieths) - 0x3C);
    }
}

/* 0792:1c66 cockpit_pictures_draw — hud.md §4.3 */
void cockpit_pictures_draw(s16 bottom_y)
{
    mouse_set_pos(160, 100);
    DSB(DS_colour_offset) = 0x80;
    hud_load_pic(0x11E9 /* "2.BOT" */);
    pic_draw(DS_sprite_set, PICPAIRS(0x0B5E), 320, 0, bottom_y, 0);        /* current page */
    hud_load_pic(0x11EF /* "1.BOT" */);
    pic_draw(DS_sprite_set, PICPAIRS(0x0B5C), 320, 0, (s16)(bottom_y - 0x2C), 1);
    if (bottom_y == 0xC7) {
        hud_load_pic(0x11F5 /* ".TOP" */);
        pic_draw(DS_sprite_set, PICPAIRS(0x0B5A), 320, 0, 15, 0);
        gfx_copy_rect(0, 319, 0, 199, 0, 199, 1, 0);                        /* page 1 -> screen */
    }
    hud_page(1);
    lzw_decode(ds_far(DS_compass_pic), ds_ptr(DS_sprite_set));
    /* PORT: the original draws 180h pairs here; COMPASS.LZ has 17Ah and the 6 extra (stale) pairs of
     * the unpack buffer were written above row 0 of page 1 (hud.md §9.1). Draw 17Ah as chase_view_exit
     * does. */
    pic_draw(DS_sprite_set, 0x17A, 152, 0, 7, 0);
    if (bottom_y != 0xC7) load_opponent_pob_nonvga();
}

/* 0792:1dfe load_opponent_pob_nonvga — hud.md §2.1 (no-op on VGA) */
void load_opponent_pob_nonvga(void)
{
    if (DSW(DS_video_mode) == 0x13) return;
    if (DSB(DS_protection_ok) == 0 || DSB(DS_race_computer_cars) == 0 || DSB(DS_menu_preview) != 0) return;
    /* PORT: the non-VGA path (re-load the opponent .POB into DS:CEBC through 0000:0e74, because the
     * EGA/Tandy unpack buffer overlaps it) is not ported: the port always runs in mode 13h. */
}
