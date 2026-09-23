/* Sound driver core, segment 1ace (device independent part) and 1bd7 — sound.md §4.1-4.5.
 * All driver state is in DGROUP (DS:C5DE..CB5A); the tables come from the loaded EXE image. */
#include "sound/sound.h"
#include "sound/sound_int.h"
#include "platform/platform.h"

#include <string.h>

/* 1ace:0006 music_notes_off — sound.md §4.4 */
void music_notes_off(void)
{
    DSB(DS_snd_active) &= 0xFE;
    for (s16 c = 15; c >= 0; c--) {
        u16 bx = DSB(DS_instr_chmap + c);
        if (bx > 0x0F) continue;
        if (DSB(DS_voice_owner + bx) != 1) continue;
        u16 si = (u16)(bx << 1);
        DSB(DS_force_off) = 1;
        snd_call_note_off(bx, si, DSW(DS_voice_noteword + si));   /* CH = note of the noteword */
        DSB(DS_force_off) = 0;
        DSB(DS_voice_owner + bx) = 0;
        DSB(DS_voice_priority + bx) = 0;
    }
}

/* 1ace:004b music_play — sound.md §4.4 */
void music_play(FarPtr song, u8 fade, u8 vol)
{
    music_notes_off();
    if (far_is_null(song)) return;
    DSW(DS_music_pos) = (u16)(song.off + 2);          /* skip the u16 length */
    DSW(DS_music_pos + 2) = song.seg;
    DSB(DS_music_ctl) = 0x10;
    u8 al;
    if (fade != 0) {                                   /* unused: the game passes 0 */
        DSB(DS_music_ctl) |= (u8)((fade & 0x0F) | 0x20);
        DSB(DS_fade_target) = vol;
        DSB(DS_fade_count) = 0;
        al = 0x81;
    } else {
        al = vol;
    }
    DSB(DS_master_vol) = al;
    DSW(DS_music_wait) = 1;
    DSB(DS_music_halt) = 0;
    music_claim_voices();
    if (DSB(DS_snd_ready) & 1) DSB(DS_snd_active) |= 1;
}

/* 1ace:00b4 music_stop — sound.md §4.4 */
void music_stop(u8 mode)
{
    if (mode == 0) {
        music_notes_off();
    } else if (mode & 0x80) {
        DSB(DS_music_ctl) = 0x80;
    } else {
        DSB(DS_music_ctl) = (u8)((DSB(DS_music_ctl) & 0xF0) | (mode & 0x0F) | 0x40);
    }
}

/* 1ace:00ec voice_release — sound.md §4.5 */
void voice_release(u16 v)
{
    u16 bx = v;
    if (bx > 0x0F) return;
    u16 si = (u16)(bx << 1);
    u8 owner = DSB(DS_voice_owner + bx);
    if ((s8)owner <= 1) return;
    DSB(DS_voice_sfx_id + bx) = 0;
    u16 s = (u16)(owner - 1);
    DSB(DS_sfx_slot_voice + s) = 0xFF;
    DSW(DS_sfx_ptr + (u16)(s << 1)) = 0;
    DSB(DS_force_off) = 1;
    snd_call_note_off(bx, si, (u16)(DSB(DS_voice_note + bx) << 8));  /* CL: stale, unused */
    DSB(DS_force_off) = 0;
    DSB(DS_voice_priority + bx) = 0;
    DSW(DS_voice_noteword + si) = 0;
    DSB(DS_voice_note + bx) = 0;
    DSB(DS_voice_owner + bx) = 0;
    DSB(DS_snd_active) &= 0xFB;
}

/* 1ace:0151 music_claim_voices — sound.md §4.4 */
void music_claim_voices(void)
{
    const u8 al = 0x40;
    for (s16 cx = 15; cx >= 0; cx--) {
        u16 bx = (u16)cx;
        if (DSB(DS_voice_owner + bx) >= 2) {
            if (al < DSB(DS_voice_priority + bx)) continue;
            /* sic: releases the voice numbered by the owner id (unreachable: effects are >= 60h) */
            voice_release(DSB(DS_voice_owner + bx));
        }
        DSB(DS_voice_owner + bx) = 1;
        DSB(DS_voice_priority + bx) = al;
    }
}

/* 1ace:0185 music_resume — sound.md §4.4 (no callers) */
void music_resume(void)
{
    if (!(DSB(DS_music_ctl) & 0x10)) return;
    if (DSB(DS_snd_active) & 1) return;
    if (!(DSB(DS_snd_ready) & 1)) return;
    DSW(DS_music_wait) = 1;
    DSB(DS_music_halt) = 0;
    DSB(DS_snd_active) |= 1;
    music_claim_voices();
}

/* 1ace:01b6 music_enable — sound.md §4.4 (no callers) */
void music_enable(void)
{
    DSB(DS_snd_ready) |= 1;
}

/* 1ace:01bc music_disable — sound.md §4.4 (no callers) */
void music_disable(void)
{
    if (!(DSB(DS_snd_active) & 1)) return;
    music_notes_off();
    DSB(DS_snd_ready) &= 0xFE;
}

/* 1ace:01cc music_init — sound.md §4.1 */
void music_init(u16 dev_cfg)
{
    DSB(DS_snd_active) = 0;
    DSW(DS_sfx_vib_step) = 0;                          /* slot 0 */
    DSW(DS_sfx_slide) = 0;
    snd_reset_tables();
    u16 ax = dev_cfg;
    /* PORT: MT-32 (80h|bank), Tandy (1) and Game Blaster (2) are parked (PORTING.md): the port uses
     * the AdLib routines for them. */
    if ((ax & 0x80) || ax == 1 || ax == 2) ax = 4;
    u16 bx;
    if (ax & 0x80) {
        if (mpu_reset() == 0 || mpu_load_bank(dev_cfg) == 0) goto speaker;
        bx = 4;
    } else if (ax == 4) {
        if (adlib_detect_reset() == 0) goto speaker;
        if (adlib_load_bank() == 0) goto speaker;
        bx = 3;
    } else if (ax == 2) {
        if (cms_detect() == 0) goto speaker;
        cms_reset(0);
        snd_identity_chmap(5);
        bx = 2;
    } else if (ax == 1) {
        snd_identity_chmap(2);
        bx = 1;
    } else {
    speaker:
        bx = 0;
        DSW(DS_sfx_vib_period) = 0x0C;                 /* slot-0 vibrato for music voices */
        DSW(DS_sfx_vib_step) = 1;
        spk_pit_mode();                                /* out 43h, B6h */
    }
    snd_select_routines(bx);
    DSB(DS_snd_ready) |= 1;
}

/* 1ace:0272 snd_identity_chmap — sound.md §4.1 */
void snd_identity_chmap(u16 cx)
{
    do {
        DSB(DS_instr_chmap + cx) = (u8)cx;
    } while (--cx != 0);
}

/* 1ace:027d snd_reset_tables — sound.md §4.1 */
void snd_reset_tables(void)
{
    memset(mp(DGROUP, DS_instr_progmap), 0, 0x162);
    memmove(mp(DGROUP, DS_instr_veloff), mp(DGROUP, DS_snd_default_tables), 0xA0);
}

/* 1ace:02a5 snd_select_routines — sound.md §4.1 */
void snd_select_routines(u16 bx)
{
    DSB(DS_snd_device_id) = DSB(DS_snd_device_ids + bx);
    bx = (u16)(bx << 1);
    DSW(DS_dev_note_on) = DSW(DS_dev_note_on_tbl + bx);
    DSW(DS_dev_note_off) = DSW(DS_dev_note_off_tbl + bx);
    DSW(DS_dev_program) = DSW(DS_dev_program_tbl + bx);
    DSW(DS_dev_all_off) = DSW(DS_dev_all_off_tbl + bx);
    DSW(DS_dev_set_freq) = DSW(DS_dev_set_freq_tbl + bx);
    DSW(DS_freq_table) = DSW(DS_dev_freq_tables + bx);
    DSW(DS_freq_step_table) = DSW(DS_dev_freq_step_tables + bx);
    if (DSB(DS_snd_device_id) == 4) {
        /* Silent snare strike (velocity 0). CL and DH are whatever the caller chain left (the CX
         * main had when calling snd_init, sound.md §11). PORT: CL = 0.
         * TODO(verify): CL of the original (affects B7h's fraction/key-on bit until the first snare). */
        adlib_note_on(9, 18, 0x2600, 0x0000);
        adlib_note_off(9, 18, 0x2600);
    }
}

/* 1ace:02fa instr_load — sound.md §4.1. INSTR.DAT section SI: the 162h-byte table block to DS:C638,
 * the instruments to a DOS block (far ptr DS:C96A). Returns 1 on success. */
u16 instr_load(u16 si)
{
    DSW(DS_instr_ok) = 0;
    if (DSW(DS_instr_data) != 0 || DSW(DS_instr_data_seg) != 0)
        dos_free(ds_far(DS_instr_data));               /* the second init frees the first block */
    s16 fh = dos_open_read(DS_s_INSTR_DAT);
    if (fh == -1) return DSW(DS_instr_ok);             /* (no close on this path) */
    DSW(DS_instr_handle) = (u16)fh;
    /* PORT: the original leaves on CF after each INT 21h read; dos_read (platform) does not report
     * errors, so those exits are only taken for the lseek and the allocation. */
    for (;;) {
        dos_read(ds_ptr(DS_instr_tmp_size), 2, fh);
        if (si == 0) break;
        u16 dx = DSW(DS_instr_tmp_size);
        if (dx == 0) goto close;
        if (dos21_lseek(fh, (s32)dx, 1) == 0xFFFFFFFFu) goto close;
        si--;
    }
    do {                                               /* device name, ignored (word compare, sic) */
        dos_read(ds_ptr(DS_instr_tmp_char), 1, fh);
    } while (DSW(DS_instr_tmp_char) != 0);
    dos_read(ds_ptr(DS_instr_progmap), 0x162, fh);
    {
        u16 ax = (u16)(s16)((s8)DSB(DS_instr_count) * (s8)DSB(DS_instr_size));   /* imul byte */
        DSW(DS_instr_tmp_size) = ax;
        if (ax != 0) {
            u16 err = 0;
            u16 seg = dos21_alloc((u16)((ax >> 4) + 1), &err);
            DSW(DS_instr_data_seg) = seg != 0 ? seg : err;   /* AX is stored before the CF test */
            if (seg == 0) goto close;
            dos_read(far_make(seg, 0), DSW(DS_instr_tmp_size), fh);
        }
    }
    DSW(DS_instr_ok) = 1;
close:
    dos_close(fh);
    return DSW(DS_instr_ok);
}

/* 1ace:03ba music_reset — sound.md §4.4 (no callers) */
void music_reset(u16 bx)
{
    snd_call_all_off(bx);
    memset(mp(DGROUP, DS_music_pos), 0, 22);           /* C83D..C852 */
}

/* 1ace:03d5 snd_nop — sound.md §2 */
void snd_nop(void)
{
}

/* 1ace:03d6 snd_tick — sound.md §4.2 */
void snd_tick(void)
{
    for (u16 cx = 16; cx != 0; cx--) DSB(DS_voice_busy - 1 + cx) = 0;   /* [bx-3724h], bx = 16..1 */
    ((CodeFn)snd_near(DS_sfx_modulate_ptr))();          /* call [C912] */
    ((CodeFn)snd_near(DS_sfx_step_ptr))();              /* call [C910] */
    if (!(DSB(DS_snd_ready) & 1)) return;
    if (!(DSB(DS_snd_active) & 1)) return;
    music_fade();
    s16 w = (s16)(DSW(DS_music_wait) - 1);
    DSW(DS_music_wait) = (u16)w;
    if (w > 0) return;                                 /* events when the count reaches <= 0 */
    u16 es = DSW(DS_music_pos + 2);
    u16 di = DSW(DS_music_pos);
    u16 bx = 0;
    for (;;) {
        u16 cx = rd16(es, di);
        u16 dx = rd16(es, (u16)(di + 2));             /* reads 4 bytes regardless of the length */
        u8 st = (u8)cx;
        DSB(DS_music_status) = st;
        bx = (u16)((bx & 0xFF00) | (st & 0x0F));
        bx = (u16)((bx & 0xFF00) | DSB(DS_instr_chmap + bx));
        DSW(DS_cur_voice) = bx;
        u16 type = (u16)((st & 0x70) >> 4);
        cx = (u16)((cx & 0xFF00) | 4);                 /* CL = 4 (shift count) at the handler */
        di = (u16)(di + DSB(DS_event_len + type));
        u16 si = (u16)(bx << 1);
        DSW(DS_snd_call_target) = DSW(DS_music_handlers + (u16)(type << 1));
        SndEventFn h = (SndEventFn)codeptr_lookup_near(SND_SEG, DSW(DS_snd_call_target));
        di = h(bx, si, cx, dx, di);
        if (DSB(DS_music_halt) & 1) break;
        if (!(DSB(DS_music_status) & 0x80)) continue;
        u8 al = rd8(es, di);
        di++;
        u16 ax = al;                                   /* cbw of a positive byte */
        if (al & 0x80) {
            al &= 0x7F;
            u8 ah = rd8(es, di);
            di++;
            /* shl al,1 / shr ah,1 / rcr al,1: al = lo7 | (ah & 1) << 7, ah >>= 1 */
            ax = (u16)(al | (ah & 1) << 7 | (ah >> 1) << 8);
        }
        DSW(DS_music_wait) = ax;
        break;
    }
    DSW(DS_music_pos) = di;
}

/* 1ace:048d music_fade — sound.md §4.2 (unused by the game: ctl is 10h, 80h or 0) */
void music_fade(void)
{
    u8 al = (u8)(DSB(DS_music_ctl) & 0xEF);
    if (al == 0) return;
    DSB(DS_fade_count) = (u8)(DSB(DS_fade_count) - 1);
    if ((s8)DSB(DS_fade_count) >= 0) return;
    u8 ah = al;
    al &= 0x0F;
    DSB(DS_fade_count) = al;
    if (ah & 0x80) return;
    if (!(al & 0x20)) {                                /* sic: al was masked to 0Fh, always taken */
        DSB(DS_master_vol) = (u8)(DSB(DS_master_vol) - 1);
        if (DSB(DS_master_vol) != 0x81) return;
        DSB(DS_music_ctl) = 0;
        DSB(DS_music_halt) = 1;
        music_notes_off();
        return;
    }
    al = (u8)(DSB(DS_master_vol) + 1);                 /* fade in: unreachable */
    if ((s8)al > (s8)DSB(DS_fade_target)) DSB(DS_music_ctl) &= 0xDF;
    else DSB(DS_master_vol) = al;
}

/* 1ace:04de mus_ev_note_off — sound.md §4.3 */
u16 mus_ev_note_off(u16 bx, u16 si, u16 cx, u16 dx, u16 di)
{
    if ((u8)bx <= 0x0D) snd_call_note_off(bx, si, cx);
    return di;
}

/* 1ace:04e8 mus_ev_note_on — sound.md §4.3 */
u16 mus_ev_note_on(u16 bx, u16 si, u16 cx, u16 dx, u16 di)
{
    if ((u8)bx > 0x0D) return di;
    if ((u8)dx == 0) return mus_ev_note_off(bx, si, cx, dx, di);
    DSB(DS_cur_owner) = 1;
    bx = (u16)((bx & 0xFF00) | DSB(DS_voice_program + bx));
    s16 ax = (s16)((s8)DSB(DS_instr_veloff + bx) + (u8)dx);
    if (ax < 0) ax = 0;
    else if (ax >= 0x7F) ax = 0x7F;
    s16 x = (s16)((s8)DSB(DS_master_vol) + ax);
    u8 dl;
    if (x < 0) dl = 1;
    else if (x >= 0x7F) dl = 0x7F;
    else dl = (u8)x;                                   /* 0 stays 0: a silent note is still played */
    bx = DSW(DS_cur_voice);
    cx = (u16)(cx & 0xFF00);                           /* CL = 0 */
    snd_call_note_on(bx, si, cx, (u16)(0x4000 | dl));
    return di;
}

/* 1ace:0536 mus_ev_loop — sound.md §4.3 (SI = 2*voice; the "and bl,3" is dead) */
u16 mus_ev_loop(u16 bx, u16 si, u16 cx, u16 dx, u16 di)
{
    if (DSB(DS_music_ctl) & 0x80) {
        DSB(DS_music_halt) = 1;
        DSB(DS_music_ctl) = 0;
        music_notes_off();
        return di;
    }
    u8 ch = (u8)(cx >> 8);
    if (ch == 0) {                                     /* loop start */
        DSW(DS_loop_start + si) = di;
        DSW(DS_loop_status + si) = DSB(DS_music_status);   /* AX = 00:status */
        return di;
    }
    u16 ax = DSW(DS_loop_count + si);
    if (ax == 0) {
        if (DSW(DS_loop_start + si) == 0) return di;
        DSW(DS_loop_count + si) = (u16)(s16)(s8)ch;    /* cbw */
        DSW(DS_loop_exit + si) = di;
    } else if ((u8)ax == 1) {
        DSW(DS_loop_count + si) = (u16)(DSW(DS_loop_count + si) - 1);   /* last pass: fall through */
        return di;
    }
    di = DSW(DS_loop_start + si);
    DSB(DS_music_status) = (u8)DSW(DS_loop_status + si);   /* "20 00": no delta after a jump */
    DSW(DS_loop_count + si) = (u16)(DSW(DS_loop_count + si) - 1);
    return di;
}

/* 1ace:0591 mus_ev_stop_point — sound.md §4.3 */
u16 mus_ev_stop_point(u16 bx, u16 si, u16 cx, u16 dx, u16 di)
{
    if (DSB(DS_music_ctl) & 0x80) {
        DSB(DS_music_halt) = 1;
        DSB(DS_music_ctl) = 0;
        music_notes_off();
    }
    return di;
}

/* 1ace:05a6 mus_ev_program — sound.md §4.3 */
u16 mus_ev_program(u16 bx, u16 si, u16 cx, u16 dx, u16 di)
{
    if ((u8)bx > 0x0D) return di;
    DSB(DS_voice_program + bx) = (u8)(cx >> 8);
    snd_call_program(bx, si, cx);
    return di;
}

/* 1ace:05b4 mus_ev_loop_exit — sound.md §4.3 */
u16 mus_ev_loop_exit(u16 bx, u16 si, u16 cx, u16 dx, u16 di)
{
    u16 ax = DSW(DS_loop_exit + si);
    if (ax != 0) di = ax;
    return di;
}

/* 1ace:05bf mus_ev_hook — sound.md §4.3 (cli; call [C914] = ret; sti) */
u16 mus_ev_hook(u16 bx, u16 si, u16 cx, u16 dx, u16 di)
{
    ((CodeFn)snd_near(DS_mus_hook_ptr))();
    return di;
}

/* 1ace:05ca mus_ev_end — sound.md §4.3 */
u16 mus_ev_end(u16 bx, u16 si, u16 cx, u16 dx, u16 di)
{
    if (DSB(DS_music_status) == 0xFC) {
        DSB(DS_music_halt) = 1;
        DSB(DS_music_ctl) = 0;
        music_notes_off();
    }
    return di;
}

/* 1ace:05df voice_alloc_check — sound.md §4.5. Returns CF: true = reject the note. */
bool voice_alloc_check(u16 bx, u8 ch, u8 dh)
{
    if (DSB(DS_cur_owner) <= 1) {
        if (dh < DSB(DS_voice_priority + bx)) return true;
        if (DSB(DS_voice_owner + bx) != 1) {
            voice_release(bx);                         /* no-op unless an effect holds it */
            DSB(DS_voice_owner + bx) = 1;
            DSB(DS_voice_priority + bx) = 0x40;
        }
        if (DSB(DS_snd_device_id) != 0x80 && DSB(DS_voice_busy + bx) != 0
            && ch <= DSB(DS_voice_note + bx))
            return true;                               /* same tick: the highest note wins */
    }
    DSB(DS_voice_busy + bx) = 1;
    return false;
}

/* 1ace:086d note_fold — sound.md §4.8 */
u16 note_fold(u16 cx, u16 ax, u16 si)
{
    s8 ch = (s8)(cx >> 8);
    s8 ah = (s8)(ax >> 8), al = (s8)ax;
    while (ch > ah) ch = (s8)(ch - 12);
    while (ch < al) ch = (s8)(ch + 12);
    cx = (u16)((u8)ch << 8 | (cx & 0xFF));
    DSW(DS_voice_noteword + si) = cx;                  /* 16-bit store at byte offset SI */
    return cx;
}

/* 1ace:0d86 freq_lookup — sound.md §4.8 */
u16 freq_lookup(u16 *cx)
{
    u8 cl = (u8)(*cx & 0xFF) >> 4;
    u8 ch = (u8)(*cx >> 8);
    u16 q = div16_8(ch, 12);                           /* AL = octave, AH = semitone */
    u8 oct = (u8)q, semi = (u8)(q >> 8);
    u16 bx = (u16)(semi << 1);
    u16 dx = DSW((u16)(DSW(DS_freq_step_table) + bx));
    u16 ax = DSW((u16)(DSW(DS_freq_table) + bx));
    for (; cl != 0; cl--) ax = (u16)(ax + dx);
    *cx = (u16)(oct << 8 | oct);                       /* CL = CH = octave */
    return ax;
}

/* 1bd7:000c dos_free — sound.md §2 */
u16 dos_free(FarPtr p)
{
    return dos21_free(p.seg);
}

/* Port set-up: the near routine pointers stored in DGROUP (sound.h). */
void sound_register_codeptrs(void)
{
    static const struct { u32 fn; CodeFn f; } t[] = {
        { FN_snd_nop, (CodeFn)snd_nop },
        { FN_snd_tick, (CodeFn)snd_tick },
        /* device routines DS:C92E..C956 */
        { FN_spk_note_on, (CodeFn)spk_note_on },       { FN_tandy_note_on, (CodeFn)tandy_note_on },
        { FN_cms_note_on, (CodeFn)cms_note_on },       { FN_adlib_note_on, (CodeFn)adlib_note_on },
        { FN_mpu_note_on, (CodeFn)mpu_note_on },
        { FN_spk_note_off, (CodeFn)spk_note_off },     { FN_tandy_note_off, (CodeFn)tandy_note_off },
        { FN_cms_note_off, (CodeFn)cms_note_off },     { FN_adlib_note_off, (CodeFn)adlib_note_off },
        { FN_mpu_note_off, (CodeFn)mpu_note_off },
        { FN_adlib_program, (CodeFn)adlib_program },   { FN_mpu_program, (CodeFn)mpu_program },
        { FN_spk_all_off, (CodeFn)spk_all_off },       { FN_tandy_all_off, (CodeFn)tandy_all_off },
        { FN_cms_reset, (CodeFn)cms_reset },           { FN_adlib_all_off, (CodeFn)adlib_all_off },
        { FN_mpu_all_off, (CodeFn)mpu_all_off },
        { FN_spk_set_freq, (CodeFn)spk_set_freq },     { FN_tandy_set_freq, (CodeFn)tandy_set_freq },
        { FN_cms_set_freq, (CodeFn)cms_set_freq },     { FN_adlib_set_freq, (CodeFn)adlib_set_freq },
        { FN_mpu_set_freq, (CodeFn)mpu_set_freq },
        /* DS:C910 / C912 */
        { FN_sfx_step, (CodeFn)sfx_step },             { FN_sfx_modulate, (CodeFn)sfx_modulate },
        /* music event handlers DS:C975 */
        { FN_mus_ev_note_off, (CodeFn)mus_ev_note_off }, { FN_mus_ev_note_on, (CodeFn)mus_ev_note_on },
        { FN_mus_ev_loop, (CodeFn)mus_ev_loop },       { FN_mus_ev_stop_point, (CodeFn)mus_ev_stop_point },
        { FN_mus_ev_program, (CodeFn)mus_ev_program }, { FN_mus_ev_loop_exit, (CodeFn)mus_ev_loop_exit },
        { FN_mus_ev_hook, (CodeFn)mus_ev_hook },       { FN_mus_ev_end, (CodeFn)mus_ev_end },
        /* effect opcodes DS:CB3C */
        { FN_sfx_op_slide, (CodeFn)sfx_op_slide },     { FN_sfx_op_slide_off, (CodeFn)sfx_op_slide_off },
        { FN_sfx_op_vibrato, (CodeFn)sfx_op_vibrato }, { FN_sfx_op_vibrato_off, (CodeFn)sfx_op_vibrato_off },
        { FN_sfx_op_wait, (CodeFn)sfx_op_wait },       { FN_sfx_op_note, (CodeFn)sfx_op_note },
        { FN_sfx_op_loop_start, (CodeFn)sfx_op_loop_start }, { FN_sfx_op_loop_end, (CodeFn)sfx_op_loop_end },
        { FN_sfx_op_end, (CodeFn)sfx_op_end },         { FN_sfx_slot_reset, (CodeFn)sfx_slot_reset },
        { FN_sfx_op_program, (CodeFn)sfx_op_program }, { FN_sfx_op_goto_engine, (CodeFn)sfx_op_goto_engine },
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) codeptr_register(t[i].fn, t[i].f);
}
