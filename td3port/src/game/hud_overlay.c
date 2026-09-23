/* HUD overlays (hud.md §4.10, §4.11): chase-car / instant-replay panel, broken windscreen, water roll. */
#include "game/game.h"

static void hud_page(s16 p)
{
    DSW(DS_page_cur) = (u16)p;
    gfx_set_draw_page(p);
}

/* 0792:04ca chase_view_exit — hud.md §4.10 */
void chase_view_exit(void)
{
    DSB(DS_mirror_dirty) = 1;                                    /* DS:B6D1 */
    music_for_state(DSB(DS_radio_station));
    gfx_copy_rect(0, 319, 0, 15, 0, 15, 1, 0);                   /* restore the top bar */
    hud_page(1);
    lzw_decode(ds_far(DS_compass_pic), ds_ptr(DS_sprite_set));
    pic_draw(DS_sprite_set, 0x17A, 152, 0, 7, 0);                /* compass strip back into page 1 */
    load_opponent_pob_nonvga();
    hud_page(0);
    gfx_copy_rect(0, 319, 16, 103, 0, 199, 1, 0);                /* centre-rim cockpit back on screen */
    DSB(DS_marker_pos) = 0xFF;
    DSB(DS_speedo_drawn) = 0xFF;
    DSB(DS_tach_drawn) = 0xFF;
    DSB(DS_odo_drawn) = 0xFF;
    DSB(DS_compass_drawn) = 0xFF;
    DSB(DS_rim_shown) = 0xFF;
    DSB(DS_look_rect_dirty) = 0;                                 /* DS:CC52 lever_shown */
    DSB(DS_external_panel_on) = 0;                               /* DS:CC92 panel_shown */
}

/* 0792:059a chase_view_enter — hud.md §4.10 */
void chase_view_enter(void)
{
    music_stop(0);
    gfx_copy_rect(0, 319, 0, 15, 0, 15, 0, 1);                   /* save the top bar */
    hud_page(0);
    gfx_set_colour(0);
    gfx_fill_rect(0, 319, 0, 15);
    replay_panel_draw();
    DSB(DS_external_panel_on) = 1;
}

/* 0792:0602 replay_panel_draw — hud.md §4.10 */
void replay_panel_draw(void)
{
    s16 s;
    DSB(DS_text_transparent) = 1;                                /* DS:90E0 */
    hud_page(0);
    lzw_decode(ds_far(DS_chase_pic), ds_ptr(DS_sprite_set));
    ega_pal_black();
    DSB(DS_colour_offset) = 0;
    gfx_set_colour(8);    gfx_fill_rect(0, 319, 112, 199);
    gfx_set_colour(0x0F); gfx_frame(0, 319, 112, 199);
    gfx_set_colour(7);    gfx_frame(1, 318, 113, 198);
    gfx_set_colour(0);    gfx_frame(2, 317, 114, 197);
    pic_draw(DS_sprite_set, 0x573, 248, 56, 195, 0);             /* CHASE.LZ buttons */
    load_opponent_pob_nonvga();
    DSW(DS_last_cell) = 0xFFFF;                                  /* DS:BD34 */
    text_set_colours(0x0C, 0);
    if (DSB(DS_replay_state) != 0 || DSB(DS_crashed) != 0) {     /* instant replay */
        s = print_records(0x1EF8, 0);                            /* "INSTANT REPLAY" */
        text_set_colours(0, 7);
        s = print_records(0x1EF8, s);                            /* "return", "pause", "replay" */
        if (DSB(DS_crashed) == 0) {
            gfx_set_colour(8);
            gfx_fill_rect(56, 129, 180, 195);                    /* hide F5 */
        }
    } else {                                                     /* F5 chase-car view */
        gfx_set_colour(8);
        gfx_fill_rect(130, 299, 180, 195);                       /* hide F9 / F10 */
        s = print_records(0x1EDE, 0);                            /* "CHASE CAR VIEW" */
        text_set_colours(0, 7);
        s = print_records(0x1EDE, s);                            /* "return" */
    }
    text_set_colours(0, 8);
    s = print_records(0x1F23, 0);                                /* "Viewpoint Control:" */
    text_set_colours(0, 7);
    s = print_records(0x1F23, s);                                /* the labels */
    (void)s;
    ega_pal_restore();
    if (DSB(DS_replay_state) != 0 && DSB(DS_crashed) == 0) message_box(0x17);   /* "Instant Replay" */
    DSB(DS_text_transparent) = 0;
}

/* 0792:19ca water_overlay_start — hud.md §4.11 */
void water_overlay_start(void)
{
    hud_page(1);
    lzw_decode(ds_far(DS_water_pic), ds_ptr(DS_sprite_set));
    DSB(DS_colour_offset) = 0;
    pic_draw(DS_sprite_set, DSW(DS_video_mode) == 0x13 ? 0x1729 : 0x12EA, 160, 0, 95, 0);
    sfx_play(6);
    gfx_copy_rect(0, 159, 0, 95, 160, 95, 1, 1);                 /* second copy at x 160..319 */
    water_roll_step();
    hud_page(0);
    load_opponent_pob_nonvga();
}

/* 0792:1a72 water_roll_step — hud.md §4.11 */
void water_roll_step(void)
{
    if (DSB(DS_half_window) != 0) {                              /* view x 40..279, rows 16..79 */
        gfx_copy_rect(40, 168, 0, 15, 40, 31, 1, 0);             /* x1 = 168 as coded */
        gfx_copy_rect(256, 279, 0, 15, 256, 31, 1, 0);
        gfx_copy_rect(40, 279, 16, 63, 40, 79, 1, 0);
        gfx_copy_rect(40, 279, 0, 61, 40, 63, 1, 1);             /* rotate down 2 rows */
        gfx_copy_rect(40, 279, 78, 79, 40, 1, 0, 1);
        bios_wait_ticks(1);
        return;
    }
    gfx_copy_rect(0, 167, 0, 15, 0, 31, 1, 0);
    gfx_copy_rect(256, 319, 0, 15, 256, 31, 1, 0);               /* x 168..255 of rows 16..31: mirror */
    gfx_copy_rect(0, 319, 16, 95, 0, 111, 1, 0);
    gfx_copy_rect(0, 319, 0, 93, 0, 95, 1, 1);
    gfx_copy_rect(0, 319, 110, 111, 0, 1, 0, 1);
    /* PORT: the full-window roll runs unpaced in the original (40 steps as fast as the machine copies
     * ~60 KB each); pace it like the half-window path, 1 BIOS tick per step (hud.md §9.5). */
    bios_wait_ticks(1);
}

/* 0792:1c00 broken_glass_overlay — hud.md §4.11 */
void broken_glass_overlay(void)
{
    lzw_decode(ds_far(DS_broke_pic), ds_ptr(DS_sprite_set));
    sfx_play(2);
    hud_page(0);
    rle_draw_viewbuf(DS_sprite_set, DSW(DS_video_mode) == 0x13 ? 0x23C2 : 0x19E1, 95);
    mirror_frame_draw();                                         /* 0e12:7c21 */
    DSW(DS_top_row) = 0;                                         /* DS:BAD4 */
    view_present();
}
