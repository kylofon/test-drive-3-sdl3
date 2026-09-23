/* AdLib / Sound Blaster routines (device index 3, C5E3 = 4): 1ace:07ad..0a17 and segment 1bd8 —
 * sound.md §4.8. Register writes go straight to the host's OPL2 emulator (host_opl_write). */
#include "sound/sound.h"
#include "sound/sound_int.h"
#include "host.h"

/* 1ace:07ad adlib_set_tl — sound.md §4.8 */
void adlib_set_tl(u16 si, u8 dl)
{
    u16 bx = DSB(DS_adlib_voice_slots + si);           /* slot */
    u8 ah;
    if (DSB(DS_adlib_is_carrier + bx) == 1 || (s8)(u8)bx > 6 || DSB(DS_adlib_inst_conn + bx) != 0)
        ah = (u8)((0x3F - dl) & 0x3F);                 /* velocity TL, KSL = 0 (sic: conn is per voice) */
    else
        ah = DSB(DS_adlib_inst_tl + bx);               /* instrument KSL|TL */
    adlib_write((u8)(DSB(DS_adlib_op_offset + bx) + 0x40), ah);
}

/* 1ace:07df adlib_note_on — sound.md §4.8 */
void adlib_note_on(u16 bx, u16 si, u16 cx, u16 dx)
{
    u8 ch = (u8)(cx >> 8), cl = (u8)cx;
    u8 dl = (u8)dx, dh = (u8)(dx >> 8);
    if ((u8)bx == 9) {                                 /* percussion channel */
        ch = (u8)(ch - 0x24);
        bx = (u16)((bx & 0xFF00) | ch);                /* index >= 40 reads on into the next tables */
        dh = DSB(DS_adlib_perc_voice + bx);
        if ((s8)dh >= 0x0B) return;
        ch = DSB(DS_instr_percmap + bx);
        bx = (u16)((bx & 0xFF00) | dh);
        si = (u16)(bx << 1);
        dl = (u8)(dl >> 1);
        adlib_set_tl(si, dl);                          /* every drum operator gets velocity TL */
        if ((s8)(u8)bx < 7) {
            si++;
            adlib_set_tl(si, dl);
        }
        u8 ah = (u8)(DSB(DS_adlib_rhythm_bit + bx) | DSB(DS_adlib_bd));   /* no 0->1 edge check */
        DSB(DS_adlib_bd) = ah;
        adlib_write(0xBD, ah);
        DSB(DS_adlib_b0_shadow + bx) = cl;             /* sic: key-on bit of B6..B8 = CL & 20h */
        adlib_set_freq(bx, si, (u16)(ch << 8 | cl));   /* BD: folded word stored at byte offset 13 */
        return;
    }
    while ((s8)ch < 0x13) ch = (u8)(ch + 12);
    ch = (u8)(ch - 0x13);                              /* driver note = MIDI - 19 */
    if (voice_alloc_check(bx, ch, dh)) return;
    DSB(DS_voice_owner + bx) = DSB(DS_cur_owner);
    cx = (u16)(ch << 8 | cl);
    DSB(DS_force_off) = 1;
    adlib_note_off(bx, si, cx);                        /* key off first */
    DSB(DS_force_off) = 0;
    DSW(DS_voice_noteword + si) = cx;
    DSB(DS_voice_note + bx) = ch;
    dl = (u8)((s8)dl >> 1);                            /* sar */
    adlib_set_tl(si, dl);                              /* modulator (voices 3-5 quirk) */
    adlib_set_tl((u16)(si + 1), dl);                   /* carrier */
    DSB(DS_adlib_b0_shadow + bx) = 0x20;
    adlib_set_freq(bx, si, cx);
}

/* 1ace:0884 adlib_set_freq — sound.md §4.8 */
void adlib_set_freq(u16 bx, u16 si, u16 cx)
{
    if ((s8)(u8)bx >= 9) return;
    cx = note_fold(cx, 0x6000, si);
    u16 ax = freq_lookup(&cx);
    adlib_write((u8)(0xA0 + (u8)bx), (u8)ax);
    u8 ah = (u8)((u8)(cx << 2) | (u8)(ax >> 8) | (DSB(DS_adlib_b0_shadow + bx) & 0x20));
    DSB(DS_adlib_b0_shadow + bx) = ah;                 /* octave 8 lands on the key-on bit (quirk) */
    adlib_write((u8)(0xB0 + (u8)bx), ah);
}

/* 1ace:08c1 adlib_note_off — sound.md §4.8 */
void adlib_note_off(u16 bx, u16 si, u16 cx)
{
    u8 ch = (u8)(cx >> 8);
    if ((u8)bx > 9) return;
    if ((u8)bx == 9) {
        if ((s8)ch < 0x24) ch = 0x24;
        ch = (u8)(ch - 0x24);
        bx = (u16)((bx & 0xFF00) | ch);
        bx = (u16)((bx & 0xFF00) | DSB(DS_adlib_perc_voice + bx));
        if ((u8)bx > 0x0A) return;
        u8 ah = (u8)(~DSB(DS_adlib_rhythm_bit + bx) & DSB(DS_adlib_bd));
        DSB(DS_adlib_bd) = ah;
        adlib_write(0xBD, ah);
    } else {
        if (!(DSB(DS_force_off) & 1)) {
            ch = (u8)(ch - 0x13);
            while ((s8)ch < 0) ch = (u8)(ch + 12);
            if (ch != DSB(DS_voice_note + bx)) return;  /* stale note off: ignored */
        }
        u8 ah = (u8)(DSB(DS_adlib_b0_shadow + bx) & 0x1F);
        DSB(DS_adlib_b0_shadow + bx) = ah;
        adlib_write((u8)(0xB0 + (u8)bx), ah);
    }
    DSW(DS_voice_noteword + si) = 0;                   /* percussion: SI is still the caller's 2*9 */
    DSB(DS_voice_note + bx) = 0;
}

/* 1ace:092a adlib_load_instrument — sound.md §4.8 */
void adlib_load_instrument(u16 bx, u16 si, u16 cx)
{
    FarPtr p = ds_far(DS_instr_data);
    u16 di = (u16)(p.off + (u8)(cx >> 8) * 9);         /* mul dl: AL * 9 */
    u16 es = p.seg;
    DSB(DS_adlib_inst_conn + bx) = rd8(es, (u16)(di + 8)) & 1;
    bx = (u16)((bx & 0xFF00) | DSB(DS_adlib_voice_slots + si));
    DSB(DS_adlib_inst_tl + bx) = rd8(es, (u16)(di + 1));
    bx = (u16)((bx & 0xFF00) | DSB(DS_adlib_voice_slots + si + 1));
    if ((u8)bx != 0xFF) DSB(DS_adlib_inst_tl + bx) = rd8(es, (u16)(di + 5));
    for (u16 dx = 2; dx != 0; dx--) {
        u16 slot = DSB(DS_adlib_voice_slots + si);
        if (slot == 0xFF) return;                      /* single-operator drums: no C0 write */
        for (u16 c = 0; c < 4; c++) {
            u8 ah = rd8(es, di);
            di++;
            adlib_write((u8)(DSB(DS_adlib_op_offset + slot) + DSB(DS_adlib_op_regs + c)), ah);
        }
        si++;
    }
    adlib_write(0xC0, rd8(es, di));                    /* sic: register C0h, not C0h + voice */
}

/* 1ace:09a1 adlib_program — sound.md §4.8 */
void adlib_program(u16 bx, u16 si, u16 cx)
{
    if ((s8)(u8)bx < 6) {
        DSB(DS_voice_program + bx) = (u8)(cx >> 8);
        u8 ch = DSB(DS_instr_progmap + (u8)(cx >> 8));
        adlib_load_instrument(bx, si, (u16)(ch << 8 | (cx & 0xFF)));
    } else {
        u8 ch = DSB(DS_adlib_voice_inst + bx);         /* drums fixed; voices 11..13 read CA30[0..2] */
        DSB(DS_voice_program + bx) = ch;
    }
}

/* 1ace:09c2 adlib_all_off — sound.md §4.8 (only reached from the dead music_reset) */
void adlib_all_off(u16 bx)
{
    for (s16 c = 8; c >= 0; c--) {
        u8 ah = (u8)(DSB(DS_adlib_b0_shadow + bx) & 0x1F);   /* sic: shadow of BX, register B0h + c */
        DSB(DS_adlib_b0_shadow + bx) = ah;
        adlib_write((u8)(0xB0 + c), ah);
    }
    DSB(DS_adlib_bd) = 0x20;
    adlib_write(0xBD, 0x20);
}

/* 1ace:09ed adlib_load_bank — sound.md §4.8 */
u16 adlib_load_bank(void)
{
    if (instr_load(0) == 0) return 0;
    for (s16 v = 10; v >= 0; v--) {
        u16 bx = (u16)v;
        u8 ch = DSB(DS_adlib_voice_inst + bx);
        DSB(DS_voice_program + bx) = ch;
        DSW(DS_cur_voice) = bx;
        adlib_load_instrument(bx, (u16)(bx << 1), (u16)(ch << 8));
    }
    /* PORT: the original returns the AX of the last adlib_write (value:status), never 0. */
    return 1;
}

/* 1bd8:000e adlib_write — sound.md §4.8. PORT: index/data ports 388h/389h and the 6 + 35 status-read
 * delays become one write into the emulator. */
void adlib_write(u8 al, u8 ah)
{
    host_opl_write(al, ah);
}

/* 1bd8:0028 adlib_detect_reset — sound.md §4.8 */
u16 adlib_detect_reset(void)
{
    adlib_write(0x04, 0x60);
    adlib_write(0x04, 0x80);
    u8 bl = 0x00;                                      /* PORT: status after the timer reset */
    adlib_write(0x02, 0xFF);
    adlib_write(0x04, 0x21);
    u8 bh = 0xC0;                                      /* PORT: status after timer 1 expired */
    adlib_write(0x04, 0x60);
    adlib_write(0x04, 0x80);
    if ((bl & 0xE0) != 0 || (bh & 0xE0) != 0xC0) return 0;
    for (u16 cx = 0xF5; cx != 0; cx--) adlib_write((u8)cx, 0);   /* F5h..01h = 0 */
    adlib_write(0x04, 0x60);
    DSB(DS_adlib_bd) = 0x20;
    adlib_write(0xBD, 0x20);                           /* rhythm mode on, all drums off */
    adlib_write(0x01, 0x20);                           /* waveform select enable */
    return 1;
}
