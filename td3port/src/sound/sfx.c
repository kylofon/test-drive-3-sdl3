/* Sound-effect engine of the driver: 1ace:0ce9..109a — sound.md §4.5-4.7. Effect scripts are u16
 * words in DGROUP (DS:91BE..944B), addressed by DS offset; DS:93E6 is patched by engine_sound. */
#include "sound/sound.h"
#include "sound/sound_int.h"

/* 1ace:0ce9 voice_modulate — sound.md §4.6 */
void voice_modulate(u8 al, u16 si)
{
    u16 bx = (u16)(al << 1);                           /* BH of the callers is 0 */
    u16 cx = (u16)(DSW(DS_voice_noteword + bx) + DSW(DS_sfx_slide + si) + DSW(DS_sfx_vib_step + si));
    if (cx == DSW(DS_voice_noteword + bx) && DSB(DS_voice_note + (bx >> 1)) == (u8)(cx >> 8)) return;
    DSW(DS_voice_noteword + bx) = cx;
    snd_call_set_freq((u16)(bx >> 1), bx, cx);
}

/* 1ace:0d1c sfx_modulate — sound.md §4.6 (first thing every tick, via DS:C912) */
void sfx_modulate(void)
{
    for (s16 si = 4; si >= 0; si -= 2) {
        s16 c = (s16)(DSW(DS_sfx_vib_count + si) - 1);
        DSW(DS_sfx_vib_count + si) = (u16)c;
        if (c < 0) {
            DSW(DS_sfx_vib_step + si) = (u16)-DSW(DS_sfx_vib_step + si);
            DSW(DS_sfx_vib_count + si) = DSW(DS_sfx_vib_period + si);
        }
    }
    if (DSB(DS_snd_ready) & 4) {
        for (u16 bx = 2; bx != 0; bx--) {              /* effect slots 2 and 1 */
            DSB(DS_cur_owner) = (u8)(bx + 1);
            u8 al = DSB(DS_sfx_slot_voice + bx);
            if (al != 0xFF) voice_modulate(al, (u16)(bx << 1));
        }
    }
    DSB(DS_cur_owner) = 1;
    for (s16 c = 8; c >= 0; c--) {                     /* music (sic: owner of VOICE c, voice of CHANNEL c) */
        if (DSB(DS_voice_owner + c) != 1) continue;
        u8 v = DSB(DS_instr_chmap + c);
        if (v > 0x0F) continue;
        if (DSB(DS_voice_note + v) == 0) continue;
        voice_modulate(v, 0);                          /* slot 0 values */
    }
}

/* 1ace:0db9 sfx_start — sound.md §4.5 (voice stealing). Returns the voice or FFFFh. */
u16 sfx_start(u16 script, u16 prio)
{
    if (!(DSB(DS_snd_ready) & 4)) return 0xFFFF;
    DSB(DS_snd_active) |= 4;
    u16 dx = 3;                                        /* owner id = slot + 1 */
    u16 cx = 8;
    u8 al;
    for (;;) {                                         /* highest channel 8..1 mapped below voice 9 */
        al = DSB(DS_instr_chmap + cx);
        if (al < 9) break;
        if (--cx == 0) break;
    }
    u16 bx = al;
    if (al != 0 && DSB(DS_voice_owner + bx) == (u8)dx) {   /* slot 2 busy: try voice-1, slot 1 */
        bx--;
        dx--;
        if (DSB(DS_voice_owner + bx) == (u8)dx
            && !((s8)DSB(DS_voice_priority + bx) < (s8)DSB(DS_voice_priority + bx + 1))) {
            bx++;                                      /* both busy: the lower priority, tie -> slot 2 */
            dx++;
        }
    }
    if ((s8)DSB(DS_voice_priority + bx) > (s8)(u8)prio) return 0xFFFF;   /* equal priority replaces */
    u16 si = (u16)(bx << 1);
    DSB(DS_force_off) = 1;
    snd_call_note_off(bx, si, (u16)(DSB(DS_voice_note + bx) << 8));
    DSB(DS_force_off) = 0;
    DSB(DS_voice_owner + bx) = (u8)dx;
    DSB(DS_voice_priority + bx) = (u8)prio;
    si = (u16)(dx - 1);
    DSB(DS_sfx_slot_voice + si) = (u8)bx;
    si = (u16)(si << 1);
    DSW(DS_sfx_ptr + si) = script;
    DSW(DS_sfx_wait + si) = 1;
    return bx;                                         /* slot modulation state is not reset */
}

/* 1ace:0e55 sfx_step — sound.md §4.6 (one opcode per active slot per tick, via DS:C910) */
void sfx_step(void)
{
    if (!(DSB(DS_snd_ready) & 4)) return;
    for (s16 s = 2; s >= 0; s--) {
        u16 bx = (u16)s;
        if (DSB(DS_sfx_slot_voice + bx) == 0xFF) continue;
        u16 si = (u16)(bx << 1);
        u16 di = DSW(DS_sfx_ptr + si);
        if (di == 0) continue;
        u16 ax = DSW(DS_sfx_wait + si);
        if (ax != 0) {
            DSW(DS_sfx_wait + si) = (u16)(ax - 1);
            continue;                                  /* (stores DI back unchanged) */
        }
        u16 dx = DSW(di);
        if ((s16)dx > 0x0C) dx = 9;                    /* signed: 0 and negatives never in the data */
        di = (u16)(di + 2);
        DSW(DS_snd_call_target) = DSW((u16)(DS_sfx_ops + (u16)(dx << 1)));
        SndOpFn op = (SndOpFn)codeptr_lookup_near(SND_SEG, DSW(DS_snd_call_target));
        di = op(bx, si, di);
        DSW(DS_sfx_ptr + si) = di;
    }
}

/* 1ace:0eab sfx_op_slide (op 1: sign, n) — sound.md §4.6 */
u16 sfx_op_slide(u16 bx, u16 si, u16 di)
{
    u16 dx = DSW((u16)(di + 2));
    if (DSW(di) == 0) dx = (u16)-dx;
    DSW(DS_sfx_slide + si) = dx;
    return (u16)(di + 4);
}

/* 1ace:0ebe sfx_op_slide_off (op 2) — sound.md §4.6 */
u16 sfx_op_slide_off(u16 bx, u16 si, u16 di)
{
    DSW(DS_sfx_slide + si) = 0;
    return di;
}

/* 1ace:0ec5 sfx_op_vibrato (op 3: period, step) — sound.md §4.6 */
u16 sfx_op_vibrato(u16 bx, u16 si, u16 di)
{
    u16 dx = DSW(di);
    DSW(DS_sfx_vib_period + si) = dx;
    dx = (u16)((dx >> 1) | 1);
    DSW(DS_sfx_vib_count + si) = dx;
    DSW(DS_sfx_vib_step + si) = DSW((u16)(di + 2));
    return (u16)(di + 4);
}

/* 1ace:0edf sfx_op_vibrato_off (op 4) — sound.md §4.6 */
u16 sfx_op_vibrato_off(u16 bx, u16 si, u16 di)
{
    DSW(DS_sfx_vib_step + si) = 0;
    DSW(DS_sfx_vib_period + si) = 0;
    return di;
}

/* 1ace:0eea sfx_op_wait (op 5: n) — sound.md §4.6 */
u16 sfx_op_wait(u16 bx, u16 si, u16 di)
{
    DSW(DS_sfx_wait + si) = DSW(di);
    return (u16)(di + 2);
}

/* 1ace:0ef4 sfx_op_note (op 6: note, velocity) — sound.md §4.6 */
u16 sfx_op_note(u16 bx, u16 si, u16 di)
{
    DSB(DS_cur_owner) = (u8)(bx + 1);
    bx = (u16)((bx & 0xFF00) | DSB(DS_sfx_slot_voice + bx));
    si = (u16)(bx << 1);
    if (DSW(DS_voice_noteword + si) != 0) {
        DSB(DS_force_off) = 1;
        snd_call_note_off(bx, si, (u16)(DSB(DS_voice_note + bx) << 8));
        DSB(DS_force_off) = 0;
    }
    u16 cx = (u16)((DSW(di) & 0xFF) << 8);            /* MIDI note (low byte; DS:93E6 for the engine), CL = 0 */
    u16 dx = DSW((u16)(di + 2));
    DSB(DS_voice_velocity + bx) = (u8)dx;
    if (dx != 0) {                                     /* velocity 0 = rest */
        dx = (u16)(DSB(DS_voice_priority + bx) << 8 | (dx & 0xFF));
        snd_call_note_on(bx, si, cx, dx);
    }
    return (u16)(di + 4);
}

/* 1ace:0f3e sfx_op_loop_end (op 8: x, count) — sound.md §4.6 (count+1 passes, 0 = forever) */
u16 sfx_op_loop_end(u16 bx, u16 si, u16 di)
{
    u16 ax = DSW(DS_sfx_loop_count + si);
    if (ax != 0) {
        if (ax != 0xFFFF) ax--;
        DSW(DS_sfx_loop_count + si) = ax;
        if (ax != 0) return DSW(DS_sfx_loop_start + si);
        return (u16)(di + 4);
    }
    ax = DSW((u16)(di + 2));
    if (ax == 0) ax--;
    DSW(DS_sfx_loop_count + si) = ax;
    return DSW(DS_sfx_loop_start + si);
}

/* 1ace:0f70 sfx_op_loop_start (op 7: x, ignored) — sound.md §4.6 */
u16 sfx_op_loop_start(u16 bx, u16 si, u16 di)
{
    di = (u16)(di + 2);
    DSW(DS_sfx_loop_start + si) = di;
    return di;
}

/* 1ace:0f78 sfx_op_end (op 9) — sound.md §4.6 */
u16 sfx_op_end(u16 bx, u16 si, u16 di)
{
    voice_release(DSB(DS_sfx_slot_voice + bx));
    sfx_slot_reset(bx, si, di);
    return 0;
}

/* 1ace:0f8d sfx_slot_reset (also op 10) — sound.md §4.6 */
u16 sfx_slot_reset(u16 bx, u16 si, u16 di)
{
    DSW(DS_sfx_loop_start + si) = 0;
    DSW(DS_sfx_loop_count + si) = 0;
    DSW(DS_sfx_vib_step + si) = 0;
    DSW(DS_sfx_vib_period + si) = 0;
    DSW(DS_sfx_unused + si) = 0;
    DSW(DS_sfx_slide + si) = 0;
    DSW(DS_sfx_vib_count + si) = 1;
    DSW(DS_sfx_wait + si) = 1;
    return di;
}

/* 1ace:0fb1 sfx_op_program (op 11: prog) — sound.md §4.6 (sic: AdLib maps through C638 twice) */
u16 sfx_op_program(u16 bx, u16 si, u16 di)
{
    bx = (u16)((bx & 0xFF00) | DSB(DS_sfx_slot_voice + bx));
    u16 cx = (u16)(DSB((u16)(DS_instr_progmap + DSW(di))) << 8);
    si = (u16)(bx << 1);
    snd_call_program(bx, si, cx);
    return (u16)(di + 2);
}

/* 1ace:0fcb sfx_op_goto_engine (op 12: address) — sound.md §4.6 */
u16 sfx_op_goto_engine(u16 bx, u16 si, u16 di)
{
    if (DSB(DS_engine_off) == 0) {
        for (s16 i = 15; i >= 0; i--)
            if (DSB(DS_voice_sfx_id + i) == 1) return sfx_op_end(bx, si, di);   /* engine already runs */
        di = DSW(di);                                  /* = 93DE, the engine script */
        u16 v = DSB(DS_sfx_slot_voice + bx);
        DSB(DS_voice_priority + v) = 0x60;
        DSB(DS_voice_sfx_id + v) = 1;
        DSB(DS_sfx_voice + 1) = (u8)v;                 /* DS:9177, the game's table */
        return di;
    }
    return sfx_op_end(bx, si, di);
}

/* 1ace:0ffe sfx_init — sound.md §4.1 */
void sfx_init(u16 dev_cfg)
{
    snd_reset_tables();
    u16 bx = 0;
    u8 al = (u8)dev_cfg;
    /* PORT: MT-32, Tandy and Game Blaster are parked (PORTING.md): use the AdLib routines. */
    if ((al & 0x80) || al == 1 || al == 2) al = 4;
    if (al & 0x80) {
        if (mpu_reset() != 0 && mpu_load_bank(1) != 0) bx = 4;
    } else if (al == 4) {
        if (adlib_detect_reset() != 0 && adlib_load_bank() != 0) bx = 3;
    } else if (al == 2) {
        if (cms_detect() != 0) {
            cms_reset(0);
            snd_identity_chmap(5);
            bx = 2;
        }
    } else if (al == 1) {
        snd_identity_chmap(2);
        bx = 1;
    }
    snd_select_routines(bx);                           /* no speaker defaults here */
    DSB(DS_snd_ready) |= 4;
}

/* 1ace:1072 sfx_stop_all — sound.md §4.5 */
void sfx_stop_all(void)
{
    DSB(DS_snd_active) &= 0xFB;
    for (s16 s = 2; s >= 0; s--) voice_release(DSB(DS_sfx_slot_voice + s));   /* FFh -> ignored */
}
