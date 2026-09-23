/* Game-side sound entry points outside the driver segments (sound.md §5): 01f4:1cf0..1ec1 (init,
 * music selection, shutdown), 0c1c:1110..11f6 (effects) and 0e12:23df (engine note). */
#include "sound/sound.h"
#include "sound/sound_int.h"
#include "platform/platform.h"
#include "game/flow.h"

#include <string.h>

/* 01f4:1cf0 snd_init — sound.md §5.1 */
void snd_init(void)
{
    if (DSB(DS_snd_on) == 0) {
        sfx_init(DSW(DS_snd_device_cfg));
        music_init(DSW(DS_snd_device_cfg));
        DSW(DS_snd_device_cfg) = DSB(DS_snd_device_id);   /* DS:0096 = 0/1/2/4/80h from now on */
        timer_install();
    }
    DSB(DS_snd_on) = 1;
}

/* 01f4:1d22 music_for_state — sound.md §5.1 (switch on DS:0086, table 01f4:1e3a) */
s16 music_for_state(s16 track)
{
    switch (DSW(DS_game_state)) {
    case 1: case 2: case 3: case 6: {                  /* 1d66: title, protection, results */
        if (DSB(DS_snd_on) != 1) return 0;
        if (DSB(DS_music_off) != 0) return 0;
        strcpy(ds_str(0x0A9C), ds_str(0x0FAC));        /* "A:" + "DATAA.DAT": disk check */
        u16 f = crt_fopen(0x0A9A, 0x0FB6 /* "rb" */);
        DSW(0xE5A6) = f;                               /* file handle variable of the caller */
        if (f == 0) return 0;
        crt_fclose(f);
        music_play_theme();
        return 0;                                      /* PORT: AX undefined in the original */
    }
    case 4:                                            /* 1d3c: menus, whatever is in music_buf */
        if (DSB(DS_snd_on) != 1) return 0;
        if (DSB(DS_music_off) != 0) return 0;
        music_play(ds_far(DS_music_buf), 0, 0);
        return 0;                                      /* PORT: AX undefined in the original */
    case 5:                                            /* 1db4: race ("radio") */
        if (!(DSB(DS_music_off) == 0 && DSW(DS_sfx_off) == 1 && DSB(DS_snd_on) == 1)) {
            /* effects on: only when not on the PC speaker (word DS:008F = sfx_off:DS:0090, sic) */
            if (DSB(DS_snd_on) != 1) return 0;
            if (DSB(DS_music_off) != 0) return 0;
            if (DSW(DS_snd_device_cfg) == 0) return 0;
        }
        music_stop(0);
        strcpy(ds_str(0x0AE4), ds_str(0x0FB9));        /* ".MUS" after "A:SCENEnn?" (DS:0ADA) */
        DSB(0x0AE3) = (u8)((u8)track + 'A');           /* A/B/C = radio station */
        file_load_far(DS_path_scene, ds_far(DS_scene_music_buf));
        if (DSB(DS_ext_view) == 0)                     /* not during an instant replay */
            music_play(ds_far(DS_scene_music_buf), 0, 0);
        return 1;
    default:
        return 0;                                      /* PORT: AX undefined in the original */
    }
}

/* 01f4:1e48 music_play_theme — sound.md §5.1 (title, attract, results) */
void music_play_theme(void)
{
    if (DSB(DS_snd_on) != 1) return;
    if (DSB(DS_music_off) != 0) return;
    music_stop(0);
    strcpy(ds_str(0x0A9C), ds_str(0x0FBE));           /* "A:" + "THEME.MUS" */
    file_load_far(0x0A9A, ds_far(DS_music_buf));
    music_play(ds_far(DS_music_buf), 0, 0);
}

/* 01f4:1e9a snd_shutdown — sound.md §5.1 (no chip reset) */
void snd_shutdown(void)
{
    if (DSB(DS_snd_on) == 1) {
        sfx_play(0);
        music_stop(0);
        timer_restore();
    }
    DSB(DS_snd_on) = 0;
}

/* 0c1c:1110 sfx_play — sound.md §5.2 */
void sfx_play(u16 id)
{
    sfx_play_ax(id);
}

/* 0c1c:111d sfx_play_ax — sound.md §5.2 (cli/sti around the driver calls: single-threaded port) */
void sfx_play_ax(u16 id)
{
    if (DSB(DS_snd_on) != 1) return;
    if (id == 0) {
        sfx_stop_all();
        for (u16 bx = 0; bx < 0x18; bx++) DSB(DS_sfx_voice + bx) = 0xFF;
        return;
    }
    u8 al = (u8)id;
    if (al != 2 && al != 6 && DSB(DS_ext_view) != 0) return;   /* replay: only 2 and 6 */
    if (DSB(DS_sfx_off) != 0) return;
    u8 ah = al;
    al &= 0x7F;
    if (al == 0x14 && DSB(DS_leg_siren_alt) != 0) al = 0x17;
    u16 bx = al;
    if (ah & 0x80) {                                   /* stop effect al (no range check) */
        ah = DSB(DS_sfx_voice + bx);
        if (ah == 0xFF) return;
        DSB(DS_sfx_voice + bx) = 0xFF;
        bx = ah;
        if (DSB(DS_voice_sfx_id + bx) == al) voice_release(bx);
        return;
    }
    if (bx > 0x17) return;
    u8 prio = DSB(DS_sfx_priority + bx);
    u8 v = DSB(DS_sfx_voice + bx);
    if (v != 0xFF && DSB(DS_voice_sfx_id + v) == al) return;   /* already playing */
    if (prio == 0x7F) sfx_stop_all();
    u8 r = (u8)sfx_start(DSW(DS_sfx_script + (u16)(bx << 1)), (u16)(v << 8 | prio));
    DSB(DS_sfx_voice + bx) = r;
    if (r == 0xFF) return;
    DSB(DS_voice_sfx_id + r) = al;
    if (al == 0x0F || al == 0x10) DSB(DS_sfx_15_16_started) = 1;
}

/* 0e12:23df engine_sound — sound.md §5.3 (per frame from frame_update) */
void engine_sound(void)
{
    if (DSB(DS_ext_view) != 0) return;
    if (DSB(DS_engine_off) != 0) {
        u8 v = DSB(DS_sfx_voice + 1);
        if ((s8)v < 0) return;
        if (DSB(DS_voice_sfx_id + v) == 1) voice_release(v);
        return;
    }
    sfx_play_ax(1);                                    /* no-op while it plays */
    u16 cx = DSW(DS_engine_rpm);
    if (cx >= 0x5DC) {
        cx = 0x5DC;
        DSW(DS_engine_rpm) = cx;                       /* clamped and written back (side effect) */
    }
    cx = (u16)((cx << 2) + 0x1300);
    if (!(DSW(DS_damage) & 0x20)) cx = (u16)(cx + 0x400);   /* healthy engine: +4 semitones */
    u8 v = DSB(DS_sfx_voice + 1);
    if ((s8)v < 0) return;
    if (DSB(DS_voice_sfx_id + v) != 1) return;
    DSB(DS_engine_script_note) = (u8)(cx >> 8);        /* note byte of the engine script's note op */
    /* (the original also builds (noteword & 7Fh) | cx in AX and drops it) */
    DSW(DS_voice_noteword + (u16)(v << 1)) = (u16)(cx & 0xFF80);
}
