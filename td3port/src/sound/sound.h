#pragma once
/* Sound module (sound.md): the music/effects driver 1ace/1bd7/1bd8 with the AdLib and PC-speaker device
 * routines, and the game-facing entry points that live in other segments (snd_init/snd_shutdown/
 * music_for_state in 01f4, sfx_play/sfx_play_ax in 0c1c, engine_sound in 0e12).
 *
 * Driver state lives in mem[] at its DGROUP addresses (PORTING.md; sound.md §9.1's "one state struct" is
 * superseded). OPL2 writes go to host_opl_write, the speaker to host_speaker (host.h). The host opens the
 * audio device in host_init, so the sound module needs no separate host set-up: the game's own
 * snd_init (called by game_main) does the driver init, and timer_install (platform) starts the ticks.
 * INSTR.DAT is read with the platform DOS services (dos_open_read / dos_read / dos21_lseek / dos_close,
 * dos21_alloc / dos21_free), as the original's INT 21h calls; 1bd7:000c dos_free is sound's own. */
#include "mem.h"

/* Registers every sound routine whose near address is stored in DGROUP and called through it: device
 * routines DS:C906..C90E and the per-device tables DS:C92E..C956 (5 x 5, AdLib and PC-speaker columns plus
 * snd_nop 1ace:03d5 for the parked devices), DS:C910 sfx_step / C912 sfx_modulate / C914 hook, and the
 * music event handlers DS:C975[8]. Called by modules_init() (game/game.h); callers use
 * codeptr_lookup_near(0x1ACE, off). */
void sound_register_codeptrs(void);

/* 01f4:1cf0 snd_init — sound.md §5.1 (once: sfx_init + music_init with DS:0096, DS:0096 = C5E3, timer_install; DS:009C = 1) */
void snd_init(void);
/* 01f4:1d22 music_for_state — sound.md §5.1 (song for game_state DS:0086; track = DS:B6CC radio station in the race;
 * returns 1/0 in state 5, undefined (port: 0) otherwise) */
s16 music_for_state(s16 track);
/* 01f4:1e9a snd_shutdown — sound.md §5.1 (sfx_play(0), music_stop(0), timer_restore; DS:009C = 0) */
void snd_shutdown(void);
/* 0c1c:1110 sfx_play — sound.md §5.2 (far C wrapper of sfx_play_ax) */
void sfx_play(u16 id);
/* 0c1c:111d sfx_play_ax — sound.md §5.2 (AX = effect id; 0 = stop all, id|80h = stop id) */
void sfx_play_ax(u16 id);
/* 0e12:23df engine_sound — sound.md §5.3 (per frame from frame_update: engine effect 1, pitch from DS:129C, clamps it to 1500) */
void engine_sound(void);
/* 1ace:00b4 music_stop — sound.md §4.4 (0 = notes off now; bit 7 = stop at the next loop/Bx event; else fade speed) */
void music_stop(u8 mode);
/* 1ace:03d6 snd_tick — sound.md §4.2 (the whole per-tick driver; called by the platform tick handler before DS:00A0++) */
void snd_tick(void);
