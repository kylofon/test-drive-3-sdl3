#pragma once
/* Private interface of the sound module (sound.md): the driver segments 1ace / 1bd7 / 1bd8.
 *
 * The driver's near routines take register arguments; the C functions model them with explicit
 * register parameters (PORTING.md): bx = voice (BL, BH is always 0), si = 2*voice (or the noteword
 * byte offset the routine stores to), cx = CH:CL (note : 1/256-semitone fraction), dx = DH:DL
 * (priority : velocity), di = script / event pointer offset.
 *
 * The routine pointers the core calls through (DS:C906..C90E, C910/C912/C914, the music event
 * handlers DS:C975 and the effect opcode table DS:CB3C) stay in mem[] as near offsets into 1ace and
 * are dispatched with codeptr_lookup_near(0x1ACE, off) by the snd_call_* helpers below.
 * snd_nop (1ace:03d5, a bare `ret`) is stored in slots of every kind; the helpers call it without
 * arguments. */
#include "mem.h"
#include "codeptr.h"
#include "symbols.h"

#define SND_SEG 0x1ACE                  /* file segment of the driver's near routines */

/* Register-model signatures of the routines stored in the device tables (sound.md §1, §4.1). */
typedef void (*SndNoteOnFn)(u16 bx, u16 si, u16 cx, u16 dx);   /* 07df b92 (c15 a59 64f) */
typedef void (*SndNoteOffFn)(u16 bx, u16 si, u16 cx);          /* 08c1 bde (ca0 b16 703) */
typedef void (*SndProgramFn)(u16 bx, u16 si, u16 cx);          /* 09a1 (637), 03d5 */
typedef void (*SndAllOffFn)(u16 bx);                           /* 09c2 c0e (cd1 b5f 787) */
typedef void (*SndSetFreqFn)(u16 bx, u16 si, u16 cx);          /* 0884 bc9 (c66 abf 6a3) */
/* Music event handler (DS:C975): BX = voice (chmap[ch], may be FFh), SI = 2*BX, CX = a1:04h,
 * DX = a3:a2, DI = offset of the next event; returns DI. */
typedef u16 (*SndEventFn)(u16 bx, u16 si, u16 cx, u16 dx, u16 di);
/* Effect opcode (DS:CB3C): BX = slot, SI = 2*slot, DI = DS offset of the arguments; returns DI. */
typedef u16 (*SndOpFn)(u16 bx, u16 si, u16 di);

/* ---- driver.c: core (device independent) ---- */
/* 1ace:0006 music_notes_off — sound.md §4.4 */
void music_notes_off(void);
/* 1ace:004b music_play — sound.md §4.4 (far; stack: far ptr song, fade byte, vol byte) */
void music_play(FarPtr song, u8 fade, u8 vol);
/* 1ace:00ec voice_release — sound.md §4.5 (far; stack word v) */
void voice_release(u16 v);
/* 1ace:0151 music_claim_voices — sound.md §4.4 */
void music_claim_voices(void);
/* 1ace:0185 music_resume — sound.md §4.4 (no callers) */
void music_resume(void);
/* 1ace:01b6 music_enable — sound.md §4.4 (no callers) */
void music_enable(void);
/* 1ace:01bc music_disable — sound.md §4.4 (no callers) */
void music_disable(void);
/* 1ace:01cc music_init — sound.md §4.1 (far; stack word dev_cfg) */
void music_init(u16 dev_cfg);
/* 1ace:0272 snd_identity_chmap — sound.md §4.1 (CX = n) */
void snd_identity_chmap(u16 cx);
/* 1ace:027d snd_reset_tables — sound.md §4.1 */
void snd_reset_tables(void);
/* 1ace:02a5 snd_select_routines — sound.md §4.1 (BX = device index 0..4) */
void snd_select_routines(u16 bx);
/* 1ace:02fa instr_load — sound.md §4.1 (SI = section; returns AX = 1 ok / 0) */
u16 instr_load(u16 si);
/* 1ace:03ba music_reset — sound.md §4.4 (no callers; BX = whatever the caller had) */
void music_reset(u16 bx);
/* 1ace:03d5 snd_nop — sound.md §2 (ret) */
void snd_nop(void);
/* 1ace:048d music_fade — sound.md §4.2 */
void music_fade(void);
/* 1ace:04de .. 05ca music event handlers — sound.md §4.3 */
u16 mus_ev_note_off(u16 bx, u16 si, u16 cx, u16 dx, u16 di);
u16 mus_ev_note_on(u16 bx, u16 si, u16 cx, u16 dx, u16 di);
u16 mus_ev_loop(u16 bx, u16 si, u16 cx, u16 dx, u16 di);
u16 mus_ev_stop_point(u16 bx, u16 si, u16 cx, u16 dx, u16 di);
u16 mus_ev_program(u16 bx, u16 si, u16 cx, u16 dx, u16 di);
u16 mus_ev_loop_exit(u16 bx, u16 si, u16 cx, u16 dx, u16 di);
u16 mus_ev_hook(u16 bx, u16 si, u16 cx, u16 dx, u16 di);
u16 mus_ev_end(u16 bx, u16 si, u16 cx, u16 dx, u16 di);
/* 1ace:05df voice_alloc_check — sound.md §4.5 (BX = v, CH = device note, DH = prio; returns CF: true = reject) */
bool voice_alloc_check(u16 bx, u8 ch, u8 dh);
/* 1ace:086d note_fold — sound.md §4.8 (fold CH into [AL, AH] by octaves, store CX at DS:C86D+SI; returns CX) */
u16 note_fold(u16 cx, u16 ax, u16 si);
/* 1ace:0d86 freq_lookup — sound.md §4.8 (in CH note, CL frac; returns AX = table value, *cx: CL = CH = octave) */
u16 freq_lookup(u16 *cx);
/* 1bd7:000c dos_free — sound.md §2 (far; INT 21h 49h on the segment; returns 0 or the DOS error) */
u16 dos_free(FarPtr p);

/* ---- sfx.c: effect engine ---- */
/* 1ace:0ce9 voice_modulate — sound.md §4.6 (AL = v, SI = 2*slot) */
void voice_modulate(u8 al, u16 si);
/* 1ace:0d1c sfx_modulate — sound.md §4.6 (via DS:C912) */
void sfx_modulate(void);
/* 1ace:0db9 sfx_start — sound.md §4.5 (far; stack: script DS offset, prio; returns voice or FFFFh) */
u16 sfx_start(u16 script, u16 prio);
/* 1ace:0e55 sfx_step — sound.md §4.6 (via DS:C910) */
void sfx_step(void);
/* 1ace:0eab .. 0fcb effect opcodes (DS:CB3C) — sound.md §4.6 */
u16 sfx_op_slide(u16 bx, u16 si, u16 di);
u16 sfx_op_slide_off(u16 bx, u16 si, u16 di);
u16 sfx_op_vibrato(u16 bx, u16 si, u16 di);
u16 sfx_op_vibrato_off(u16 bx, u16 si, u16 di);
u16 sfx_op_wait(u16 bx, u16 si, u16 di);
u16 sfx_op_note(u16 bx, u16 si, u16 di);
u16 sfx_op_loop_end(u16 bx, u16 si, u16 di);
u16 sfx_op_loop_start(u16 bx, u16 si, u16 di);
u16 sfx_op_end(u16 bx, u16 si, u16 di);
u16 sfx_slot_reset(u16 bx, u16 si, u16 di);
u16 sfx_op_program(u16 bx, u16 si, u16 di);
u16 sfx_op_goto_engine(u16 bx, u16 si, u16 di);
/* 1ace:0ffe sfx_init — sound.md §4.1 (far; stack word dev_cfg) */
void sfx_init(u16 dev_cfg);
/* 1ace:1072 sfx_stop_all — sound.md §4.5 (far) */
void sfx_stop_all(void);

/* ---- adlib.c: AdLib / Sound Blaster (device index 3) ---- */
/* 1ace:07ad adlib_set_tl — sound.md §4.8 (SI = voice-slot index, DL = velocity/2) */
void adlib_set_tl(u16 si, u8 dl);
/* 1ace:07df adlib_note_on — sound.md §4.8 */
void adlib_note_on(u16 bx, u16 si, u16 cx, u16 dx);
/* 1ace:0884 adlib_set_freq — sound.md §4.8 */
void adlib_set_freq(u16 bx, u16 si, u16 cx);
/* 1ace:08c1 adlib_note_off — sound.md §4.8 */
void adlib_note_off(u16 bx, u16 si, u16 cx);
/* 1ace:092a adlib_load_instrument — sound.md §4.8 (BX = voice, SI = 2*voice, CH = instrument) */
void adlib_load_instrument(u16 bx, u16 si, u16 cx);
/* 1ace:09a1 adlib_program — sound.md §4.8 */
void adlib_program(u16 bx, u16 si, u16 cx);
/* 1ace:09c2 adlib_all_off — sound.md §4.8 */
void adlib_all_off(u16 bx);
/* 1ace:09ed adlib_load_bank — sound.md §4.8 (returns nonzero on success) */
u16 adlib_load_bank(void);
/* 1bd8:000e adlib_write — sound.md §4.8 (far; AL = register, AH = value) */
void adlib_write(u8 al, u8 ah);
/* 1bd8:0028 adlib_detect_reset — sound.md §4.8 (far; returns 1 = found) */
u16 adlib_detect_reset(void);

/* ---- speaker.c: PC speaker (device index 0) ---- */
/* 1ace:0b92 spk_note_on — sound.md §4.9 */
void spk_note_on(u16 bx, u16 si, u16 cx, u16 dx);
/* 1ace:0bc9 spk_set_freq — sound.md §4.9 */
void spk_set_freq(u16 bx, u16 si, u16 cx);
/* 1ace:0bde spk_note_off — sound.md §4.9 */
void spk_note_off(u16 bx, u16 si, u16 cx);
/* 1ace:0c0e spk_all_off — sound.md §4.9 */
void spk_all_off(u16 bx);
/* PORT: PIT channel 2 mode set (music_init's `out 43h, B6h`). */
void spk_pit_mode(void);

/* ---- parked.c: MPU-401 / Game Blaster / Tandy (parked, PORTING.md: fall back to AdLib) ---- */
u16  mpu_reset(void);                               /* 1ace:062c */
void mpu_program(u16 bx, u16 si, u16 cx);           /* 1ace:0637 */
void mpu_note_on(u16 bx, u16 si, u16 cx, u16 dx);   /* 1ace:064f */
void mpu_set_freq(u16 bx, u16 si, u16 cx);          /* 1ace:06a3 */
void mpu_note_off(u16 bx, u16 si, u16 cx);          /* 1ace:0703 */
u16  mpu_command(u8 ah);                            /* 1ace:073d */
void mpu_data(u8 al);                               /* 1ace:076d */
void mpu_all_off(u16 bx);                           /* 1ace:0787 */
u16  mpu_load_bank(u16 ax);                         /* 1ace:0792 */
u16  cms_detect(void);                              /* 1ace:0a18 */
void cms_note_on(u16 bx, u16 si, u16 cx, u16 dx);   /* 1ace:0a59 */
void cms_set_freq(u16 bx, u16 si, u16 cx);          /* 1ace:0abf */
void cms_note_off(u16 bx, u16 si, u16 cx);          /* 1ace:0b16 */
void cms_reset(u16 bx);                             /* 1ace:0b5f */
void tandy_note_on(u16 bx, u16 si, u16 cx, u16 dx); /* 1ace:0c15 */
void tandy_set_freq(u16 bx, u16 si, u16 cx);        /* 1ace:0c66 */
void tandy_note_off(u16 bx, u16 si, u16 cx);        /* 1ace:0ca0 */
void tandy_all_off(u16 bx);                         /* 1ace:0cd1 */

/* ---- api.c: game-side entry points outside the driver ---- */
/* 01f4:1e48 music_play_theme — sound.md §5.1 */
void music_play_theme(void);

/* ---- dispatch through the near routine pointers in DGROUP ---- */
static inline CodeFn snd_near(u16 ptr_ds) { return codeptr_lookup_near(SND_SEG, DSW(ptr_ds)); }

static inline void snd_call_note_on(u16 bx, u16 si, u16 cx, u16 dx)     /* call [C906] */
{
    CodeFn f = snd_near(DS_dev_note_on);
    if (f == snd_nop) snd_nop(); else ((SndNoteOnFn)f)(bx, si, cx, dx);
}
static inline void snd_call_note_off(u16 bx, u16 si, u16 cx)            /* call [C908] */
{
    CodeFn f = snd_near(DS_dev_note_off);
    if (f == snd_nop) snd_nop(); else ((SndNoteOffFn)f)(bx, si, cx);
}
static inline void snd_call_program(u16 bx, u16 si, u16 cx)             /* call [C90A] */
{
    CodeFn f = snd_near(DS_dev_program);
    if (f == snd_nop) snd_nop(); else ((SndProgramFn)f)(bx, si, cx);
}
static inline void snd_call_all_off(u16 bx)                             /* call [C90C] */
{
    CodeFn f = snd_near(DS_dev_all_off);
    if (f == snd_nop) snd_nop(); else ((SndAllOffFn)f)(bx);
}
static inline void snd_call_set_freq(u16 bx, u16 si, u16 cx)            /* call [C90E] */
{
    CodeFn f = snd_near(DS_dev_set_freq);
    if (f == snd_nop) snd_nop(); else ((SndSetFreqFn)f)(bx, si, cx);
}
