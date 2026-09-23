/* Game flow (game_flow.md §4.16-4.18, descriptions.md .HI): the computer cars' results, the leg banner,
 * scorecard and race status pages, the record digits, the top-score entry and table. Segment 01f4. */
#include "game/game.h"

#define REC(p) ((u16)(DS_player_recs + (p) * 0x1b))    /* 27-byte player record p (0-3) */

/* 01f4:0a96 top_score_enter — game_flow.md §4.18 */
void top_score_enter(void)
{
    s16 o, cur = 0x15, shown = 0, i, j;
    u16 blink = 0, k;

    DSB(DS_colour_offset) ^= 0x80;
    flow_strcpy(DS_path_car + 7, 0x0F2A);                      /* "SC.BIN" */
    pal_load(DS_path_car);
    DSB(DS_skip_enabled) = 0;
    DSB(DS_skip_flag) = 0;
    flow_page(1);
    gfx_set_colour(0);
    gfx_fill_rect(0, 0x13f, 0, 199);
    flow_strcpy(DS_path_car + 7, 0x0F31);                      /* ".BIC" */
    flow_load_decode(DS_path_car);
    pic_draw(DS_sprite_set, DSW(DS_car_pic_pairs + 6), 0x70, 0, 0x8b, 0);
    flow_strcpy(DS_path_car + 7, 0x0F36);                      /* ".ICN" */
    flow_load_decode(DS_path_car);
    pic_draw(DS_sprite_set, DSW(DS_car_pic_pairs), 0xd0, 0x70, 0x8b, 0);
    text_set_colours(0x0e, 0);
    o = print_records(0x21AA, 0);                              /* "You are now one of" / "...TOP DRIVERS!!" */
    text_set_colours(0x0f, 0);
    if (DSB(DS_race_players) == 1) {
        print_records(0x21AA, o);                              /* "Enter your name:" */
    } else {
        DSB(0x21FC) = (u8)(DSB(DS_cur_player) + '0');
        print_records(0x21F3, 0);                              /* "Player n name:" */
    }
    gfx_set_colour(0x0c);
    gfx_frame(0xa4, 0x124, 0xb3, 0xc3);
    for (i = 0; i < 0x0f; i++) DSB(DS_name_buf + i) = ' ';
    DSB(DS_name_buf + 15) = 0x80;
    screen_present();
    flow_page(0);
    text_set_colours(10, 0);
    DSB(DS_kbd_key) = 0;
    while (cur < 0x80) {                                       /* cur = 15h + cursor index */
        gfx_set_colour(0);
        gfx_move_to(0xa8, 0xc0);
        gfx_line_to(0x11f, 0xc0);                              /* erase the cursor */
        blink ^= 0x0f;
        if (cur != shown) {
            shown = cur;
            text_goto_cell(0x17, 0x15);
            print_text(DS_name_buf, 0);
            DSB((u16)(DS_name_buf + cur - 0x15)) = 0x80;       /* text_x <- cursor position */
            text_goto_cell(0x17, 0x15);
            print_text(DS_name_buf, 0);
            DSB((u16)(DS_name_buf + cur - 0x15)) = ' ';
        }
        gfx_set_colour((s16)blink);
        gfx_move_to((s16)DSW(DS_text_x), 0xc0);
        gfx_line_to((s16)(DSW(DS_text_x) + 7), 0xc0);
        bios_wait_ticks(2);
        key_poll(&k);
        if (k == 8) {                                          /* Backspace */
            if (cur != 0x15) DSB((u16)(DS_name_buf + cur - 0x16)) = ' ';
            shown = 0;
            if (cur > 0x15) cur--;
        } else if (k == 0x0d) {
            cur = 0x80;
        } else if (k == 0x1e || k == 0x9e) {                   /* Del */
            if (cur != 0x23) DSB((u16)(DS_name_buf + cur - 0x15)) = ' ';
            shown = 0;
            if (cur > 0x15) cur--;
        } else if (k == 0x94) {
            if (cur > 0x15) cur--;
        } else if (k == 0x96) {
            if (cur < 0x23) cur++;
        }
        if (((k > 0x1f && k < 0x3a) || (k > 0x40 && k < 0x5b) || (k > 0x60 && k < 0x7b)) && k != '#' && k != '*') {
            if (cur < 0x23) DSB((u16)(DS_name_buf + cur - 0x15)) = (u8)k;
            shown = 0;
            if (cur < 0x23) cur++;
        }
    }
    DSB(DS_tjl_flag) = 0;
    DSB(DS_name_buf + 15) = 0;
    if (strcmp(ds_str(DS_name_buf), ds_str(0x0F3B)) == 0) {   /* "TJL            " */
        DSB(DS_tjl_flag) = 1;
        text_set_colours(10, 0);
        print_records(0x2204, 0);                              /* "Greetings Tom, enjoy your game!" */
        wait_key(0);
    } else {
        DSB(DS_tjl_flag) = 0;
    }
    for (i = 0; i < 7; i++)
        if (DSL(DS_hi_scores + i * 4) <= DSL(DS_race_score)) break;   /* u32 */
    for (j = 6; i < j; j--) {
        DSL(DS_hi_scores + j * 4) = DSL(DS_hi_scores + (j - 1) * 4);
        DSB(DS_hi_car + j) = DSB(DS_hi_car + j - 1);
        for (s16 n = 0; n < 0x0f; n++)
            DSB((u16)(DS_hi_names + 2 + j * 0x12 + n)) = DSB((u16)(DS_hi_names + 2 + (j - 1) * 0x12 + n));
    }
    DSL(DS_hi_scores + i * 4) = DSL(DS_race_score);
    DSB(DS_hi_car + i) = (u8)DSW(DS_car_index);
    for (s16 n = 0; n < 0x0f; n++)
        DSB((u16)(DS_hi_names + 2 + i * 0x12 + n)) = DSB((u16)(DS_name_buf + n));
    hi_write();
}

/* 01f4:0f4e top_scores_screen — game_flow.md §4.18 */
s16 top_scores_screen(s16 unused)
{
    s16 o;
    (void)unused;
    shared_scene_load();                                       /* game_state 3: only checks DATAB.DAT */
    DSB(DS_skip_enabled) = 0;
    DSB(DS_colour_offset) ^= 0x80;
    flow_strcpy(DS_path_program + 2, 0x0F4B);                  /* "TOPCOLR.BIN" */
    pal_load(DS_path_program);
    flow_page(1);
    flow_strcpy(DS_path_program + 2, 0x0F57);                  /* "TOPSCORC.LZ" */
    flow_load_decode(DS_path_program);
    pic_draw(DS_sprite_set, 0x26b4, 0x140, 0, 199, 0);
    flow_strcpy(DS_path_program + 2, 0x0F63);                  /* "TOPSCORB.LZ" */
    flow_load_decode(DS_path_program);
    pic_draw(DS_sprite_set, 0x2d83, 0x140, 0, 0x84, 1);
    flow_strcpy(DS_path_program + 2, 0x0F6F);                  /* "TOPSCORA.LZ" */
    flow_load_decode(DS_path_program);
    pic_draw(DS_sprite_set, 0x2e1b, 0x140, 0, 0x42, 1);
    playdisk_verify();
    DSB(DS_text_transparent) = 1;
    text_set_colours(0, 0);
    o = print_records(0x217D, 0);                              /* "TEST DRIVE III - TOP DRIVERS" */
    text_goto_cell(2, 0x0b);
    text_set_colours(0x0b, 0);
    print_chars(DS_scene_name, 0x12);
    if (DSL(DS_race_score) != 0 && DSB(DS_race_players) == 1) o = print_records(0x217D, o);   /* "Your Score:" */
    for (s16 i = 0; i < 7; i++) {
        u32 sc = DSL(DS_hi_scores + i * 4);
        text_goto_cell(i == 0 ? 4 : (s16)(i + 5), 0x13);
        if (sc != 0) {
            print_score(sc);
            if (car_lst_load(DSB(DS_hi_car + i), DS_path_car, DS_car_name, DS_car_pic_pairs) == 0) {
                u16 f;
                flow_strcpy(DS_path_car + 7, 0x0F7B);          /* ".LST" */
                f = crt_fopen(DS_path_car, 0x0F80);            /* "rb" */
                DSW(DS_file_handle) = f;
                if (f != 0) {
                    crt_fread(ds_ptr(DS_car_name), 1, 0x13, f);
                    crt_fclose(f);
                }
                text_goto_cell(i == 0 ? 4 : (s16)(i + 5), 0);
                text_set_colours(0x0b, 0);
                DSB(DS_car_name + 0x12) = 0x80;
                print_text(DS_car_name, 0);
                DSB(DS_car_name + 0x12) = 0;
            }
        }
    }
    car_lst_load((s16)DSW(DS_car_index), DS_path_car, DS_car_name, DS_car_pic_pairs);
    if (DSL(DS_race_score) != 0 && DSB(DS_race_players) == 1) {
        text_goto_cell(0x18, 0x17);
        print_score(DSL(DS_race_score));
    }
    print_records(DS_hi_names, 0);                             /* names, column 1Ah */
    DSL(DS_race_score) = 0;
    DSB(DS_kbd_key) = 0;
    DSB(DS_skip_flag) = 0;
    screen_present();
    DSB(DS_text_transparent) = 0;
    if (DSB(DS_skip_flag) == 0) return wait_key(0);
    return 0;
}

/* 01f4:12d4 print_score — game_flow.md §4.6 (8 digits built in DS:EA7A.., leading zeros dropped) */
void print_score(u32 v)
{
    s16 i, n;
    DSB(DS_name_buf + 7) = (u8)(v % 10 + '0');
    DSB(DS_name_buf + 6) = (u8)(v % 100 / 10 + '0');
    DSB(DS_name_buf + 5) = (u8)(v % 1000 / 100 + '0');
    DSB(DS_name_buf + 4) = (u8)(v % 10000 / 1000 + '0');
    DSB(DS_name_buf + 3) = (u8)(v % 100000 / 10000 + '0');
    DSB(DS_name_buf + 2) = (u8)(v % 1000000 / 100000 + '0');
    DSB(DS_name_buf + 1) = (u8)(v % 10000000 / 1000000 + '0');
    DSB(DS_name_buf + 0) = (u8)(v / 10000000 + '0');           /* no modulo: >= 10^8 gives ':'.. (faithful) */
    for (i = 0; i < 7 && DSB((u16)(DS_name_buf + i)) == '0'; i++) {}
    for (n = 0; i < 8; i++, n++) DSB((u16)(DS_score_text + n)) = DSB((u16)(DS_name_buf + i));
    DSB((u16)(DS_score_text + n)) = 0x80;
    print_text(DS_score_text, 0);
}

/* 01f4:3f8e leg_banner_draw — game_flow.md §4.17 (the leg's picture set on page 1) */
void leg_banner_draw(void)
{
    u16 l6 = (u16)(DSB(DS_leg_index) * 6);
    DSB(DS_colour_offset) = 0;
    flow_page(1);
    gfx_set_colour(0);
    gfx_fill_rect(0, 0x13f, 0, 5);
    flow_strcpy(DS_path_scene + 2, (u16)(DSW(DS_scene_index) * 8 + DS_scene_slots));
    flow_strcpy(DS_path_scene + 10, 0x10F3);                   /* ".COL" */
    DSB(DS_path_scene + 9) = DSB((u16)(l6 + 0x0B1E));
    pal_load(DS_path_scene);
    flow_strcpy(DS_path_scene + 11, 0x10F8);                   /* "BLZ" */
    flow_load_decode(DS_path_scene);
    pic_draw(DS_sprite_set, DSW((u16)(l6 + 0x0B1C)), 0x140, 0, 0x4a, 0);
    DSB(DS_path_scene + 11) = 'A';
    flow_load_decode(DS_path_scene);
    pic_draw(DS_sprite_set, DSW((u16)(l6 + 0x0B1A)), 0x140, 0, 0x37, 1);
}

/* cumulative score of record p as the race status page builds it: hi byte pair : lo byte pair */
static s32 rec_score(u16 lo_ds, u16 hi_ds)
{
    return (s32)((u32)DSW(hi_ds) << 16 | DSW(lo_ds));
}

/* 01f4:40ce race_status_screen — game_flow.md §4.17 (checked against 01f4:42be-4450: signed 32-bit compares) */
void race_status_screen(void)
{
    u16 msg;
    s16 o;

    DSB(DS_skip_enabled) = 0;
    DSB(DS_skip_flag) = 0;
    results_frame_draw(1);
    for (s16 i = 0; i < 0x0c; i++)
        DSB((u16)(DS_results_name_buf + i)) = DSB((u16)(DS_scene_leg_names + DSB(DS_leg_index) * 0x30 + i));
    DSB(0x22C7) = 0xaa;
    text_set_colours(0, 7);
    text_goto(0x51, 0x0e);
    o = print_text(DS_results_name_buf, 0);                    /* leg name */
    text_set_colours(0, 7);
    text_goto(0x51, 1);
    o = print_text(DS_results_name_buf, o);                    /* "RACE STATUS" */
    text_set_colours(4, 7);
    text_goto(0x51, 0x1b);
    o = print_text(DS_results_name_buf, o);                    /* "Total Score" */

    if (DSB(DS_race_computer_cars) == 0) {
        bool alive = false, tie;
        u8 win;
        s32 best, cand;
        for (u16 i = 0; i < DSB(DS_race_players); i++) {
            text_set_colours(0, 7);
            o = print_records(DS_results_name_buf, o);         /* "Player n" */
            text_goto_cell((s16)(i * 2 + 0x0f), 3);
            DSB(DS_car_name + 0x12) = 0x80;
            print_text(DS_car_name, 0);
            DSB(DS_car_name + 0x12) = 0;
            if (i < DSB(DS_cur_player)) {
                gfx_set_colour(0);
                draw_leg_record(0x6d, (s16)(i * 0x10 + 0x75), REC(i), 0);
                gfx_set_colour(4);
                draw_leg_record(0xd7, (s16)(i * 0x10 + 0x75), (u16)(REC(i) + 0x0a), 0);
            }
        }
        for (u16 i = DSB(DS_cur_player); i <= DSB(DS_race_players); i++)
            if (DSB((u16)(i * 0x1b - 0x180f)) != 0) alive = true;          /* lives of player i (1-based) */
        if (alive && DSB(DS_cur_player) != DSB(DS_race_players)) goto present;
        /* the verdict: cumulative scores (+0Eh) of players 1-4 */
        win = 0;
        tie = false;
        best = rec_score(0xE802, 0xE804);
        cand = rec_score(0xE81D, 0xE81F);
        if (cand == best) tie = true;
        if (cand > best) { tie = false; win = 1; best = cand; }
        cand = rec_score(0xE838, 0xE83A);
        if (cand == best) tie = true;
        if (cand > best) { tie = false; win = 2; best = cand; }
        cand = rec_score(0xE853, 0xE855);
        if (cand == best) tie = true;
        if (cand > best) { tie = false; win = 3; }
        DSB(0x24AE) = (u8)(win + '1');
        DSB(0x24D1) = DSB(0x24AE);
        text_set_colours(0x0f, 0);
        if (tie) {
            msg = 0x24DC;                                      /* "Wow, a TIE! ..." */
        } else {
            alive = false;
            for (u16 i = 1; i <= DSB(DS_race_players); i++)
                if (DSB((u16)(i * 0x1b - 0x180f)) != 0) alive = true;
            if (alive && (u16)DSB(DS_leg_index) < (u16)(DSB(DS_leg_count) - 1))
                msg = 0x24A5;                                  /* "Player n is currently winning..." */
            else
                msg = 0x24C8;                                  /* "Player n has WON!" */
        }
    } else {
        s32 you, c1, c2;
        text_set_colours(0, 7);
        DSB(DS_car_name + 0x12) = 0x80;
        DSB(0x22AD) = 0x80;
        DSB(0x2298) = 0x80;
        print_records(0x225C, 0);                              /* You / car / Computer 1 / name / Computer 2 / name */
        DSB(DS_car_name + 0x12) = 0;
        DSB(0x22AD) = 0;
        DSB(0x2298) = 0;
        gfx_set_colour(0);
        draw_leg_record(0x6d, 0x75, REC(0), 0);
        draw_leg_record(0x6d, 0x85, REC(1), 0);
        draw_leg_record(0x6d, 0x95, REC(2), 0);
        gfx_set_colour(4);
        draw_leg_record(0xd7, 0x75, (u16)(REC(0) + 0x0a), 0);
        draw_leg_record(0xd7, 0x85, (u16)(REC(1) + 0x0a), 0);
        draw_leg_record(0xd7, 0x95, (u16)(REC(2) + 0x0a), 0);
        you = rec_score(0xE802, 0xE804);
        c1 = rec_score(0xE81D, 0xE81F);
        c2 = rec_score(0xE838, 0xE855);                        /* quirk: high word from player 3's record */
        text_set_colours(0x0f, 0);
        if ((u16)DSB(DS_leg_index) < (u16)(DSB(DS_leg_count) - 1)) {
            if (DSB(DS_lives) == 0) msg = 0x2426;              /* "You LOST! ..." */
            else if (you < c1 || you < c2) msg = 0x2488;       /* "Press space to continue..." */
            else msg = 0x2469;                                 /* "You are currently winning..." */
        } else {
            if (DSB(DS_lives) == 0 || you < c1 || you < c2) msg = 0x2426;
            else msg = 0x244A;                                 /* "Congratulations! You WON!" */
        }
    }
    print_records(msg, 0);
present:
    flow_page(0);
    screen_present();
    DSB(DS_kbd_key) = 0;
    wait_key(0);
}

/* 01f4:470e results_frame_draw — game_flow.md §4.17 */
void results_frame_draw(s16 status)
{
    flow_page(1);
    gfx_set_colour(0);
    gfx_fill_rect(0, 0x13f, 0xbe, 199);
    gfx_set_colour(7);
    gfx_fill_rect(0, 0x13f, 0x4b, 0xbd);
    gfx_set_colour(0x0f);
    gfx_move_to(0, 0xbc);
    gfx_line_to(0, 0x4b);
    gfx_line_to(0x13e, 0x4b);
    gfx_move_to(0x13e, 0x4b);
    gfx_line_to(0x13e, 0xbc);
    gfx_line_to(0, 0xbc);
    gfx_set_colour(8);
    gfx_frame(2, 0x13d, 0x4d, 0xbb);
    flow_strcpy(DS_path_playdisk + 2, DSB(DS_car_imperial) == 0 ? 0x1107 : 0x10FC);   /* DETAIL2 / DETAIL1.LZ */
    flow_load_decode(DS_path_playdisk);
    pic_draw(DS_sprite_set, DSB(DS_car_imperial) == 0 ? 0x15d : 0x161, 0xb8, 0x72, 0x65, 0);   /* column heads */
    gfx_set_colour(4);
    gfx_move_to(0x13c, 0x5a);
    gfx_line_to(3, 0x5a);
    gfx_frame(3, 0x13c, 0x4e, 0xba);
    gfx_move_to(0xd0, 0x4e);
    if (status == 1) {
        gfx_line_to(0xd0, 0xba);
    } else {
        gfx_line_to(0xd0, 0xac);
        gfx_move_to(3, 0xac);
        gfx_line_to(0x13c, 0xac);
        gfx_move_to(0xfa, 0x5a);
        gfx_line_to(0xfa, 0xac);
        gfx_move_to(0x10d, 0x5a);
        gfx_line_to(0x10d, 0xac);
    }
    text_set_colours(0x0f, 0);
}

/* 01f4:4954 leg_results — game_flow.md §4.17 (scorecard, record merge, .HI update) */
void leg_results(void)
{
    u8 route_new = 0, cum_new = 0;
    u16 li = DSB(DS_leg_index), ri = DSB(DS_route_index);
    s16 o;

    playdisk_verify();
    DSB(DS_skip_enabled) = 0;
    DSB(DS_skip_flag) = 0;
    flow_page(1);
    results_frame_draw(0);
    print_records(0x230C, 0);                                  /* "Press space to continue..." */
    if (DSB(DS_lives) != 0) {
        route_new = (u8)leg_record_merge(DS_leg_result, (u16)(DS_hi_route_best + (li * 3 + ri) * 8));
        cum_new = (u8)leg_record_merge(DS_race_result, (u16)(DS_hi_cumulative_best + li * 8));
    }
    if ((u16)route_new + cum_new != 0) hi_write();
    if (DSB(DS_race_players) != 1) {
        text_set_colours(0, 7);
        DSB(0x225A) = (u8)(DSB(DS_cur_player) + '0');
        print_records(0x2251, 0);                              /* "Player n" */
    }
    flow_strcpy(0x2378, DS_scene_name);                        /* "<scene name> Record" */
    DSB(0x238A) = ' ';
    text_set_colours(0x0f, 0);
    o = print_records(0x230C, 0);
    text_set_colours(0, 7);
    text_goto(0x51, 1);
    o = print_text(0x230C, o);                                 /* SCORECARD */
    text_goto(0x51, 0x0e);
    o = print_text(0x230C, o);                                 /* Your Record */
    text_set_colours(4, 7);
    text_goto(0x51, 0x1b);
    o = print_text(0x230C, o);                                 /* Best Records */
    text_set_colours(0, 7);
    for (s16 i = 0; i < 0x0c; i++) {                           /* leg name, route name */
        DSB((u16)(0x235A + i)) = DSB((u16)(DS_scene_leg_names + li * 0x30 + i));
        DSB((u16)(0x2369 + i)) = DSB((u16)(ri * 0x0c + li * 0x30 + i - 0x1A34));   /* DS:E5CC + ... */
    }
    o = print_records(0x230C, o);
    gfx_set_colour(0);
    draw_leg_record(0x6d, 0x6d, DS_leg_result, route_new);
    draw_leg_record(0x6d, 0x7d, DS_race_result, cum_new);
    gfx_set_colour(4);
    draw_leg_record(0xd7, 0x6d, (u16)(DS_hi_route_best + (li * 3 + ri) * 8), 0);
    draw_leg_record(0xd7, 0x7d, (u16)(DS_hi_cumulative_best + li * 8), 0);
    draw_leg_record(0xd7, 0xb5, (u16)(DSB(DS_leg_count) * 8 + 0x212C), 0);
    if (li + 1 < (u16)DSB(DS_leg_count)) {                     /* next section and its 3 routes */
        for (s16 i = 0; i < 0x0c; i++) {
            u16 n = (u16)(i + li * 0x30);
            DSB((u16)(0x23AF + i)) = DSB((u16)(n - 0x1A10));
            DSB((u16)(0x23BE + i)) = DSB((u16)(n - 0x1A04));
            DSB((u16)(0x23CD + i)) = DSB((u16)(n - 0x19F8));
            DSB((u16)(0x23DC + i)) = DSB((u16)(n - 0x19EC));
        }
        print_records(0x230C, o);
        gfx_set_colour(4);
        draw_leg_record(0xd7, 0x95, (u16)(li * 0x18 + 0x2074), 0);
        draw_leg_record(0xd7, 0x9d, (u16)(li * 0x18 + 0x207C), 0);
        draw_leg_record(0xd7, 0xa5, (u16)(li * 0x18 + 0x2084), 0);
    }
    flow_page(0);
    screen_present();
    DSB(DS_kbd_key) = 0;
    DSB(DS_skip_flag) = 0;
    text_set_colours(0x0f, 0);
    while (DSB(DS_skip_flag) == 0) {                           /* alternate the bottom line until a key */
        if ((u16)route_new + cum_new != 0) {
            print_records(0x2400, 0);                          /* "CONGRATULATIONS!  You set a record!" */
            for (s16 i = 0; i < 0x19; i++) if (DSB(DS_skip_flag) == 0) wait_key(2);
        }
        print_records(0x230C, 0);                              /* "Press space to continue..." */
        for (s16 i = 0; i < 0x19; i++) if (DSB(DS_skip_flag) == 0) wait_key(2);
    }
    DSB(DS_kbd_key) = 0;
}

/* digit d of the 5-row font DS:B75B at the pen */
static void rec_digit(u16 d)
{
    gfx_draw_bitmap((u16)(DS_digit_font + d * 5), 1, 5);
}

/* 01f4:4d7e draw_leg_record — game_flow.md §4.17 (verified) */
void draw_leg_record(s16 x, s16 y, u16 rec_ds, u8 hl)
{
    u16 d, v;
    u8 r0 = DSB(rec_ds), r1 = DSB((u16)(rec_ds + 1)), r2 = DSB((u16)(rec_ds + 2));
    u32 s, p;
    s16 px;

    if (hl & 1) gfx_set_colour(4);                             /* improved time in red */
    d = (u16)(r0 % 1000 / 100);                                /* minutes: up to 3 digits */
    if (d == 0) {
        gfx_move_to(x, y);
        d = (u16)(r0 % 100 / 10);
        if (d != 0) rec_digit(d);
    } else {
        gfx_move_to((s16)(x - 5), y);
        rec_digit(d);
        gfx_move_to(x, y);
        rec_digit((u16)(r0 % 100 / 10));
    }
    gfx_move_to((s16)(x + 5), y);
    rec_digit((u16)(r0 % 10));
    gfx_put_pixel((s16)(x + 0x0b), (s16)(y - 1));             /* ':' */
    gfx_put_pixel((s16)(x + 0x0b), (s16)(y - 3));
    gfx_move_to((s16)(x + 0x0e), y);
    rec_digit((u16)(r1 % 100 / 10));
    gfx_move_to((s16)(x + 0x13), y);
    rec_digit((u16)(r1 % 10));
    gfx_put_pixel((s16)(x + 0x19), y);                         /* '.' */
    gfx_move_to((s16)(x + 0x1c), y);
    rec_digit((u16)(r2 % 100 / 10));                           /* frac: always 0 for 0-9 (§9) */
    if (hl & 1) gfx_set_colour(0);
    if (hl & 2) gfx_set_colour(4);
    gfx_move_to((s16)(x + 0x26), y);
    v = DSB((u16)(rec_ds + 3));
    if (DSB(DS_car_imperial) == 0) v = (u16)((u16)(v << 3) / 5);   /* km/h */
    d = (u16)(v % 1000 / 100);
    if (d != 0) rec_digit(d);
    gfx_move_to((s16)(x + 0x2b), y);
    rec_digit((u16)(v % 100 / 10));
    gfx_move_to((s16)(x + 0x30), y);
    rec_digit((u16)(v % 10));
    if (hl & 2) gfx_set_colour(0);
    if (hl & 4) gfx_set_colour(4);
    s = DSL((u16)(rec_ds + 4));
    px = (s16)(x + 0x3a);
    for (p = 100000000u; p > 9; p /= 10) {                     /* 05F5E100h */
        if (p / 10 < s || p == 10) {
            gfx_move_to(px, y);
            rec_digit((u16)((s % p) / (p / 10)));
            px = (s16)(px + 5);
        }
    }
    if (hl & 4) gfx_set_colour(0);
}

/* 01f4:51e2 leg_record_merge — game_flow.md §4.17 (field-wise best; <= counts as a new record) */
s16 leg_record_merge(u16 new_ds, u16 best_ds)
{
    s16 r = 0;
    u32 tn = (u32)DSB(new_ds) * 6000u + (u32)DSB((u16)(new_ds + 1)) * 100u + (u32)DSB((u16)(new_ds + 2)) * 10u;
    u32 tb = (u32)DSB(best_ds) * 6000u + (u32)DSB((u16)(best_ds + 1)) * 100u + (u32)DSB((u16)(best_ds + 2)) * 10u;
    if (tn <= tb || tb == 0) {
        r = 1;
        DSB(best_ds) = DSB(new_ds);
        DSB((u16)(best_ds + 1)) = DSB((u16)(new_ds + 1));
        DSB((u16)(best_ds + 2)) = DSB((u16)(new_ds + 2));
    }
    if (DSB((u16)(best_ds + 3)) <= DSB((u16)(new_ds + 3))) {
        r = (s16)(r + 2);
        DSB((u16)(best_ds + 3)) = DSB((u16)(new_ds + 3));
    }
    if (DSL((u16)(best_ds + 4)) <= DSL((u16)(new_ds + 4))) {
        r = (s16)(r + 4);
        DSL((u16)(best_ds + 4)) = DSL((u16)(new_ds + 4));
    }
    return r;
}

/* 01f4:55f0 opponent_results — game_flow.md §4.16 (the §4.1.1 arithmetic for the two computer cars) */
void opponent_results(void)
{
    if (DSB(DS_lives) == 0 || DSB(DS_race_computer_cars) == 0) return;   /* the current human's lives */
    for (s16 p = 1; p < 3; p++) {
        u16 r = REC(p);
        u32 dist = (u32)DSB((u16)(r + 8)) * 60u + DSB((u16)(r + 9));
        u32 t = (u32)DSB(r) * 6000u + (u32)DSB((u16)(r + 1)) * 100u + (u32)DSB((u16)(r + 2)) * 10u;
        u32 k, s, cd, ct;
        u16 ref = DSW(DS_leg_ref_time);
        DSB((u16)(r + 3)) = t ? (u8)((s32)(dist * 6000u) / (s32)t) : 0;
        k = (u16)((u16)DSB((u16)(r + 3)) * (u16)(DSW(DS_skill_level) + 4));
        if (dist != 0 && DSB(DS_lives) != 0) {
            s = (k * ref * 0x14u) / dist;
        } else {
            u32 dd = (dist > ref) ? ref : dist;
            s = (dd * k) / 0x60u;
        }
        if (s > 1000000u) s = 1000000u;
        DSL((u16)(r + 4)) = s;
        DSL((u16)(r + 0x0e)) += s;                             /* the cumulative record's score */
        DSB((u16)(r + 0x0c)) = (u8)(DSB((u16)(r + 0x0c)) + DSB((u16)(r + 2)));
        if (DSB((u16)(r + 0x0c)) > 9) {
            DSB((u16)(r + 0x0c)) = (u8)(DSB((u16)(r + 0x0c)) - 10);
            DSB((u16)(r + 0x0b))++;
        }
        DSB((u16)(r + 0x0b)) = (u8)(DSB((u16)(r + 0x0b)) + DSB((u16)(r + 1)));
        if (DSB((u16)(r + 0x0b)) > 0x3b) {
            DSB((u16)(r + 0x0b)) = (u8)(DSB((u16)(r + 0x0b)) - 0x3c);
            if (DSB((u16)(r + 0x0a)) != 0xff) DSB((u16)(r + 0x0a))++;
        }
        if ((u16)DSB((u16)(r + 0x0a)) + DSB(r) < 0x100) DSB((u16)(r + 0x0a)) = (u8)(DSB((u16)(r + 0x0a)) + DSB(r));
        else DSB((u16)(r + 0x0a)) = 0xff;
        DSB((u16)(r + 0x13)) = (u8)(DSB((u16)(r + 0x13)) + DSB((u16)(r + 9)));
        if (DSB((u16)(r + 0x13)) > 0x3b) {
            DSB((u16)(r + 0x13)) = (u8)(DSB((u16)(r + 0x13)) - 0x3c);
            DSB((u16)(r + 0x12))++;
        }
        DSB((u16)(r + 0x12)) = (u8)(DSB((u16)(r + 0x12)) + DSB((u16)(r + 8)));
        cd = (u32)DSB((u16)(r + 0x12)) * 60u + DSB((u16)(r + 0x13));
        ct = (u32)DSB((u16)(r + 0x0a)) * 6000u + (u32)DSB((u16)(r + 0x0b)) * 100u + (u32)DSB((u16)(r + 0x0c)) * 10u;
        DSB((u16)(r + 0x0d)) = ct ? (u8)((s32)(cd * 6000u) / (s32)ct) : 0;
    }
}
