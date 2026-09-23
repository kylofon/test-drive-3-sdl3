/* Game flow (game_flow.md §4.8-4.11): the main select screen with the rotating car, the driver options,
 * car select and course select. Segment 01f4. */
#include "game/game.h"

/* PORT: the main menu renders its 3D preview unpaced in the original; game_flow.md §7 recommends the race
 * loop's rule with the original constant: at least 5 timer ticks per loop (~4.4 s per turn of the car). */
#define MENU_FRAME_TICKS 5

/* 01f4:144c main_menu — game_flow.md §4.8 (returns the chosen item, also in DS:E5AC) */
s16 main_menu(void)
{
    u16 back_to = 1, shown = 0xff, k;

    playdisk_verify();
    DSB(DS_menu_preview) = 1;
    DSB(DS_external_panel_on) = 1;
    DSB(DS_skip_enabled) = 0;
    if (flow_vga()) {
        pal_fade_out_01F4_1C72();
        flow_page(0);
        gfx_set_colour(0);
        gfx_fill_rect(0, 0x13f, 0, 199);
    }
    DSB(DS_colour_offset) = 0x80;
    flow_strcpy(DS_path_playdisk + 2, 0x0F83);                 /* "SELCOLR.BIN" */
    pal_load(DS_path_playdisk);
    flow_strcpy(DS_path_car + 7, 0x0F8F);                      /* "SIC.BIN" -> A:CCERVSIC.BIN */
    pal_load_32(DS_path_car);
    DSB(DS_colour_offset) = 0;
    flow_strcpy(DS_path_playdisk + 2, 0x0F97);                 /* "OTWCOL.BIN" */
    pal_load(DS_path_playdisk);
    pal_black_01F4_1C56();
    DSB(DS_colour_offset) = 0x80;
    DSB(DS_skip_flag) = 0;
    flow_page(1);
    lzw_decode(ds_far(DS_select_pic), ds_ptr(DS_sprite_set));
    pic_draw(DS_sprite_set, 0x17cc, 0x140, 0, 199, 0);
    text_set_colours(0x0f, 0);
    text_goto(0x0f, 0x0d);
    DSB(DS_car_name + 0x12) = 0x80;
    print_text(DS_car_name, 0);
    DSB(DS_car_name + 0x12) = 0;
    flow_strcpy(DS_path_car + 7, 0x0FA2);                      /* ".SIC" */
    flow_load_decode(DS_path_car);
    pic_draw(DS_sprite_set, DSW(DS_car_pic_pairs + 2), 0x48, 0x54, 0xc5, 0);
    flow_strcpy(DS_path_scene + 2, (u16)(DSW(DS_scene_index) * 8 + DS_scene_slots));
    flow_strcpy(DS_path_scene + 9, 0x0FA7);                    /* ".SIC" */
    flow_load_decode(DS_path_scene);
    pic_draw(DS_sprite_set, DSW(DS_scene_pic_pairs + 2), 0x48, 0xa4, 0xc2, 0);
    gfx_copy_rect(0, 0x13f, 0, 199, 0, 199, 1, 0);
    flow_page(0);
    menu_box_draw((s16)DSW(DS_menu_choice), 0x0f, 0x0f);
    DSB(DS_colour_offset) = 0;

    /* rotating car preview (the engine runs with menu_preview = 1) */
    stage_load_objects();
    car_leg_reset();
    hud_reset_topbar();
    DSB(DS_ext_view) = 1;
    DSB(DS_cam_moved) = 1;
    DSW(DS_cam_pitch) = 0xfff0;
    DSB(DS_cam_yaw) = 0x80;
    DSB(0x94AD) = 0;                                           /* camera roll */
    DSB(DS_cam_elev) = 0x24;
    DSW(0x94A7) = 0x140;                                       /* camera y */
    DSW(DS_cam_dist) = 0x3e;
    DSW(DS_view_top_row) = 0x16;
    gfx_set_display_offset(0, 0);
    flow_page(0);
    frame_update();
    frame_draw();
    view_present();
    pal_apply();
    DSB(DS_kbd_key) = 0;
    for (;;) {
        u16 frame_start = DSW(DS_tick_count);                 /* PORT: pacing, see MENU_FRAME_TICKS */
        s16 col;
        if ((s16)DSW(DS_menu_choice) > 0x7f) {
            DSW(DS_menu_choice) &= 0x7f;
            if (DSW(DS_menu_choice) == 0) {
                DSB(DS_skip_flag) = 0;
                DSB(DS_kbd_key) = 0;
            }
            DSB(DS_menu_preview) = 0;
            pal_fade_out_01F4_1C72();
            flow_page(0);
            if (flow_vga()) {
                gfx_set_colour(0);
                gfx_fill_rect(0, 0x13f, 0, 199);
            }
            pal_apply();
            return (s16)DSW(DS_menu_choice);
        }
        frame_update();
        frame_draw();
        view_present();
        DSB(DS_obj_heading) = (u8)(DSB(DS_obj_heading) + 2);   /* object 0 heading, low byte += 2 */
        col = (DSW(DS_frame_counter) & 1) == 0 ? 0x0f : 7;     /* blink */
        menu_box_draw((s16)DSW(DS_menu_choice), col, col);
        if (shown != DSW(DS_menu_choice)) {
            menu_box_draw((s16)shown, 9, 0);
            menu_box_draw((s16)DSW(DS_menu_choice), 0x0f, 0x0f);
            shown = DSW(DS_menu_choice);
        }
        key_poll(&k);
        if (k != 0) {
            if (k == 0x92) {                                   /* Up: the race box */
                if (DSW(DS_menu_choice) != 0) back_to = DSW(DS_menu_choice);
                DSW(DS_menu_choice) = 0;
            } else if (k == 0x94) {                            /* Left */
                if (DSW(DS_menu_choice) == 0) DSW(DS_menu_choice) = 4;
                else DSW(DS_menu_choice)--;
            } else if (k == 0x96) {                            /* Right */
                if (DSW(DS_menu_choice) == 4) DSW(DS_menu_choice) = 0;
                else DSW(DS_menu_choice)++;
            } else if (k == 0x98 && DSW(DS_menu_choice) == 0) {   /* Down */
                DSW(DS_menu_choice) = back_to;
            }
        }
        if (k == 0x0d) DSW(DS_menu_choice) = (u16)(DSW(DS_menu_choice) + 0x80);
        flow_page(0);
        while ((u16)(DSW(DS_tick_count) - frame_start) < MENU_FRAME_TICKS) host_pump();   /* PORT */
    }
}

/* 01f4:18b0 menu_box_draw — game_flow.md §4.8 */
void menu_box_draw(s16 item, s16 c, s16 c2)
{
    s16 x = (s16)(item * 0x50 - 0x50);
    gfx_set_colour(c);
    if (item == 0) gfx_frame(0x1f, 0x120, 0x1a, 0x71);
    else if (item > 0 && item < 5) gfx_frame((s16)(x + 3), (s16)(x + 0x4e), 0x8c, 0xc6);
    gfx_set_colour(c2);
    if (item == 0) gfx_frame(0x1e, 0x121, 0x19, 0x72);
    else if (item > 0 && item < 5) gfx_frame((s16)(x + 2), (s16)(x + 0x4f), 0x8b, 199);
    if (c2 != 0) return;
    gfx_set_colour(1);                                         /* shadow */
    if (item == 0) {
        gfx_move_to(0x121, 0x1b);
        gfx_line_to(0x121, 0x72);
        gfx_line_to(0x20, 0x72);
    } else {
        if (item < 1 || item > 4) return;
        gfx_move_to((s16)(x + 0x4f), 0x8d);
        gfx_line_to((s16)(x + 0x4f), 199);
        gfx_line_to((s16)(x + 4), 199);
    }
}

/* 01f4:3b2e options_screen — game_flow.md §4.9 (skill level, then opponents) */
void options_screen(void)
{
    u16 old_skill = DSW(DS_skill_level);
    u16 old_p = DSB(DS_race_players), old_c = DSB(DS_race_computer_cars);
    u16 shown_opp = 0xff, shown_skill = 0xff, k;
    s16 done;

    DSB(DS_colour_offset) = 0;
    flow_strcpy(DS_path_playdisk + 2, 0x1092);                 /* "DIFFCOLR.BIN" */
    pal_load(DS_path_playdisk);
    flow_page(1);
    lzw_decode(ds_far(DS_difflevc_pic), ds_ptr(DS_sprite_set));
    pic_draw(DS_sprite_set, 0x2bf9, 0x140, 0, 199, 0);
    lzw_decode(ds_far(DS_difflevb_pic), ds_ptr(DS_sprite_set));
    pic_draw(DS_sprite_set, 0x27ba, 0x140, 0, 0x86, 1);
    lzw_decode(ds_far(DS_diffleva_pic), ds_ptr(DS_sprite_set));
    pic_draw(DS_sprite_set, 0x2e17, 0x140, 0, 0x45, 1);
    gfx_set_colour(flow_vga() ? 0x13 : 0);
    gfx_fill_rect(0, 0x13f, 0, 0x0d);
    gfx_set_colour(8);
    gfx_fill_rect(1, 0x13e, 1, 0x0c);
    gfx_set_colour(7);
    gfx_fill_rect(2, 0x13d, 2, 0x0b);
    flow_page(0);
    screen_present();
    text_set_colours(0, 7);
    text_goto(3, 1);
    print_text(0x1E25, 0);                                     /* "Driver Options: Skill Level -" */
    DSB(DS_kbd_key) = 0;

    for (done = 0; done == 0; ) {                              /* phase 1: skill level */
        bios_wait_ticks(2);
        key_poll(&k);
        if (shown_skill != DSW(DS_skill_level)) {
            flow_strcpy(0x1E45, (s16)DSW(DS_skill_level) < 3 ? 0x109F : 0x10AC);   /* "(auto-shift)" / blanks */
            DSB(DS_options_skill_text) = (u8)(DSW(DS_skill_level) + '1');
            DSB(0x1E51) = 0xaa;
            text_goto(3, 0x19);
            print_text(DS_options_skill_text, 0);
            shown_skill = DSW(DS_skill_level);
        }
        if (k == '=' || k == '+' || k == 0x92 || k == 0x96) {
            if ((s16)DSW(DS_skill_level) < 8) DSW(DS_skill_level)++;
        } else if (k == '-' || k == '_' || k == 0x94 || k == 0x98) {
            if ((s16)DSW(DS_skill_level) > 0) DSW(DS_skill_level)--;
        } else if (k > '0' && k < ':') {
            DSW(DS_skill_level) = (u16)(k - '1');
        } else if (k == 0x0d) {
            done = 1;
        }
    }

    flow_page(0);
    text_goto(3, 1);
    print_text(0x1E52, 0);                                     /* "Driver Options: Race Against-" */
    DSB(DS_kbd_key) = 0;
    for (done = 0; ; ) {                                       /* phase 2: opponents */
        u16 opp;
        if (done != 0) {
            if (old_skill != DSW(DS_skill_level)
                || (u16)(DSB(DS_race_players) - DSB(DS_race_computer_cars)) != (u16)(old_p - old_c))
                playdisk_write();
            return;
        }
        bios_wait_ticks(2);
        key_poll(&k);
        opp = (u16)(DSB(DS_race_players) - DSB(DS_race_computer_cars));
        if (opp != shown_opp) {
            if (DSB(DS_race_computer_cars) != 0) {
                flow_strcpy(DS_options_opponent_text, 0x10B9);                 /* "Computer Cars " */
            } else if (DSB(DS_race_players) == 1) {
                flow_strcpy(DS_options_opponent_text, 0x10C8);                 /* "Clock         " */
            } else {
                DSB(DS_options_opponent_text) = (u8)(DSB(DS_race_players) + 0x2f);   /* number of OTHER players */
                flow_strcpy(DS_options_opponent_text + 1,
                            DSB(DS_race_players) == 2 ? 0x10D7 : 0x10E5);     /* " Other Person" / " Other People" */
            }
            DSB(0x1E8C) = 0xaa;
            text_goto(3, 0x1a);
            print_text(DS_options_opponent_text, 0);
            shown_opp = (u16)(DSB(DS_race_players) - DSB(DS_race_computer_cars));
        }
        if (k == 'c' || k == 'C') {
            DSB(DS_race_computer_cars) = 0;
            DSB(DS_race_players) = 1;
        } else if (k == 'p' || k == 'P') {
            DSB(DS_race_computer_cars) = 1;
            DSB(DS_race_players) = 1;
        } else if (k >= '0' && k < '4') {
            DSB(DS_race_computer_cars) = 0;
            DSB(DS_race_players) = (u8)(k - 0x2f);
        } else if (k == 0x92 || k == 0x96) {
            if (DSB(DS_race_computer_cars) == 0) {
                if (DSB(DS_race_players) < 4) DSB(DS_race_players)++;
            } else {
                DSB(DS_race_computer_cars) = 0;
            }
        } else if (k == 0x94 || k == 0x98) {
            if (DSB(DS_race_players) == 1) DSB(DS_race_computer_cars) = 1;
            else if (DSB(DS_race_players) > 1) DSB(DS_race_players)--;
        } else if (k == 0x0d) {
            done = 1;
        }
    }
}

/* 01f4:310c car_select — game_flow.md §4.10 (the spec card scrolls through pages 0 and 1) */
void car_select(void)
{
    s16 sel = (s16)DSW(DS_car_index), orig = (s16)DSW(DS_car_index);
    s16 done = 0;
    u16 k;

    car_select_draw();
    DSB(DS_kbd_key) = 0;
    while (done == 0) {
        u8 h;
        if (((DSB(DS_kbd_bits) | DSB(DS_joy_bits)) & 0x0f) == 9) {    /* scroll back */
            for (s16 y = 199; y > 0; y--)
                gfx_copy_rect(0x70, 0x13f, (s16)(y - 1), (s16)(y - 1), 0x70, y, 1, 1);
            gfx_copy_rect(0x70, 0x13f, 0xb8, 0xb8, 0x70, 0, 0, 1);
            for (s16 y = 0xb8; y > 0x70; y--)
                gfx_copy_rect(0x70, 0x13f, (s16)(y - 1), (s16)(y - 1), 0x70, y, 0, 0);
            gfx_copy_rect(0x70, 0x13f, 199, 199, 0x70, 0x70, 1, 0);
        } else {                                                        /* scroll forward, one row */
            gfx_copy_rect(0x70, 0x13f, 0x70, 0x70, 0x70, 199, 0, 1);
            for (s16 y = 0x70; y < 0xb8; y++)
                gfx_copy_rect(0x70, 0x13f, (s16)(y + 1), (s16)(y + 1), 0x70, y, 0, 0);
            gfx_copy_rect(0x70, 0x13f, 0, 0, 0x70, 0xb8, 1, 0);
            for (s16 y = 0; y < 199; y++)
                gfx_copy_rect(0x70, 0x13f, (s16)(y + 1), (s16)(y + 1), 0x70, y, 1, 1);
        }
        h = (u8)((DSB(DS_kbd_bits) | DSB(DS_joy_bits)) & 0x0f);
        if (h != 9 && h != 10) bios_wait_ticks(6);
        else host_pump();                                               /* PORT: unpaced in the original */
        key_poll(&k);
        if (k != 0) {
            if (k == 0x0d) done = 1;
            else if (k == 0x92) sel = car_prev(sel);
            else if (k == 0x98) sel = car_next(sel);
            if (sel != (s16)DSW(DS_car_index)) {
                DSW(DS_car_index) = (u16)sel;
                car_select_draw();
                DSB(DS_kbd_key) = 0;
            }
        }
    }
    if (orig != (s16)DSW(DS_car_index)) playdisk_write();
}

/* 01f4:3332 car_next — game_flow.md §4.10 (verified: only the next slot is tried) */
s16 car_next(s16 s)
{
    s16 orig = s;
    s = (s == DSB(DS_cars_on_disk) - 1) ? 0 : (s16)(s + 1);
    do {
        if (car_lst_load(s, DS_path_car, DS_car_name, DS_car_pic_pairs) == 0) return s;
        s = orig;
    } while (orig == 0);
    return orig;
}

/* 01f4:3396 car_prev — game_flow.md §4.10 */
s16 car_prev(s16 s)
{
    s16 orig = s;
    s = (s == 0) ? (s16)(DSB(DS_cars_on_disk) - 1) : (s16)(s - 1);
    do {
        if (car_lst_load(s, DS_path_car, DS_car_name, DS_car_pic_pairs) == 0) return s;
        s = orig;
    } while (orig == 0);
    return orig;
}

/* 01f4:33f8 car_select_draw — game_flow.md §4.10 */
void car_select_draw(void)
{
    DSB(DS_colour_offset) ^= 0x80;
    flow_strcpy(DS_path_car + 7, 0x1057);                      /* "SC.BIN" */
    pal_load(DS_path_car);
    flow_page(1);
    gfx_set_colour(0x0f);
    gfx_fill_rect(0, 0x6f, 0x53, 0x53);                        /* divider line */
    flow_strcpy(DS_path_car + 2, (u16)(DSW(DS_car_index) * 6 + DS_car_slots));
    flow_strcpy(DS_path_car + 7, 0x105E);                      /* "FL1.LZ" */
    flow_load_decode(DS_path_car);
    pic_draw(DS_sprite_set, DSW(DS_car_pic_pairs + 0x16), 0xd0, 0x70, 199, 0);
    flow_strcpy(DS_path_car + 7, 0x1065);                      /* ".BIC" */
    flow_load_decode(DS_path_car);
    pic_draw(DS_sprite_set, DSW(DS_car_pic_pairs + 6), 0x70, 0, 0x52, 0);
    flow_strcpy(DS_path_car + 7, 0x106A);                      /* ".SID" */
    flow_load_decode(DS_path_car);
    pic_draw(DS_sprite_set, DSW(DS_car_pic_pairs + 8), 0x70, 0, 199, 0);
    flow_strcpy(DS_path_car + 7, 0x106F);                      /* ".ICN" */
    flow_load_decode(DS_path_car);
    pic_draw(DS_sprite_set, DSW(DS_car_pic_pairs), 0xd0, 0x70, 0x52, 0);
    screen_present();
    flow_page(1);
    flow_strcpy(DS_path_car + 7, 0x1074);                      /* "FL2.LZ" */
    flow_load_decode(DS_path_car);
    pic_draw(DS_sprite_set, DSW(DS_car_pic_pairs + 0x18), 0xd0, 0x70, 199, 0);
    flow_page(0);
}

/* 01f4:3636 scene_select — game_flow.md §4.11 */
void scene_select(void)
{
    s16 sel, orig, done = 0;
    u16 k;

    DSB(DS_radio_station) = 0;
    sel = (s16)DSW(DS_scene_index);
    orig = (s16)DSW(DS_scene_index);
    scene_select_draw();
    DSB(DS_kbd_key) = 0;
    while (done == 0) {
        bios_wait_ticks(4);
        key_poll(&k);
        if (k != 0) {
            if (k == 0x0d) done = 1;
            else if (k == 0x92) sel = scene_prev(sel);
            else if (k == 0x98) sel = scene_next(sel);
            if (sel != (s16)DSW(DS_scene_index)) {
                DSW(DS_scene_index) = (u16)sel;
                scene_select_draw();
                DSB(DS_kbd_key) = 0;
            }
        }
    }
    if (orig != (s16)DSW(DS_scene_index)) {
        playdisk_write();
        DSB(DS_protection_ok) = 0;                             /* the scenery disk has its own code sheet */
    }
}

/* 01f4:36de scene_next — game_flow.md §4.11 */
s16 scene_next(s16 s)
{
    s16 orig = s;
    s = (s == DSB(DS_scenes_on_disk) - 1) ? 0 : (s16)(s + 1);
    do {
        if (scene_lst_load(s, DS_path_scene, DS_scene_name) == 0) return s;
        s = orig;
    } while (orig == 0);
    return orig;
}

/* 01f4:373e scene_prev — game_flow.md §4.11 */
s16 scene_prev(s16 s)
{
    s16 orig = s;
    s = (s == 0) ? (s16)(DSB(DS_scenes_on_disk) - 1) : (s16)(s - 1);
    do {
        if (scene_lst_load(s, DS_path_scene, DS_scene_name) == 0) return s;
        s = orig;
    } while (orig == 0);
    return orig;
}

/* 01f4:379c scene_select_draw — game_flow.md §4.11 (banner, first and last leg pictures) */
void scene_select_draw(void)
{
    u16 last = (u16)(DSB(DS_leg_count) * 6);                   /* entry of leg leg_count-1 at 0B14/16/18 + 6n */

    DSB(DS_colour_offset) = 0;
    flow_page(1);
    flow_strcpy(DS_path_scene + 2, (u16)(DSW(DS_scene_index) * 8 + DS_scene_slots));
    flow_strcpy(DS_path_scene + 9, 0x107B);                    /* ".ICN" */
    flow_load_decode(DS_path_scene);
    pal_black_01F4_1C56();
    if (flow_vga()) {
        flow_page(0);
        gfx_set_colour(0);
        gfx_fill_rect(0, 0x13f, 0, 199);
    }
    flow_page(1);
    gfx_set_colour(0);
    gfx_fill_rect(0, 0x13f, 0, 199);
    pic_draw(DS_sprite_set, DSW(DS_scene_pic_pairs), 0x140, 0, 0x20, 0);   /* banner */
    /* first leg: "<scene><d>.COL" palette at 16.., .BLZ, .ALZ */
    flow_strcpy(DS_path_scene + 10, 0x1080);                   /* ".COL" */
    DSB(DS_path_scene + 9) = DSB(0x0B1E);
    pal_load(DS_path_scene);
    flow_strcpy(DS_path_scene + 11, 0x1085);                   /* "BLZ" */
    flow_load_decode(DS_path_scene);
    pic_draw(DS_sprite_set, DSW(0x0B1C), 0x140, 0, 0x6b, 0);
    DSB(DS_path_scene + 11) = 'A';
    flow_load_decode(DS_path_scene);
    pic_draw(DS_sprite_set, DSW(0x0B1A), 0x140, 0, 0x58, 1);
    gfx_set_colour(8);
    gfx_frame(0, 0x13f, 0x26, 0x6b);
    /* last leg: palette at 144.. */
    DSB(DS_colour_offset) = 0x80;
    flow_strcpy(DS_path_scene + 10, 0x1089);                   /* ".COL" */
    DSB(DS_path_scene + 9) = DSB((u16)(last + 0x0B18));
    pal_load(DS_path_scene);
    flow_strcpy(DS_path_scene + 11, 0x108E);                   /* "BLZ" */
    flow_load_decode(DS_path_scene);
    pic_draw(DS_sprite_set, DSW((u16)(last + 0x0B16)), 0x140, 0, 0xb6, 0);
    DSB(DS_path_scene + 11) = 'A';
    flow_load_decode(DS_path_scene);
    pic_draw(DS_sprite_set, DSW((u16)(last + 0x0B14)), 0x140, 0, 0xa3, 1);
    gfx_set_colour(8);
    gfx_frame(0, 0x13f, 0x71, 0xb6);
    gfx_set_colour(8);
    gfx_fill_rect(0, 0x13f, 0xbc, 199);
    gfx_set_colour(7);
    gfx_fill_rect(1, 0x13e, 0xbd, 0xc6);
    lzw_decode(ds_far(DS_ssbj_pic), ds_ptr(DS_sprite_set));
    pic_draw(DS_sprite_set, 0x11d, 0x70, 0x70, 0xc6, 0);      /* SSBJ.LZ */
    flow_page(0);
    pal_black_01F4_1C56();
    gfx_copy_rect_from_copy_page(0, 0x13f, 0, 199);            /* page 1 -> screen */
    pal_fade_in_01F4_1C64();
}
