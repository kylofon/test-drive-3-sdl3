# Sound — porting spec (segments `1ace`, `1bd7`, `1bd8`)

Addresses as in `port/RE_GUIDE.md` (`SSSS:OOOO` file segments, `DS:xxxx` = DGROUP 1BE4). Confidence:
**verified** = read in the disassembly (and, where noted, checked against `tools/td3snd.py` renders),
**likely**, **guess**. Data formats (`.MUS`, `INSTR.DAT`) and the first analysis are in
`port/formats/sound.md`; this spec restates what the port needs and corrects/extends it (section 11).
Symbols: `port/spec/sound_symbols.csv`.

Scope decided in `PLAN.md`: the port implements the **device-independent core**, the **AdLib/SB**
routines (fed into a bundled OPL2 emulator) and the **PC speaker** routines (TD3.CFG audio 0, also the
fallback when a device is not found). Tandy (SN76489), Game Blaster (CMS/SAA1099) and MT-32 (MPU-401)
are parked: listed in the tables, not specified.

---

## 1. Overview

One assembly module, linked as segments `1ace` (driver, 4.3 KB), `1bd7` (DOS free) and `1bd8` (AdLib
port I/O and detection). It is a small MIDI-like sequencer plus a script-driven sound-effect engine
that share 16 logical *voices*. Everything audible happens inside `snd_tick` (`1ace:03d6`), which
the timer ISR (`0c1c:10e8`, platform spec) calls on every PIT tick, **145.652 Hz**. The game only
calls the API (start/stop music, start/stop effects) and pokes the engine note directly.

Device independence: the core never touches hardware. It calls five routines through pointers
(`DS:C906..C90E`: note on, note off, program, all off, set frequency) and two table pointers
(`DS:C916/C918`: frequency base and 1/16-semitone step tables). `snd_select_routines` (`1ace:02a5`)
copies one column of the per-device tables at init:

| index | device id `C5E3` | device | note on | note off | program | all off | set freq | freq / step table |
|---|---|---|---|---|---|---|---|---|
| 0 | 00h | PC speaker (PIT ch 2, port 61h) | `0b92` | `0bde` | `03d5` (ret) | `0c0e` | `0bc9` | `CAAA` / `CA92` |
| 1 | 01h | Tandy SN76489, port C0h *(parked)* | `0c15` | `0ca0` | `03d5` | `0cd1` | `0c66` | `CADA` / `CAC2` |
| 2 | 02h | Game Blaster, 220h–22Fh *(parked)* | `0a59` | `0b16` | `03d5` | `0b5f` | `0abf` | `CA70` / `CA58` |
| **3** | **04h** | **AdLib / SB, 388h/389h** | **`07df`** | **`08c1`** | **`09a1`** | **`09c2`** | **`0884`** | **`C9AA` / `C992`** |
| 4 | 80h | MPU-401 / MT-32, 330h/331h *(parked)* | `064f` | `0703` | `0637` | `0787` | `06a3` | `CAAA` / `CA92` |

Voices: 0–15. AdLib: 0–5 melodic OPL channels, 6–10 the rhythm section (6 BD, 7 SD, 8 TT, 9 CY,
10 HH), **9 is also the "percussion channel" voice** that MIDI channel 9 maps to. Owners:
`voice_owner[v]` 0 free, 1 music, 2/3 = sound-effect slot 1/2 + 1. Sound effects run in 3 *slots*
(0–2); on AdLib only slots 1 and 2 are ever used, fixed to voices 4 and 5.

### Call graph

```
main 0000:0000
 └ snd_init 01f4:1cf0 (once)
    ├ sfx_init 1ace:0ffe(cfg) ─┬ snd_reset_tables 027d
    │                          ├ adlib_detect_reset 1bd8:0028 → adlib_write 1bd8:000e
    │                          ├ adlib_load_bank 09ed → instr_load 02fa (→ dos_free 1bd7:000c)
    │                          │                       → adlib_load_instrument 092a
    │                          └ snd_select_routines 02a5 → adlib_note_on/off (silent snare)
    ├ music_init 1ace:01cc(cfg)  (same probe sequence again, + PC-speaker defaults)
    └ timer_install 0c1c:109f (PIT ch 0 divisor 2000h)

timer_isr 0c1c:10e8 (145.652 Hz, platform)
 └ snd_tick 1ace:03d6
    ├ clear voice_busy[16]
    ├ [C912] sfx_modulate 0d1c → voice_modulate 0ce9 → [C90E] set_freq
    ├ [C910] sfx_step 0e55 → sfx_ops[CB3C] 0eab..0fcb → [C906]/[C908]/[C90A], voice_release 00ec
    └ if music ready+playing: music_fade 048d; countdown; events → music_handlers[C975] 04de..05ca
                                → [C906] note on (voice_alloc_check 05df) / [C908] note off / [C90A] program

game code (main thread, with cli/sti around driver calls):
 music: music_for_state 01f4:1d22, music_play_theme 01f4:1e48 → music_stop 00b4, music_play 004b
 sfx:   sfx_play 0c1c:1110 → sfx_play_ax 0c1c:111d → sfx_start 0db9, voice_release 00ec, sfx_stop_all 1072
 engine: frame_update 0e12:76ec → engine_sound 0e12:23df → sfx_play_ax(1); writes voice_noteword[v]
 exit:  snd_shutdown 01f4:1e9a → sfx_play(0), music_stop(0), timer_restore 0c1c:10cf
```

Concurrency in the original: the driver's state is shared between the ISR and the game. The game
brackets `sfx_start`, `voice_release`, `sfx_stop_all` and the engine note write with `cli/sti`;
`music_play`/`music_stop` are called **without** `cli` (a tick can land in the middle; harmless in
practice). The port must serialise all API calls against the tick (section 9).

---

## 2. Function table

Near routines take register arguments; the column gives the port signature and the original
registers. `v` = voice (BL, BX), `si = 2*v` unless noted, `note` = CH, `frac` = CL (1/256 semitone,
only the top 4 bits are used), `vel` = DL, `prio` = DH.

### Core (device independent)

| address | name | signature | purpose | conf. |
|---|---|---|---|---|
| `1ace:0006` | music_notes_off | `void (void)` | clear "playing"; for every channel c: v = chmap[c], if v ≤ 15 and owner 1: forced note off (note = noteword>>8), owner = prio = 0 | verified |
| `1ace:004b` | music_play | `far void (u8 far *song, u8 fade, s8 vol)` (stack) | notes off; pos = song+2; ctl 10h; vol / fade setup; wait 1; claim voices; playing if ready. Game: fade = vol = 0 | verified |
| `1ace:00b4` | music_stop | `far void (u8 mode)` | 0 = notes off now; bit 7 = stop at next loop/Bx event; else fade-out speed (unused) | verified |
| `1ace:00ec` | voice_release | `far void (u16 v)` | end an effect on v: free its slot, forced note off, owner/prio/noteword/note = 0, clear C5E1 bit 2 | verified |
| `1ace:0151` | music_claim_voices | `void (void)` | every voice not held by an effect with prio > 40h → owner 1, prio 40h (bug: releases voice *owner-id*, unreachable) | verified |
| `1ace:0185` | music_resume | `far void (void)` | if loaded, not playing, ready: continue from the **current** position. No callers | verified |
| `1ace:01b6` | music_enable | `far void (void)` | C5E0 \|= 1. No callers | verified |
| `1ace:01bc` | music_disable | `far void (void)` | if playing: notes off, C5E0 &= ~1. No callers | verified |
| `1ace:01cc` | music_init | `far void (u16 dev)` | reset, probe device, load bank, PC-speaker defaults on fallback, select routines, C5E0 \|= 1 | verified |
| `1ace:0272` | snd_identity_chmap | `void (CX n)` | chmap[1..n] = 1..n (Tandy 2, CMS 5) | verified |
| `1ace:027d` | snd_reset_tables | `void (void)` | zero C638..C799 (162h bytes), copy A0h default bytes C79A → C6B8 | verified |
| `1ace:02a5` | snd_select_routines | `void (BX idx)` | device id + routine/table pointers; AdLib: silent snare strike | verified |
| `1ace:02fa` | instr_load | `int (SI section)` | load INSTR.DAT section: table block → C638, instruments → DOS block (far C96A) | verified |
| `1ace:03ba` | music_reset | `void (void)` | dev all-off, zero C83D..C852 (pos, status, halt, voice_program). No callers | verified |
| `1ace:03d5` | snd_nop | `void (void)` | `ret`; default routine pointer, "program" of speaker/Tandy/CMS, event hook | verified |
| `1ace:03d6` | snd_tick | `far void (void)` | the whole per-tick driver (section 4.2) | verified |
| `1ace:048d` | music_fade | `void (void)` | per-tick fade of master_vol; fade-in branch unreachable (bug) | verified |
| `1ace:04de` | mus_ev_note_off | handler type 0 | if v ≤ 13: dev note off(v, a1) | verified |
| `1ace:04e8` | mus_ev_note_on | handler type 1 | velocity 0 → note off; velocity offset + master_vol; dev note on prio 40h | verified |
| `1ace:0536` | mus_ev_loop | handler type 2 | loop start / loop end | verified |
| `1ace:0591` | mus_ev_stop_point | handler type 3 | stop if C5E2 bit 7 | verified |
| `1ace:05a6` | mus_ev_program | handler type 4 | v ≤ 13: voice_program[v] = a1; dev program | verified |
| `1ace:05b4` | mus_ev_loop_exit | handler type 5 | jump to loop_exit[v] if set | verified |
| `1ace:05bf` | mus_ev_hook | handler type 6 | `cli; call [C914]; sti` (= ret) | verified |
| `1ace:05ca` | mus_ev_end | handler type 7 | `FC` = halt, ctl 0, notes off | verified |
| `1ace:05df` | voice_alloc_check | `bool (v, n, prio)` → CF | reject rules for music notes; same-tick highest-note rule | verified |
| `1ace:0ce9` | voice_modulate | `void (AL v, SI 2*slot)` | noteword += slide + step; set_freq if changed | verified |
| `1ace:0d1c` | sfx_modulate | `void (void)` via C912 | vibrato counters; modulate effect slots 2,1 and music voices (slot 0 values) | verified |
| `1ace:0d86` | freq_lookup | `(CH n, CL frac) → AX base, CL octave` | table[n%12] + (frac>>4)·step[n%12] | verified |
| `1ace:0db9` | sfx_start | `far s16 (u16 script, u16 prio)` | pick voice 5/4 (slot 2/1), steal, start script; −1 = refused | verified |
| `1ace:0e55` | sfx_step | `void (void)` via C910 | one script opcode per active slot per tick | verified |
| `1ace:0eab`–`0fcb` | sfx_op_* | `DI (BX slot, SI 2*slot, DI args)` | the 12 opcodes (section 4.6) | verified |
| `1ace:0f8d` | sfx_slot_reset | `void (SI 2*slot)` | also opcode 10 | verified |
| `1ace:0ffe` | sfx_init | `far void (u16 dev)` | reset tables, probe, load bank, select routines, C5E0 \|= 4 | verified |
| `1ace:1072` | sfx_stop_all | `far void (void)` | C5E1 &= ~4; voice_release(slot_voice[2..0]) | verified |

### AdLib (index 3)

| address | name | signature | purpose | conf. |
|---|---|---|---|---|
| `1ace:07ad` | adlib_set_tl | `void (SI idx, DL vel2)` | 40h register of one operator: velocity TL or instrument byte (quirks) | verified |
| `1ace:07df` | adlib_note_on | `void (v, note, frac, vel, prio)` | rhythm (v 9) or melodic note on | verified |
| `1ace:086d` | note_fold | `(CX nw, AH max, AL min)` | fold note byte into [AL, AH] by octaves, store noteword[si/2] | verified |
| `1ace:0884` | adlib_set_freq | `void (BL v, SI, CX nw)` | A0h/B0h of channel v (< 9) | verified |
| `1ace:08c1` | adlib_note_off | `void (v, note)` | rhythm bit off, or key off if forced / same note; zero noteword, note | verified |
| `1ace:092a` | adlib_load_instrument | `void (v, CH inst)` | 9 bytes → 20/40/60/80 of both operators, byte 8 → **C0h** | verified |
| `1ace:09a1` | adlib_program | `void (v, CH prog)` | voices 0–5 load `C638[prog]`; voices ≥ 6 fixed | verified |
| `1ace:09c2` | adlib_all_off | `void (BX v)` | key off B0..B8 (with the shadow of BX, bug), BD = 20h. Only caller is the dead `music_reset` | verified |
| `1ace:09ed` | adlib_load_bank | `int (void)` | instr_load(0); instrument CA25[v] into voices 10..0 | verified |
| `1bd8:000e` | adlib_write | `far void (AL reg, AH val)` | index → 388h, 6 status reads, data → 389h, 35 status reads | verified |
| `1bd8:0028` | adlib_detect_reset | `far int (void)` | timer-1 detection; regs F5h..01h = 0, 04h = 60h, BDh = 20h, 01h = 20h | verified |

### PC speaker (index 0)

| address | name | signature | purpose | conf. |
|---|---|---|---|---|
| `1ace:0b92` | spk_note_on | `void (v, note, frac, vel, prio)` | v ≤ 2 only; everything plays on voice 0; note − 24; gate on | verified |
| `1ace:0bc9` | spk_set_freq | `void (SI, CX nw)` | PIT ch 2 divisor = (CAAA[s] + (frac>>4)·CA92[s]) >> octave | verified |
| `1ace:0bde` | spk_note_off | `void (v, note)` | v ≤ 2; if forced or note − 24 == voice_note[0]: gate off, zero noteword/note[0] | verified |
| `1ace:0c0e` | spk_all_off | `void (void)` | port 61h &= FCh | verified |

### Parked devices, helpers outside the driver

| address | name | purpose |
|---|---|---|
| `1ace:062c` `0637` `064f` `06a3` `0703` `073d` `076d` `0787` `0792` | mpu_reset, mpu_program, mpu_note_on, mpu_set_freq (pitch bend), mpu_note_off, mpu_command, mpu_data, mpu_all_off, mpu_load_bank | MPU-401 in *intelligent* mode (command D0h "send data" before each message) — parked |
| `1ace:0a18` `0a59` `0abf` `0b16` `0b5f` | cms_detect, cms_note_on, cms_set_freq, cms_note_off, cms_reset | Game Blaster — parked |
| `1ace:0c15` `0c66` `0ca0` `0cd1` | tandy_note_on, tandy_set_freq, tandy_note_off, tandy_all_off | Tandy 3-voice — parked |
| `1bd7:000c` | dos_free | `INT 21h/49h` on a far pointer (frees the previous instrument block) |
| `1ace:0000`–`0005` | – | epilogue bytes of the preceding module, not a function |

Game side (other specs own the functions; the sound-relevant behaviour is in section 5):

| address | name | spec | sound role |
|---|---|---|---|
| `0c1c:10e8` | timer_isr | platform | calls snd_tick every tick |
| `0c1c:1110` / `0c1c:111d` | sfx_play / sfx_play_ax | sound (here) | effect API |
| `0e12:23df` | engine_sound | sound (here) | engine effect pitch, per frame |
| `01f4:1cf0` / `01f4:1e9a` | snd_init / snd_shutdown | sound (here) | init / exit |
| `01f4:1d22` / `01f4:1e48` | music_for_state / music_play_theme | sound (here) | which song plays |
| `0e12:0630` | radio_next | sound (here) | key M: next race song ("Change radio station") |
| `0000:0f80` | – | game_flow | Ctrl keys: music, effects, engine sound, pause |

---

## 3. Globals

Written/read: offsets inside `1ace` unless a segment is given. "init" = initial value in the EXE.

### Driver state

| DS | name | type | meaning | written by | read by |
|---|---|---|---|---|---|
| C5DE | master_vol | s8 | added to every music velocity (fade) | 004b, 048d | 04e8 |
| C5E0 | snd_ready | u8 | bit 0 music driver ready, bit 2 effects ready | 01cc, 01b6, 01bc, 0ffe | 03d6, 004b, 0185, 0d1c, 0db9, 0e55 |
| C5E1 | snd_active | u8 | bit 0 music playing, bit 2 an effect was started (cleared by any release) | 0006, 004b, 00ec, 0185, 01cc, 0db9, 1072 | 03d6, 0185, 01bc |
| C5E2 | music_ctl | u8 | bit 4 song loaded, 5 fade in, 6 fade out, 7 stop at loop point, 0–3 fade speed | 004b, 00b4, 048d, 0536, 0591, 05ca | 0185, 048d, 0536, 0591 |
| C5E3 | snd_device_id | u8 | 0, 1, 2, 4, 80h (copied to DS:0096 by snd_init) | 02a5 | 02a5, 05df, 01f4:1d0f |
| C5E4 | pit_divisor | u16 | meant to hold 2000h — `timer_install` stores it with DS = CS (bug, see §6); never read | – | – |
| C5E6 | adlib_bd | u8 | shadow of OPL register BDh | 07df, 08c1, 09c2, 1bd8:0028 | same |
| C638 | instr_progmap | u8[128] | program → instrument (AdLib) | 027d, 02fa | 09a1, 0fb1, 0637 |
| C6B8 | instr_veloff | s8[128] | velocity offset per program | 027d, 02fa | 04e8 |
| C738 | – | u8[16] | INSTR.DAT block 100h–10Fh, never read | 027d, 02fa | – |
| C748 | instr_chmap | u8[16] | MIDI channel → voice, > 15 = not played | 027d, 02fa, 0272 | 0006, 03d6, 0d1c, 0db9 |
| C751 | (chmap[9]) | u8 | percussion voice, compared by mpu_note_on | – | 064f |
| C758 | instr_percmap | u8[64] | percussion note 36+i → pitch note (AdLib) | 027d, 02fa | 07df, 064f |
| C798 / C799 | instr_count / instr_size | u8 | 82 / 9 | 02fa | 02fa |
| C79A | snd_default_tables | u8[160] | defaults for C6B8..C757 (speaker/Tandy/CMS): veloffs, zeros, **chmap 00 00 00 FF…** | – | 027d |
| C83B | music_wait | s16 | tick countdown | 004b, 0185, 03d6 | 03d6 |
| C83D | music_pos | far ptr | next event | 004b, 03d6, 03ba | 03d6 |
| C841 | music_status | u8 | status byte of the current event (bit 7 → delta follows) | 03d6, 0536 | 03d6, 0536, 05ca |
| C842 | music_halt | u8 | leave the event loop | 004b, 0185, 048d, 0536, 0591, 05ca | 03d6 |
| C843 | voice_program | u8[16] | program per voice | 05a6, 09a1, 09ed | 04e8 |
| C853 / C859 / C85F / C865 | loop_count / loop_exit / loop_start / loop_status | u16[3] each, contiguous | per-voice loop state, **not reset by music_play** (§4.3) | 0536 | 0536, 05b4 |
| C86B / C86C | fade_count / fade_target | s8 / s8 | fade state | 004b, 048d | 048d |
| C86D | voice_noteword | u16[16] | note << 8 \| frac (frac in 1/256 semitone) | note on/off, 086d, 0ce9, 00ec, **0e12:245f** | 0006, 0ce9, 0ef4, 06a3 |
| C88D | voice_velocity | u8[16] | velocity of the last effect note | 0ef4 | 06a3 |
| C8AD | voice_owner | u8[16] | 0 free, 1 music, 2–3 effect slot + 1 | 0006, 00ec, 0151, 05df, 0db9, note ons | many |
| C8BD | voice_note | u8[16] | device note of the sounding note (AdLib: MIDI − 19; speaker: MIDI − 24) | note on/off, 00ec | note off, 05df, 0ce9, 0d1c, 0db9 |
| C8CD | voice_priority | u8[16] | music 40h, effects 60h–7Fh | 0006, 00ec, 0151, 05df, 0db9, 0fcb | 05df, 0151, 0db9, 0ef4 |
| C8DD | voice_busy | u8[16] | a note was started on the voice this tick | 03d6 (clear), 05df | 05df |
| C8ED | adlib_b0 | u8[16] | shadow of B0h+v (key-on bit 20h); v 6–8 get `frac` | 07df, 0884, 08c1, 09c2 | 0884, 08c1 |
| C8FD | cur_voice | u16 | voice of the current event | 03d6, 09ed | 04e8 |
| C8FF | cur_owner | u8 | 1 music, slot+1 effects | 04e8, 0d1c, 0ef4 | 05df, note ons |
| C900 | engine_off | u8 | Ctrl-V: no engine sound | 0000:0f80 | 0fcb, 0e12:23df |
| C901 | snd_device_ids | u8[5] | 00 01 02 04 80 | init | 02a5 |
| C906..C90E | dev_note_on, dev_note_off, dev_program, dev_all_off, dev_set_freq | near ptr | current device routines (init: all 03D5) | 02a5 | core |
| C910 / C912 / C914 | sfx_step_ptr / sfx_modulate_ptr / mus_hook_ptr | near ptr | 0E55 / 0D1C / 03D5 (constant) | – | 03d6, 05bf |
| C916 / C918 | freq_table / freq_step_table | near ptr | current tables | 02a5 | 0d86 |
| C91A / C924 | dev_freq_tables / dev_freq_step_tables | near ptr[5] | CAAA CADA CA70 C9AA CAAA / CA92 CAC2 CA58 C992 CA92 | – | 02a5 |
| C92E / C938 / C942 / C94C / C956 | dev_*_tbl | near ptr[5] | routine columns (table in §1) | – | 02a5 |
| C960 | s_INSTR_DAT | char[10] | "INSTR.DAT" (current directory) | – | 02fa |
| C96A | instr_data | far ptr | instrument block (offset stays 0, segment C96C) | 02fa | 092a |
| C96E | instr_handle | u16 | DOS handle | 02fa | 02fa |
| C970 / C972 | – | u16 | instr_load scratch: section size, name byte | 02fa | 02fa |
| C974 | mpu_bank_loaded | u8 | parked | 0792 | – |
| C975 | music_handlers | near ptr[8] | 04DE 04E8 0536 0591 05A6 05B4 05BF 05CA | – | 03d6 |
| C985 | event_len | u8[8] | 2 3 2 1 2 1 1 1 (bytes incl. status) | – | 03d6 |
| C98D | force_off | u8 | note off without the same-note check | many | note offs, 06a3 |
| C98E | call_target | u16 | scratch for indirect calls | 03d6, 0e55 | same |
| C990 | instr_ok | u16 | instr_load result | 02fa | 02fa |
| C992 / C9AA | adlib_fstep / adlib_fnum | u16[12] | §4.8 | – | 0d86 |
| C9BC | adlib_rhythm_bit | u8[11] | [6..10] = 10 08 04 02 01 (bytes 0–5 are the tail of C9AA) | – | 07df, 08c1 |
| C9C7 | adlib_op_offset | u8[18] | slot → operator offset | – | 07ad, 092a |
| C9D9 | adlib_voice_slots | u8[22] | voice → (mod slot, car slot), FFh = none | – | 07ad, 092a |
| C9EF | adlib_is_carrier | u8[18] | | – | 07ad |
| CA01 | adlib_op_regs | u8[4] | 20 40 60 80 | – | 092a |
| CA05 | adlib_inst_tl | u8[16] (+2 overlapping CA15) | instrument 40h byte per **slot**; slots 16/17 land on CA15[0]/[1] (harmless, §4.8) | 092a | 07ad |
| CA15 | adlib_inst_conn | u8[16] | instrument C0 bit 0 per **voice** (read per slot: quirk) | 092a | 07ad |
| CA25 | adlib_voice_inst | u8[11] | 00×6, 01 02 03 04 05 | – | 09a1, 09ed |
| CA30 | adlib_perc_voice | u8[40] | percussion note 36..75 → rhythm voice | – | 07df, 08c1 |
| CA58..CA91 | CMS tables, CA88 CMS key-on shadow, CA89 CMS voice bits, CA8F CMS octave shadow | | parked | | |
| CA92 / CAAA | spk_fstep / spk_divisor | s16[12] / u16[12] | §4.9 | – | 0d86 |
| CAC2 / CADA / CAF2 | Tandy step / divisor / channel bits | | parked | | |
| CAF5 | voice_sfx_id | u8[16] | game effect id playing on the voice | 00ec, 0fcb, 0c1c:11e2 | 0fcb, 0c1c:111d, 0e12:23df |
| CB05 | sfx_vib_step | s16[3] | vibrato step (sign flips) | 01cc, 0d1c, 0ec5, 0edf, 0f8d | 0ce9 |
| CB0B | sfx_unused | u16[3] | cleared by sfx_slot_reset, never read | 0f8d | – |
| CB11 | sfx_vib_count | s16[3] | init 1,1,1 | 0d1c, 0ec5, 0f8d | 0d1c |
| CB17 | sfx_vib_period | u16[3] | | 01cc, 0ec5, 0edf, 0f8d | 0d1c |
| CB1D | sfx_slide | s16[3] | added to the noteword every tick | 01cc, 0eab, 0ebe, 0f8d | 0ce9 |
| CB23 | sfx_wait | u16[3] | ticks to skip | 0db9, 0eea, 0f8d | 0e55 |
| CB29 | sfx_ptr | u16[3] | DS offset of the next opcode, 0 = idle | 00ec, 0db9, 0e55 | 0e55 |
| CB2F / CB35 | sfx_loop_start / sfx_loop_count | u16[3] | | 0f70, 0f3e, 0f8d | 0f3e |
| CB3B | sfx_slot_voice | u8[3] | FFh = free; init FF FF FF | 00ec, 0db9 | 0d1c, 0e55, ops |
| CB3C | sfx_ops | near ptr[13] | op handlers; entry 0 overlaps slot_voice[1..2] (op 0 never used) | – | 0e55 |

### Game globals used by the sound code

| DS | name | type | meaning | written by | read by |
|---|---|---|---|---|---|
| 0084 | shared_data_loaded | u8 | NEWWAVE.MUS & menu data loaded into the 65000-byte block (cleared by `stage_load_objects`) | 01f4:53c4, 0792:0d2e | 01f4:53c4 |
| 008E | music_off | u8 | Ctrl-Q | 0000:0f80 | 01f4:1d22, 1e48, 0e12:0630 |
| 008F | sfx_off | u8 | Ctrl-S; 1 for MT-32 (0000:0c68). `01f4:1db4` reads it as a word with DS:0090 (always 0) | 0000:092a, 0f80 | 0c1c:111d, 01f4:1db4 |
| 0096 | snd_device_cfg | u16 | TD3.CFG audio (0 speaker, 1 Tandy, 2 CMS, 3 → 81h, 4 AdLib); after snd_init = C5E3 | 0000:092a, 01f4:1d14 | 01f4:1cf0, 1db4, 0000:0f80 |
| 009C | snd_on | u8 | sound initialised | 01f4:1d1c, 1ebc | 0c1c:111d, 01f4:1d22, 1e48, 1e9a |
| 00A0 | timer_ticks | u16 | ISR tick counter (platform) | 0c1c:10f4 | game loops |
| 0ADA / 0AE3 | scene_path | char[] | "A:SCENE01" + track letter + ".MUS" (drive letter patched by main) | 01f4:1dfe | 0000:0ee0 |
| 129C | engine_rpm | u16 | engine speed (~rpm/10); **clamped to 1500 and written back by engine_sound** | simulation, 0e12:2423 | 0e12:2416 |
| 915D | isr_chain_count | u8 | ISR: chain to the BIOS INT 8 when (count+1)&7 == 0 | 0c1c:10f8 | 0c1c:10fc |
| 915E | sfx_priority | u8[24] | per effect id (table §5.2) | – | 0c1c:119d |
| 9176 | sfx_voice | u8[24] | voice of each effect id, FFh = none (9177 = engine) | 0c1c:1137/117a/11d6, 1ace:0ff2 | 0c1c:111d, 0e12:23df |
| 918E | sfx_script | u16[24] | DS offsets of the scripts (DS:91BE–944B) | – | 0c1c:11b9 |
| 93E6 | engine_script_note | u8 | note byte of the engine script's `note` opcode, patched every frame | 0e12:2449 | 0ef4 |
| 90DE | sfx_15_16_started | u8 | effect 15 or 16 started (skid sounds; cleared by 0977:0008 when it stops them) | 0c1c:11f0, 0977 | 0977 |
| 9486 | damage_flags | u16 | simulation; **bit 5 = engine damaged** (likely): engine sound 4 semitones lower | simulation, 0e12:0e74 | 0e12:242f |
| 948B | replay_active | u8 | instant replay running: only effects 2 and 6 play, engine_sound does nothing, race song not started | game_flow | 0c1c:1148, 0e12:23df, 01f4:1e15 |
| 9596 | leg_siren_alt | u8 | byte 4 of the leg map `<scene>A..E.DAT` (loaded to DS:9592): ≠ 0 → siren id 20 becomes 23. 0 in every shipped leg | file | 0c1c:1160 |
| B6CC | radio_station | u16 | race song A/B/C (0..2); 0 at start and on scene select, key M cycles | 0000:0000, 01f4:0070, 01f4:3636, 0e12:0630 | music_for_state callers |
| E77A | scene_music_buf | far ptr | 3630-byte buffer for `<scene>A/B/C.MUS` | game_flow | 01f4:1d22 |
| E7F0 | music_buf | far ptr | THEME.MUS / NEWWAVE.MUS (6000 bytes inside the 65000-byte block DS:E770) | game_flow | 01f4:1d22, 1e48, 53c4 |

---

## 4. Pseudocode

C, faithful to the original widths. `W(a)` = u16 at DS:a (effect scripts). The device routines are
called through a table `dev` (section 1). `force_off` is the global DS:C98D. Every "sic" is a quirk
the port keeps.

### 4.1 Device interface and init

```c
typedef struct {
    void (*note_on)(int v, u8 note, u8 frac, u8 vel, u8 prio);   /* SI = 2*v */
    void (*note_off)(int v, u8 note);                            /* uses force_off */
    void (*program)(int v, u8 prog);
    void (*all_off)(int v);
    void (*set_freq)(int v, int si, u16 nw);   /* si: noteword slot the folded value is stored to */
} DevRoutines;

/* 1ace:027d */
void snd_reset_tables(void) {
    memset(&DS[0xC638], 0, 0x162);                 /* progmap .. instr_size */
    memcpy(&DS[0xC6B8], &DS[0xC79A], 0xA0);        /* veloff, C738, chmap (00 00 00 FF..), percmap */
}

/* 1ace:02a5 */
void snd_select_routines(int idx) {
    snd_device_id = snd_device_ids[idx];            /* 00 01 02 04 80 */
    dev = dev_table[idx]; freq_table = dev_freq_tables[idx]; freq_step_table = dev_freq_step_tables[idx];
    if (snd_device_id == 4) {
        /* strike a silent snare: sets up SD/HH operator TL and BD bit 08h, then clears it.
         * CL (frac) and DH are whatever the caller left: CL = the CX that main had when it called
         * snd_init. Port: 0 (see open questions). */
        adlib_note_on(9, 0x26, /*frac*/0, /*vel*/0, /*prio*/0);
        adlib_note_off(9, 0x26);                    /* force_off = 0 */
    }
}

/* 1ace:01cc */
void far music_init(u16 dev_cfg) {
    snd_active = 0;
    sfx_vib_step[0] = 0; sfx_slide[0] = 0;
    snd_reset_tables();
    int idx = 0;
    if (dev_cfg & 0x80) {                           /* parked: MPU-401 */
        if (mpu_reset() && mpu_load_bank(dev_cfg & 0x7F)) idx = 4;
    } else if (dev_cfg == 4) {
        if (adlib_detect_reset() && adlib_load_bank()) idx = 3;
    } else if (dev_cfg == 2) {                      /* parked: Game Blaster */
        if (cms_detect()) { cms_reset(); snd_identity_chmap(5); idx = 2; }
    } else if (dev_cfg == 1) {                      /* parked: Tandy, no probe */
        snd_identity_chmap(2); idx = 1;
    }
    if (idx == 0) {                                 /* PC speaker, also every fallback */
        sfx_vib_period[0] = 12;                     /* 0x0C: slot-0 vibrato for music voices */
        sfx_vib_step[0]   = 1;
        outp(0x43, 0xB6);                           /* PIT ch 2, lo/hi, mode 3 square wave */
    }
    snd_select_routines(idx);
    snd_ready |= 1;
}

/* 1ace:0ffe */
void far sfx_init(u16 dev_cfg) {
    snd_reset_tables();
    int idx = 0;  u8 d = (u8)dev_cfg;
    if (d & 0x80)      { if (mpu_reset() && mpu_load_bank(1)) idx = 4; }
    else if (d == 4)   { if (adlib_detect_reset() && adlib_load_bank()) idx = 3; }
    else if (d == 2)   { if (cms_detect()) { cms_reset(); snd_identity_chmap(5); idx = 2; } }
    else if (d == 1)   { snd_identity_chmap(2); idx = 1; }
    snd_select_routines(idx);                        /* no speaker defaults here */
    snd_ready |= 4;
}

/* 1ace:02fa — section 0 = AdLib. Returns 1 on success. */
int instr_load(int section) {
    if (instr_data) dos_free(instr_data);           /* the second init frees the first block */
    fd = open("INSTR.DAT", read-only);  if (fail) return 0;       /* no close on this path */
    for (;;) {
        read(fd, &size16, 2);                       /* fail → close, return 0 */
        if (section == 0) break;
        if (size16 == 0) { close; return 0; }
        lseek(fd, size16, SEEK_CUR); section--;
    }
    do read(fd, &c, 1); while (c != 0);             /* device name, ignored */
    read(fd, &DS[0xC638], 0x162);                   /* table block */
    u16 n = (s8)instr_count * (s8)instr_size;       /* imul byte: 82*9 = 738 */
    if (n) { instr_data = dos_alloc((n >> 4) + 1 paragraphs); read(fd, instr_data, n); }
    close(fd);
    return 1;
}
```

`snd_init` runs `sfx_init(cfg)` **then** `music_init(cfg)`, so the AdLib probe, the chip reset, the
bank load and the silent snare all happen twice; the final state is that of one run.

### 4.2 The tick (`1ace:03d6`)

```c
void far snd_tick(void) {                           /* from the timer ISR, 145.652 Hz */
    memset(voice_busy, 0, 16);
    sfx_modulate();                                 /* via DS:C912 */
    sfx_step();                                     /* via DS:C910 */
    if (!(snd_ready & 1) || !(snd_active & 1)) return;
    music_fade();
    if (--music_wait > 0) return;                   /* s16: events run when it reaches <= 0,
                                                       so delta 0 and delta 1 both mean 1 tick */
    u8 far *p = music_pos;
    for (;;) {
        u8 st = p[0], a1 = p[1], a2 = p[2];         /* reads 4 bytes regardless of length */
        music_status = st;
        u8 v = instr_chmap[st & 0x0F];              /* may be 0xFF */
        cur_voice = v;
        int type = (st >> 4) & 7;
        p += event_len[type];                       /* 2 3 2 1 2 1 1 1 */
        music_handlers[type](v, a1, a2, &p);        /* CL = st & 0Fh, SI = 2*v (unmasked) */
        if (music_halt & 1) break;
        if (music_status & 0x80) {                  /* status may have been replaced by a loop jump */
            u16 d = *p++;
            if (d & 0x80) d = (d & 0x7F) | ((u16)*p++ << 7);
            music_wait = d;
            break;
        }
    }
    music_pos = p;
}

/* 1ace:048d — unused by the game (ctl is always 10h, 80h or 0) */
void music_fade(void) {
    u8 c = music_ctl & ~0x10;
    if (c == 0) return;
    if ((s8)--fade_count >= 0) return;
    fade_count = c & 0x0F;
    if (c & 0x80) return;
    /* sic: tests bit 5 of (c & 0x0F) → always the fade-out branch; fade-in is unreachable */
    if ((u8)--master_vol == 0x81) { music_ctl = 0; music_halt = 1; music_notes_off(); }
}
```

### 4.3 Music events and loop handling

```c
/* type 0, 1ace:04de */
void mus_ev_note_off(u8 v, u8 note) { if (v <= 13) dev.note_off(v, note); }

/* type 1, 1ace:04e8 */
void mus_ev_note_on(u8 v, u8 note, u8 vel) {
    if (v > 13) return;
    if (vel == 0) { mus_ev_note_off(v, note); return; }
    cur_owner = 1;
    s16 x = (s8)instr_veloff[voice_program[v]] + vel;
    if (x < 0) x = 0; else if (x >= 0x7F) x = 0x7F;
    x += master_vol;
    if (x < 0) x = 1; else if (x >= 0x7F) x = 0x7F;   /* x == 0 stays 0: a silent note is still played */
    dev.note_on(cur_voice, note, 0, (u8)x, 0x40);
}

/* type 2, 1ace:0536 — index = voice (SI = 2*v; the "and bl,3" is dead). Arrays are 3 long;
 * only channel 0 (voice 0) carries loop events in the data. */
void mus_ev_loop(u8 v, u8 count, u8 far **pp) {
    if (music_ctl & 0x80) { music_halt = 1; music_ctl = 0; music_notes_off(); return; }
    if (count == 0) { loop_start[v] = *pp; loop_status[v] = music_status; return; }
    if (loop_count[v] == 0) {
        if (loop_start[v] == 0) return;             /* offset test */
        loop_count[v] = (s8)count;                  /* cbw */
        loop_exit[v] = *pp;
    } else if ((u8)loop_count[v] == 1) {
        loop_count[v]--;                            /* last pass: fall through */
        return;
    }
    *pp = loop_start[v];
    music_status = (u8)loop_status[v];              /* loop start is "20 00": no delta after a jump */
    loop_count[v]--;
}
/* count = total number of passes; count 1 would loop forever. Every song ends with
 * "A0 02 <d> 20 02 FC": the two loop ends make the song repeat for ever (FC is never reached):
 * fresh state: body, body, d, body, d, body ...  */

/* type 3, 1ace:0591 */ void mus_ev_stop_point(void) { if (music_ctl & 0x80) { music_halt = 1; music_ctl = 0; music_notes_off(); } }
/* type 4, 1ace:05a6 */ void mus_ev_program(u8 v, u8 prog) { if (v <= 13) { voice_program[v] = prog; dev.program(v, prog); } }
/* type 5, 1ace:05b4 */ void mus_ev_loop_exit(u8 v, u8 far **pp) { if (loop_exit[v]) *pp = loop_exit[v]; }
/* type 6, 1ace:05bf */ void mus_ev_hook(void) { /* cli */ mus_hook(); /* = ret; sti */ }
/* type 7, 1ace:05ca */ void mus_ev_end(void) { if (music_status == 0xFC) { music_halt = 1; music_ctl = 0; music_notes_off(); } }
```

**Loop state persists across songs** (neither `music_play` nor anything the game calls clears
C853..C86A). After a song has been cut in its second or later pass, `loop_count[0]` is 1; the next
song's first `A0 02` then falls through and its first repeat gets the `<d>` gap (3–6 ticks) like all
later ones. The port keeps one loop state for the driver's lifetime (the DRO references start from a
fresh state, section 10).

Music reaches voices held by an effect in two ways the port must keep: a music **note off** whose
note equals the effect's current note keys it off (no owner check), and a music **program change**
on voice 4/5 reloads the instrument under a playing effect.

### 4.4 Music API

```c
/* 1ace:004b — args are stack words: far ptr, fade (byte), vol (byte) */
void far music_play(u8 far *song, u8 fade, s8 vol) {
    music_notes_off();
    if (song == NULL) return;
    music_pos = song + 2;                           /* skip the u16 length */
    music_ctl = 0x10;
    if (fade) {                                     /* unused: game passes 0 */
        music_ctl |= (fade & 0x0F) | 0x20; fade_target = vol; fade_count = 0; master_vol = (s8)0x81;
    } else master_vol = vol;
    music_wait = 1; music_halt = 0;                 /* first events on the next tick */
    music_claim_voices();
    if (snd_ready & 1) snd_active |= 1;
}

/* 1ace:00b4 */
void far music_stop(u8 mode) {
    if (mode == 0) music_notes_off();               /* the only mode the game uses */
    else if (mode & 0x80) music_ctl = 0x80;         /* stop at the next loop (or Bx) event */
    else music_ctl = (music_ctl & 0xF0) | (mode & 0x0F) | 0x40;
}

/* 1ace:0006 */
void music_notes_off(void) {
    snd_active &= ~1;
    for (int c = 15; c >= 0; c--) {
        u8 v = instr_chmap[c];
        if (v > 15 || voice_owner[v] != 1) continue;
        force_off = 1; dev.note_off(v, voice_noteword[v] >> 8); force_off = 0;
        voice_owner[v] = 0; voice_priority[v] = 0;
    }
}   /* AdLib: voice 9 gets note (noteword[9]>>8) = 0 → treated as 36 → only the BD bit is cleared;
       SD/TT/CY/HH bits set by the last notes stay in BDh (quirk, keep). */

/* 1ace:0151 */
void music_claim_voices(void) {
    for (int v = 15; v >= 0; v--) {
        if (voice_owner[v] >= 2) {
            if (voice_priority[v] > 0x40) continue;
            voice_release(voice_owner[v]);          /* sic: owner id, not v. Unreachable: all effects are >= 60h */
        }
        voice_owner[v] = 1; voice_priority[v] = 0x40;
    }
}

/* 1ace:0185, no callers */
void far music_resume(void) {
    if ((music_ctl & 0x10) && !(snd_active & 1) && (snd_ready & 1)) {
        music_wait = 1; music_halt = 0; snd_active |= 1; music_claim_voices();
    }
}
/* 1ace:01b6 */ void far music_enable(void)  { snd_ready |= 1; }
/* 1ace:01bc */ void far music_disable(void) { if (snd_active & 1) { music_notes_off(); snd_ready &= ~1; } }
/* 1ace:03ba */ void music_reset(void) { dev.all_off(BX); memset(&DS[0xC83D], 0, 22); }  /* no callers */
```

### 4.5 Voice allocation, priorities, stealing

```c
/* 1ace:05df — true = reject (CF). Called by every note on with the device note n. */
bool voice_alloc_check(int v, u8 n, u8 prio) {
    if (cur_owner > 1) { voice_busy[v] = 1; return false; }     /* effects always get their voice */
    if (prio < voice_priority[v]) return true;                 /* unsigned; music = 40h */
    if (voice_owner[v] != 1) {                                 /* free voice → music takes it back */
        voice_release(v);                                      /* no-op unless owner > 1 */
        voice_owner[v] = 1; voice_priority[v] = 0x40;
    }
    if (snd_device_id == 0x80) { voice_busy[v] = 1; return false; }
    if (voice_busy[v] && n <= voice_note[v]) return true;      /* same tick: the highest note wins */
    voice_busy[v] = 1; return false;
}

/* 1ace:00ec */
void far voice_release(u16 v) {
    if (v > 15) return;
    u8 o = voice_owner[v];
    if ((s8)o <= 1) return;
    voice_sfx_id[v] = 0;
    sfx_slot_voice[o - 1] = 0xFF; sfx_ptr[o - 1] = 0;
    force_off = 1; dev.note_off(v, voice_note[v]); force_off = 0;
    voice_priority[v] = 0; voice_noteword[v] = 0; voice_note[v] = 0; voice_owner[v] = 0;
    snd_active &= ~4;
}

/* 1ace:0db9 — voice stealing */
s16 far sfx_start(u16 script, u16 prio) {
    if (!(snd_ready & 4)) return -1;
    snd_active |= 4;
    u8 id = 3;                                      /* owner id = slot + 1 */
    u16 c = 8; u8 v;
    for (;;) { v = instr_chmap[c]; if (v < 9) break; if (--c == 0) break; }
    /* v = voice of the highest channel 8..1 mapped below 9: AdLib 5, PC speaker 0 */
    int bx = v;
    if (v != 0 && voice_owner[bx] == 3) {           /* slot 2 busy → try voice v-1, slot 1 */
        bx--; id = 2;
        if (voice_owner[bx] == 2 && (s8)voice_priority[bx] >= (s8)voice_priority[bx + 1]) { bx++; id = 3; }
    }                                               /* both busy → the lower priority one, tie → voice 5 */
    if ((s8)voice_priority[bx] > (s8)(u8)prio) return -1;     /* equal priority replaces */
    force_off = 1; dev.note_off(bx, voice_note[bx]); force_off = 0;
    voice_owner[bx] = id;  voice_priority[bx] = (u8)prio;
    int s = id - 1;
    sfx_slot_voice[s] = bx; sfx_ptr[s] = script; sfx_wait[s] = 1;
    return bx;                                      /* slot modulation state is NOT reset: scripts start with op 10 */
}

/* 1ace:1072 */
void far sfx_stop_all(void) {
    snd_active &= ~4;
    for (int s = 2; s >= 0; s--) voice_release(sfx_slot_voice[s]);    /* FFh → ignored */
}
```

Priorities (DS:915E): music 40h; engine 60h; effects 68h–7Fh; 7Fh additionally stops all effects
first (`sfx_play_ax`). Consequences on AdLib: effects use voice 5 (slot 2), then voice 4 (slot 1);
music notes on channels 4/5 are dropped while an effect holds the voice and come back with the next
note-on after `voice_release`. In a race the engine permanently holds voice 5, so every other effect
plays on voice 4 (or replaces the engine when both are taken and the engine has the lower priority).
On the PC speaker every effect and all music share voice 0: an effect (≥ 60h) silences music notes,
which is why the race song is only started on the speaker when effects are off.

### 4.6 Effect engine

```c
/* 1ace:0e55 */
void sfx_step(void) {
    if (!(snd_ready & 4)) return;
    for (int s = 2; s >= 0; s--) {
        if (sfx_slot_voice[s] == 0xFF) continue;
        u16 p = sfx_ptr[s];
        if (p == 0) continue;
        if (sfx_wait[s]) { sfx_wait[s]--; continue; }
        s16 op = (s16)W(p);
        if (op > 12) op = 9;                        /* signed: 0 and negatives are never in the data */
        sfx_ptr[s] = sfx_ops[op](s, p + 2);         /* one opcode per slot per tick */
    }
}
/* Timing: every opcode takes one tick; "wait n" takes n+1 ticks. sfx_start sets wait=1 and the
 * leading reset sets it again, so in a "reset; program; note" script the note runs on the 5th tick
 * after sfx_start (tick 1 skip, 2 reset, 3 skip, 4 program, 5 note). */

u16 op_slide(int s, u16 p)       { s16 n = W(p+2); sfx_slide[s] = W(p) ? n : -n; return p + 4; }   /* 1: 0eab */
u16 op_slide_off(int s, u16 p)   { sfx_slide[s] = 0; return p; }                                  /* 2: 0ebe */
u16 op_vibrato(int s, u16 p) {                                                                     /* 3: 0ec5 */
    u16 per = W(p);
    sfx_vib_period[s] = per; sfx_vib_count[s] = (per >> 1) | 1; sfx_vib_step[s] = W(p+2);
    return p + 4;
}
u16 op_vibrato_off(int s, u16 p) { sfx_vib_step[s] = 0; sfx_vib_period[s] = 0; return p; }       /* 4: 0edf */
u16 op_wait(int s, u16 p)        { sfx_wait[s] = W(p); return p + 2; }                            /* 5: 0eea */
u16 op_note(int s, u16 p) {                                                                        /* 6: 0ef4 */
    cur_owner = s + 1;
    int v = sfx_slot_voice[s];
    if (voice_noteword[v] != 0) { force_off = 1; dev.note_off(v, voice_note[v]); force_off = 0; }
    u8 note = (u8)W(p);                             /* MIDI note (low byte; DS:93E6 for the engine) */
    u16 vel = W(p+2);
    voice_velocity[v] = (u8)vel;
    if (vel) dev.note_on(v, note, 0, (u8)vel, voice_priority[v]);     /* vel 0 = rest */
    return p + 4;
}
u16 op_loop_start(int s, u16 p)  { sfx_loop_start[s] = p + 2; return p + 2; }                    /* 7: 0f70, arg ignored */
u16 op_loop_end(int s, u16 p) {                                                                    /* 8: 0f3e, 1st arg ignored */
    u16 c = sfx_loop_count[s];
    if (c) { if (c != 0xFFFF) c--; sfx_loop_count[s] = c; return c ? sfx_loop_start[s] : p + 4; }
    c = W(p+2); if (c == 0) c = 0xFFFF;             /* count+1 passes; 0 = forever */
    sfx_loop_count[s] = c;
    return sfx_loop_start[s];
}
u16 op_end(int s, u16 p) { voice_release(sfx_slot_voice[s]); sfx_slot_reset(s); return 0; }     /* 9: 0f78 */
u16 op_reset(int s, u16 p) { sfx_slot_reset(s); return p; }                                       /* 10: 0f8d */
u16 op_program(int s, u16 p) {                                                                     /* 11: 0fb1 */
    dev.program(sfx_slot_voice[s], instr_progmap[W(p)]);   /* sic: AdLib maps through C638 again */
    return p + 2;
}
u16 op_goto_engine(int s, u16 p) {                                                                 /* 12: 0fcb */
    if (engine_off == 0) {
        for (int i = 15; i >= 0; i--) if (voice_sfx_id[i] == 1) return op_end(s, p);
        int v = sfx_slot_voice[s];
        voice_priority[v] = 0x60; voice_sfx_id[v] = 1;
        sfx_voice[1] = v;                           /* DS:9177, the game's table */
        return W(p);                                /* = 93DE, the engine script */
    }
    return op_end(s, p);
}
void sfx_slot_reset(int s) {                                                                       /* 0f8d */
    sfx_loop_start[s] = sfx_loop_count[s] = 0; sfx_vib_step[s] = sfx_vib_period[s] = 0;
    sfx_unused[s] = 0; sfx_slide[s] = 0; sfx_vib_count[s] = 1; sfx_wait[s] = 1;
}

/* 1ace:0d1c, first thing every tick */
void sfx_modulate(void) {
    for (int s = 2; s >= 0; s--)
        if (--sfx_vib_count[s] < 0) { sfx_vib_step[s] = -sfx_vib_step[s]; sfx_vib_count[s] = sfx_vib_period[s]; }
    if (snd_ready & 4)
        for (int s = 2; s >= 1; s--) {              /* effect slots 2 and 1 */
            cur_owner = s + 1;
            if (sfx_slot_voice[s] != 0xFF) voice_modulate(sfx_slot_voice[s], s);
        }
    cur_owner = 1;
    for (int c = 8; c >= 0; c--) {                  /* music: note the index mix-up */
        if (voice_owner[c] != 1) continue;          /* owner of VOICE c ... */
        u8 v = instr_chmap[c];                      /* ... but the voice of CHANNEL c is modulated */
        if (v > 15 || voice_note[v] == 0) continue;
        voice_modulate(v, 0);                       /* slot 0: AdLib 0/0 (no effect); speaker step 1, period 12 */
    }
}

/* 1ace:0ce9 */
void voice_modulate(int v, int s) {
    u16 nw = voice_noteword[v] + sfx_slide[s] + sfx_vib_step[s];
    if (nw == voice_noteword[v] && voice_note[v] == (nw >> 8)) return;
    voice_noteword[v] = nw;
    dev.set_freq(v, 2*v, nw);
}
```

Vibrato is a triangle on the noteword: `step` is added every tick and changes sign every
`period+1` ticks (first after `(period>>1)|1`); amplitude ≈ step·(period+1)/2 in 1/256 semitone.
`period 0` flips every tick (a ±step trill). Slide and vibrato keep running after a rest: a rest's
note off zeroes the noteword (§4.8), so the next ticks re-send A0h/B0h with a pitch near note 0 while
the release sounds (effects 13, 18, 19).

On the PC speaker `voice_owner[1..2]` are music (1) and `chmap[1..2] = 0`, so voice 0 gets the
slot-0 vibrato applied **once per channel 0–2 whose voice owner is 1**: three times per tick for
music, twice during an effect (owner[0] = 3 skips channel 0).

### 4.7 Effect scripts (DGROUP 91BE–944B, u16 words)

Complete listing (op names as §4.6; `note n v` = MIDI note, velocity; `vib p s`; `slide sign n`).
All 23 ids point into it (`DS:918E`); scripts overlap and chain (`goto_engine 93DE`).

```
91BE (13,18): reset; program 47; loop_start; slide 1 30; vib 8 20; note 70 110; wait 10; note 70 0;
              wait 5; loop_end 0 1; slide 1 15; note 68 110; wait 8; note 74 110; wait 15; goto_engine 93DE
920C (3):     reset; program 65; slide 0 50; vib 2 225; note 67 120; wait 30; goto_engine 93DE
922C (4,11):  reset; program 47; slide 1 50; note 45 120; vib 0 160; wait 15; goto_engine 93DE
924C (5,10,12,22): reset; program 47; note 50 120; vib 20 16; slide 1 600; wait 30; goto_engine 93DE
926C (2):     reset; program 47; note 78 120; vib 4 120; wait 45; program 47; note 80 120; slide 0 125;
              vib 20 600; loop_start; wait 40; loop_end 0 3; 00FFh (= end)
92A8 (20):    reset; program 47; note 80 110; vib 275 4; loop_start; wait 100; loop_end 0 0
92C8 (17):    reset; program 18; vib 12 30; note 30 120; wait 16; goto_engine 93DE
92E2 (9):     reset; program 47; note 40 120; vib 5 75; slide 0 20; wait 20; vib 0 1000; slide 0 50;
              note 50 120; wait 75; goto_engine 93DE
9318 (14):    reset; program 47; note 84 115; wait 10; goto_engine 93DE
932C (7):     reset; program 102; note 84 120; wait 20; note 84 0; wait 10; note 80 120; wait 20; goto_engine 93DE
9354 (15):    reset; program 47; note 80 120; vib 2 200; wait 150; goto_engine 93DE
936E (16):    reset; program 47; note 78 120; vib 2 200; wait 150; goto_engine 93DE
9388 (6):     reset; program 47; vib 20 500; loop_start; note 80 120; slide 0 200; wait 40; vib 25 300;
              wait 70; 00FFh (= end)
93B4 (8):     reset; program 88; vib 4 30; note 44 120; wait 100; slide 0 10; wait 50; slide_off; wait 100;
              goto_engine 93DE
93DE (1,21):  reset; program 12; note [93E6]=20 120; vib 4 31; loop_start; loop_end 0 0   (engine, endless)
93FA (23):    reset; program 82; loop_start; note 88 110; wait 50; note 85 120; wait 50; loop_end 0 0
941E (19):    reset; program 31; slide 0 125; note 45 120; wait 15; note 35 120; wait 6; note 35 0; wait 5;
              goto_engine 93DE
```

Encoding: opcode word, then 0–2 argument words (op 1: sign, n; 3: period, step; 5: n; 6: note, vel;
7: x; 8: x, count; 11: prog; 12: address). The port should export this block from the EXE (like the
other DGROUP tables) into a **writable** u16 array addressed by DS offset, because `engine_sound`
patches the byte at 93E6.

### 4.8 AdLib routines

Tables (DGROUP, verified against the EXE by `td3snd.py check_tables`):

```
adlib_fnum  C9AA[12] = 205 223 244 267 28B 2B2 2DB 306 334 365 399 3CF   (hex; n%12 = 0 is G)
adlib_fstep C992[12] =   1   2   2   2   2   2   3   3   3   3   3   3   (F-number per 1/16 semitone)
adlib_op_offset C9C7[18] = 00 01 02 03 04 05 08 09 0A 0B 0C 0D 10 11 12 13 14 15
adlib_voice_slots C9D9[22] = (0,3) (1,4) (2,5) (6,9) (7,10) (8,11) (12,15) (16,FF) (14,FF) (17,FF) (13,FF)
adlib_is_carrier C9EF[18] = 0 0 0 1 1 1  0 0 0 1 1 1  0 0 0 1 1 1
adlib_op_regs CA01 = 20 40 60 80
adlib_rhythm_bit C9BC[6..10] = 10 08 04 02 01            (BD SD TT CY HH)
adlib_voice_inst CA25[11] = 0 0 0 0 0 0 1 2 3 4 5
adlib_perc_voice CA30[40] (notes 36..75) =
   6 7 7 7 7 8 A 8 A 8 A 8 8 9 8 9 F F A F A F F F 8 8 8 8 8 8 8 8 8 8 A F F 8 F 8     (F = none)
   (notes 76+ read on into CA58: 02 00 02 00 02 00 01 00 … — not used by the data)
```

Driver note 0 = MIDI 19 (G0, 24.5 Hz); pitch is exact MIDI (MIDI 60 → n 41 → block 3, fnum 2B2h
→ 261.7 Hz).

```c
/* 1bd8:000e — port: write straight into the emulator, drop the status-read delays */
void adlib_write(u8 reg, u8 val);

/* 1bd8:0028 */
int adlib_detect_reset(void) {
    adlib_write(0x04, 0x60); adlib_write(0x04, 0x80); u8 s1 = inp(0x388);
    adlib_write(0x02, 0xFF); adlib_write(0x04, 0x21); delay (CX = CH:C8h status reads); u8 s2 = inp(0x388);
    adlib_write(0x04, 0x60); adlib_write(0x04, 0x80);
    if ((s1 & 0xE0) != 0 || (s2 & 0xE0) != 0xC0) return 0;
    for (u16 r = 0xF5; r >= 1; r--) adlib_write((u8)r, 0);   /* every register F5h..01h = 0 (C0-C8, E0-F5 too) */
    adlib_write(0x04, 0x60);
    adlib_bd = 0x20; adlib_write(0xBD, 0x20);       /* rhythm mode on, all drums off */
    adlib_write(0x01, 0x20);                        /* waveform select enabled; waveforms stay 0 */
    return 1;
}

/* 1ace:07ad — idx = index into adlib_voice_slots (2v = modulator, 2v+1 = carrier) */
void adlib_set_tl(int idx, u8 vel2) {
    u8 slot = adlib_voice_slots[idx];
    u8 tl;
    if (adlib_is_carrier[slot] == 1 || (s8)slot > 6 || adlib_inst_conn[slot] != 0)   /* sic: conn is per voice */
        tl = (0x3F - vel2) & 0x3F;                  /* KSL = 0 */
    else
        tl = adlib_inst_tl[slot];                   /* instrument byte (KSL|TL) */
    adlib_write(0x40 + adlib_op_offset[slot], tl);
}

/* 1ace:07df */
void adlib_note_on(int v, u8 note, u8 frac, u8 vel, u8 prio) {
    if (v == 9) {                                   /* percussion channel */
        u8 i = note - 0x24;
        s8 rv = adlib_perc_voice[i];                /* byte index; i >= 40 reads the next tables */
        if (rv >= 11) return;                       /* signed */
        u8 pitch = instr_percmap[i];
        adlib_set_tl(2*rv, vel >> 1);               /* every drum operator gets velocity TL (BD mod too) */
        if (rv < 7) adlib_set_tl(2*rv + 1, vel >> 1);
        adlib_bd |= adlib_rhythm_bit[rv];           /* no 0→1 edge if the bit is already set */
        adlib_write(0xBD, adlib_bd);
        adlib_b0[rv] = frac;                        /* sic: key-on bit of B6..B8 = frac & 20h (0 for music) */
        adlib_set_freq(rv, 2*rv + (rv < 7), pitch << 8 | frac);  /* rv 9,10 return at once;
                                                       BD stores the folded word at byte offset 13 of
                                                       voice_noteword (nothing reads it) */
        return;
    }
    s8 n = note; while (n < 0x13) n += 12; n -= 0x13;          /* driver note = MIDI - 19 */
    if (voice_alloc_check(v, n, prio)) return;
    voice_owner[v] = cur_owner;
    force_off = 1; adlib_note_off(v, n); force_off = 0;        /* key off first */
    voice_noteword[v] = n << 8 | frac;
    voice_note[v] = n;
    adlib_set_tl(2*v, vel >> 1);                    /* modulator (quirks) */
    adlib_set_tl(2*v + 1, vel >> 1);                /* carrier: TL = 63 - vel/2 */
    adlib_b0[v] = 0x20;
    adlib_set_freq(v, 2*v, n << 8 | frac);
}

/* 1ace:0884 (+ note_fold 086d with AH = 60h, AL = 0, + freq_lookup 0d86) */
void adlib_set_freq(int v, int si, u16 nw) {
    if (v >= 9) return;
    s8 n = nw >> 8; u8 frac = (u8)nw;
    while (n > 0x60) n -= 12;                       /* signed: 0x80..0xFF count as negative */
    while (n < 0) n += 12;
    voice_noteword[si / 2] = (u8)n << 8 | frac;     /* 16-bit store at byte offset si */
    u8 oct = (u8)n / 12, semi = (u8)n % 12;
    u16 f = adlib_fnum[semi] + (frac >> 4) * adlib_fstep[semi];   /* max 3FCh */
    adlib_write(0xA0 + v, (u8)f);
    u8 b = (u8)(oct << 2) | (u8)(f >> 8) | (adlib_b0[v] & 0x20);
    adlib_b0[v] = b;                                /* n = 96: block 8 → 20h = key-on, block 0 (quirk) */
    adlib_write(0xB0 + v, b);
}

/* 1ace:08c1 */
void adlib_note_off(int v, u8 note) {
    if (v > 9) return;
    if (v == 9) {
        s8 n = note; if (n < 0x24) n = 0x24;
        u8 rv = adlib_perc_voice[(u8)(n - 0x24)];
        if (rv > 10) return;
        adlib_bd &= ~adlib_rhythm_bit[rv];
        adlib_write(0xBD, adlib_bd);
        voice_noteword[9] = 0;                      /* SI still 2*9 */
        voice_note[rv] = 0;
        return;
    }
    if (!force_off) {
        s8 n = note - 0x13; while (n < 0) n += 12;
        if ((u8)n != voice_note[v]) return;         /* stale note off: ignored */
    }
    adlib_b0[v] &= 0x1F;
    adlib_write(0xB0 + v, adlib_b0[v]);
    voice_noteword[v] = 0;                          /* also on the melodic path (0917..0922) */
    voice_note[v] = 0;
}

/* 1ace:092a */
void adlib_load_instrument(int v, u8 inst) {
    const u8 *p = instr_data + inst * 9;
    adlib_inst_conn[v] = p[8] & 1;
    adlib_inst_tl[adlib_voice_slots[2*v]] = p[1];
    if (adlib_voice_slots[2*v + 1] != 0xFF) adlib_inst_tl[adlib_voice_slots[2*v + 1]] = p[5];
    for (int k = 0; k < 2; k++) {
        u8 slot = adlib_voice_slots[2*v + k];
        if (slot == 0xFF) return;                   /* single-operator drums: no C0 write */
        for (int r = 0; r < 4; r++) adlib_write(adlib_op_offset[slot] + adlib_op_regs[r], *p++);
    }
    adlib_write(0xC0, *p);                          /* sic: register C0h, not C0h+v */
}

/* 1ace:09a1 */
void adlib_program(int v, u8 prog) {
    if (v < 6) { voice_program[v] = prog; adlib_load_instrument(v, instr_progmap[prog]); }
    else voice_program[v] = adlib_voice_inst[v];    /* drums fixed; v 11..13 read CA30[0..2] */
}

/* 1ace:09c2 — only reached from the dead music_reset */
void adlib_all_off(int bx) {
    for (int c = 8; c >= 0; c--) { adlib_b0[bx] &= 0x1F; adlib_write(0xB0 + c, adlib_b0[bx]); }  /* sic */
    adlib_bd = 0x20; adlib_write(0xBD, 0x20);
}

/* 1ace:09ed */
int adlib_load_bank(void) {
    if (!instr_load(0)) return 0;
    for (int v = 10; v >= 0; v--) {
        voice_program[v] = adlib_voice_inst[v]; cur_voice = v;
        adlib_load_instrument(v, adlib_voice_inst[v]);
    }
    return 1;                                       /* AX of the last write, never 0 */
}
```

The quirks, summarised (all verified in the bytes; keep them):

* **TL formula**: carrier TL = `(63 − (vel>>1)) & 63`, KSL forced to 0; there is no volume curve.
* **Modulator TL (voices 3–5 quirk)**: a modulator keeps the instrument's 40h byte only if its
  *slot* ≤ 6 and `CA15[slot]` (a per-*voice* array) is 0. Voices 0–2 (slots 0–2) behave as intended
  (additive instruments get velocity TL on both operators). Voice 3 (slot 6) looks at voice 6's
  connection bit (BD instrument, 0) → always instrument TL. Voices 4 and 5 (slots 7, 8) always get
  the velocity TL on the modulator, i.e. their FM depth follows the velocity — this is the voice
  pair all effects play on.
* **C0h quirk**: only register C0h is ever written; channel 0's feedback/connection is the byte 8
  of the *last instrument loaded anywhere* (including effect `program` ops on voices 4/5); channels
  1–8 stay at C0 = 0 (FM, no feedback) from the reset.
* **Effect program mapped twice**: op 11 passes `C638[p]` to `adlib_program`, which maps it again
  (engine: program 12 → 32 → instrument 25).
* **Rhythm**: BD register bit OR'ed without a 0→1 edge check; `music_notes_off` clears only BD.
* **n = 96 wraps into the key-on bit**; notes ≥ 80h fold as negatives (slides can reach both).
* **Note off clears noteword/note**, so an effect rest followed by slide/vibrato re-pitches the
  releasing voice near note 0 (effects 13, 18, 19).

### 4.9 PC speaker routines

```
spk_divisor CAAA[12] = 36485 34437 32505 30680 28958 27333 25799 24351 22984 21694 20477 19327
                       (1193182/divisor = C1 32.70 Hz … B1 61.7 Hz; n%12 = 0 is C)
spk_fstep   CA92[12] = -128 -120 -113 -108 -101 -96 -90 -85 -80 -76 -72 -63   (per 1/16 semitone)
default chmap (C79A+90h → C748): ch 0,1,2 → voice 0; others FFh
```

```c
/* 1ace:0b92 */
void spk_note_on(int v, u8 note, u8 frac, u8 vel, u8 prio) {
    if (v > 2) return;                              /* ch 9 (FFh) and others ignored */
    s8 n = note - 0x18; while (n < 0) n += 12;      /* driver note = MIDI - 24 */
    if (voice_alloc_check(0, n, prio)) return;      /* all channels contend for voice 0 */
    voice_owner[0] = cur_owner;
    voice_noteword[0] = n << 8 | frac;
    voice_note[0] = n;
    spk_set_freq(0, 0, n << 8 | frac);
    port61 |= 3;                                    /* gate + speaker data on */
}

/* 1ace:0bc9 */
void spk_set_freq(int v, int si, u16 nw) {
    s8 n = nw >> 8; u8 frac = (u8)nw;
    while (n > 0x60) n -= 12;  while (n < 0) n += 12;
    voice_noteword[si / 2] = (u8)n << 8 | frac;
    u8 oct = (u8)n / 12, semi = (u8)n % 12;
    u16 d = spk_divisor[semi] + (frac >> 4) * (u16)spk_fstep[semi];   /* 16-bit wrap */
    d >>= oct;                                      /* logical */
    pit2_divisor = d;                               /* out 42h lo, hi (cli) */
}

/* 1ace:0bde */
void spk_note_off(int v, u8 note) {
    if (v > 2) return;
    s8 n = note - 0x18; while (n < 0) n += 12;
    if (!force_off && (u8)n != voice_note[0]) return;
    voice_noteword[0] = 0; voice_note[0] = 0;
    port61 &= ~3;
}

/* 1ace:0c0e */ void spk_all_off(void) { port61 &= ~3; }
```

Behaviour that follows: music is reduced to one voice — within a tick the highest note of channels
0–2 wins, a later note replaces the sounding one, and a note off only stops the speaker if its note
is the sounding one. Velocity is ignored. Music voices get a small vibrato (§4.6). The engine note
written by `engine_sound` is in AdLib units (MIDI − 19) but the speaker reads note 0 as MIDI 24, so
the engine sounds **5 semitones higher** on the speaker than on AdLib.

---

## 5. Game-facing API

### 5.1 Init, shutdown, music selection (game_flow owns the callers)

```c
/* 01f4:1cf0 */
void far snd_init(void) {
    if (snd_on == 0) {
        sfx_init(snd_device_cfg); music_init(snd_device_cfg);
        snd_device_cfg = snd_device_id;             /* DS:0096 = 0/1/2/4/80h from now on */
        timer_install();                            /* platform: PIT ch 0 = 2000h, hook INT 8 */
    }
    snd_on = 1;
}

/* 01f4:1e9a */
void far snd_shutdown(void) {
    if (snd_on == 1) { sfx_play(0); music_stop(0); timer_restore(); }
    snd_on = 0;
}   /* no chip reset: drum bits other than BD may stay set in BDh */

/* 01f4:1e48 — title, attract, results */
void music_play_theme(void) {
    if (snd_on != 1 || music_off) return;
    music_stop(0);
    load "<drive>:THEME.MUS" (archive reader 0000:0ee0) into music_buf;     /* DS:0FBE */
    music_play(music_buf, 0, 0);
}

/* 01f4:1d22 — switch on main state DS:0086 (table 01f4:1e3a: 1d66 1d66 1d66 1d3c 1db4 1d66) */
int far music_for_state(int track) {
    switch (game_state) {
    case 1: case 2: case 3: case 6:                 /* 1d66 */
        if (snd_on != 1 || music_off) return;
        if (fopen("<drive>:DATAA.DAT", "rb") fails) return;   /* disk check */ fclose;
        music_play_theme(); return;
    case 4:                                         /* 1d3c: menus — plays whatever is in music_buf */
        if (snd_on == 1 && !music_off) music_play(music_buf, 0, 0);
        return;
    case 5:                                         /* 1db4: race ("radio") */
        if (!music_off && *(u16 *)&DS[0x8F] == 1 && snd_on == 1) goto play;   /* effects off */
        if (snd_on == 1 && !music_off && snd_device_cfg != 0) goto play;      /* not the speaker */
        return 0;
    play:
        music_stop(0);
        path = "<drive>:" scene_name + ('A' + track) + ".MUS";                 /* DS:0ADA, 0AE3, 0FB9 */
        load path into scene_music_buf;                                       /* 3630 bytes */
        if (!replay_active) music_play(scene_music_buf, 0, 0);
        return 1;
    default: return;                                /* ax undefined */
    }
}
```

| Song | Loaded into | Played |
|---|---|---|
| `THEME.MUS` (DATAA) | `music_buf` by `music_play_theme` | states 1, 2, 3, 6 (title/attract, protection, results) |
| `NEWWAVE.MUS` (DATAB) | `music_buf` by `shared_scene_load` 01f4:53c4 (only when DS:0084 = 0; also `music_stop(0)`) | state 4 (menus) plays the buffer as it is |
| `<scene>A/B/C.MUS` | `scene_music_buf` | state 5, track = `radio_station` DS:B6CC |

Because state 4 plays `music_buf` without reloading it, the menus play THEME instead of NEWWAVE if
THEME was loaded after the last `shared_scene_load` that actually loaded (e.g. attract → menus
without a race in between) — likely, depends on game_flow's state sequence.

Callers (all `music_stop(0)` / `music_for_state(B6CC)`):

| Where | What |
|---|---|
| `0000:0000` main (`01d9`, `065f`) | music_for_state per state |
| `0000:0f80` key 11h (Ctrl-Q) | toggle `music_off`; on → `music_for_state(B6CC)`, off → `music_stop(0)`; message 1/2 "Music on/off" |
| `0000:0f80` key 13h (Ctrl-S) | toggle `sfx_off`; off → `sfx_play(0)` and, on the speaker in a race, start the race song; on → on the speaker in a race with music on, `music_stop(0)`; message 3/4 "Sound on/off" |
| `0000:0f80` key 16h (Ctrl-V) | toggle `engine_off` (DS:C900), message 29h/2Ah "Engine sound on/off" |
| `0000:0f80` key 12h | message 11h (pause) then `music_for_state(B6CC)`: the song restarts from its beginning |
| `0000:179c(11h)` | "PAUSE - Press space to resume": `music_stop(0)`, `sfx_play(0)` |
| `0e12:0630` radio_next (keys M/m, codes 4Dh/6Dh) | `B6CC = (B6CC+1) % 3`; `music_for_state`; message 22h "Change radio station", or 28h "Music must be on to hear radio" (music off) / 27h "Sounds must be off to hear radio" |
| `0792:000c` race_run | `sfx_play(0)`, `music_stop(0)` at the start, `sfx_play(0)` at the end |
| `0792:04ca`, `0792:083c` | `music_for_state(B6CC)` (race start / after a reset — game_flow) |
| `0792:059a`, `0e12:0f31` | `music_stop(0)` (police chase end — game_flow) |

### 5.2 Effects: `sfx_play` (`0c1c:1110` far C wrapper, `0c1c:111d` AX = id)

```c
void far sfx_play_ax(u16 id) {
    if (snd_on != 1) return;
    if (id == 0) { /*cli*/ sfx_stop_all(); /*sti*/ memset(sfx_voice, 0xFF, 24); return; }
    u8 a = (u8)id;
    if (a != 2 && a != 6 && replay_active) return;
    if (sfx_off) return;
    u8 stop = a & 0x80; a &= 0x7F;
    if (a == 0x14 && leg_siren_alt) a = 0x17;
    if (stop) {
        u8 v = sfx_voice[a];                        /* no range check */
        if (v == 0xFF) return;
        sfx_voice[a] = 0xFF;
        if (voice_sfx_id[v] == a) { /*cli*/ voice_release(v); /*sti*/ }
        return;
    }
    if (a > 0x17) return;
    u8 prio = sfx_priority[a];
    u8 v = sfx_voice[a];
    if (v != 0xFF && voice_sfx_id[v] == a) return;  /* already playing */
    /*cli*/ if (prio == 0x7F) sfx_stop_all();
    v = (u8)sfx_start(sfx_script[a], prio); /*sti*/
    sfx_voice[a] = v;
    if (v != 0xFF) { voice_sfx_id[v] = a; if (a == 0x0F || a == 0x10) sfx_15_16_started = 1; }
}
```

Effect ids (priority DS:915E, script DS:918E; meaning from the call sites, **likely** unless noted):

| id | prio | script | caller | use |
|---|---|---|---|---|
| 1 | 60h | 93DE | `0e12:23df` engine_sound (per frame) | **engine** (verified) |
| 2 | 7Fh | 926C | `0792:1c00` | wrecked-car sequence (BROKE.LZ), plays during replay too |
| 3 | 78h | 920C | `0e12:33f7` sprite_collisions | crash into a roadside object (sets DS:947C = 5 shake, may call `0e12:0edb` alignment damage) |
| 4 | 78h | 922C | `0e12:4590` | collision (DS:947C = 5, DS:BAA7 = 3) |
| 5 | 78h | 924C | – | unused |
| 6 | 7Fh | 9388 | `0792:19ca` | drove into water (WATER.LZ), plays during replay too |
| 7 | 70h | 932C | `0e12:5e70` crossing_gate_update | railroad crossing bell (ding-ding) |
| 8 | 7Ch | 93B4 | `0e12:5e70` | train horn |
| 9 | 7Ch | 92E2 | `0e12:4fc7` | object type 1Fh spawns a vehicle (guess) |
| 10 | 7Ch | 924C | `0e12:0e74` | engine damage (sets DS:9486 bit 5; skill ≥ 3) |
| 11 | 7Ch | 922C | `0e12:01d8` gear_down, `0e12:0e74`, `0e12:0edb` | gear grind / transmission damage (DS:9486 bits 8+) |
| 12 | 7Ch | 924C | `0e12:0edb` | suspension/alignment damage |
| 13 | 78h | 91BE | `0e12:33f7` | hit a sprite of class ≥ E0h (removed) |
| 14 | 74h | 9318 | `0e12:0db4` | blip every 8 frames while DS:B710 ≥ 10h: radar detector |
| 15 | 78h | 9354 | `0977:0008` | tyre squeal on pavement; stopped with 8Fh |
| 16 | 78h | 936E | `0977:0008` | skid on surfaces 0Eh/0Fh/FFh; stopped with 90h |
| 17 | 78h | 92C8 | `0977:0008` | rumble on surfaces 11h/12h (off-road) |
| 18 | 78h | 91BE | `0e12:4631` | object hit (DS:BAA7 = 5, alignment damage) |
| 19 | 72h | 941E | `0977:0008` | thud (suspension bottoming / landing) |
| 20 | 7Ah | 92A8 | `0e12:5ffe` (94h to stop) | police siren (US wail, ±2.1 semitones every 276 ticks) |
| 21 | 60h | 93DE | – | unused (engine script) |
| 22 | 68h | 924C | – | unused |
| 23 | 7Ah | 93FA | id 20 when DS:9596 ≠ 0 | two-tone siren 88/85 (European?); unused by the shipped scenes |

Most one-shot scripts end with `goto_engine`: when the engine is not playing (and not switched
off) the voice becomes the engine sound, which is how the engine comes back after being displaced.

### 5.3 Engine note hook (`0e12:23df`, from `frame_update`)

```c
void engine_sound(void) {
    if (replay_active) return;
    if (engine_off) {
        u8 v = sfx_voice[1];
        if ((s8)v >= 0 && voice_sfx_id[v] == 1) { /*cli*/ voice_release(v); /*sti*/ }
        return;
    }
    sfx_play_ax(1);                                 /* no-op while it plays */
    u16 x = engine_rpm;                             /* DS:129C */
    if (x >= 1500) { x = 1500; engine_rpm = 1500; } /* 5DCh, unsigned; written back (side effect) */
    u16 cx = x * 4 + 0x1300;
    if (!(damage_flags & 0x20)) cx += 0x400;        /* healthy engine: +4 semitones */
    u8 v = sfx_voice[1];
    if ((s8)v < 0 || voice_sfx_id[v] != 1) return;
    DS[0x93E6] = cx >> 8;                           /* script note byte: used by the next "note" op only */
    /*cli*/ voice_noteword[v] = cx & 0xFF80; /*sti*/  /* (the original also builds (old&7Fh)|cx and drops it) */
}
```

Pitch: driver note `19 + x/64`, in half-semitone steps (bit 7 of the low byte), + 4 when bit 5 of
DS:9486 is clear. AdLib: MIDI 38–61 (damaged) / 42–65 (healthy), i.e. 73–349 Hz (x = 1500 gives
low byte 70h, masked to 0); PC speaker 5 semitones higher. `sfx_modulate` sends it on the next tick with the engine vibrato (±31/256 semitone
steps, period 4). The script note byte (DS:93E6) holds a driver note but op 6 reads it as a MIDI
note; it only matters for the first note-on of the engine script (19 lower than intended), which
the next frame's noteword write overrides.

---

## 6. Hardware / DOS dependencies and SDL3 replacement

| Original | Where | Port |
|---|---|---|
| OPL2 at 388h/389h (index, 6 status reads; data, 35 status reads) | `1bd8:000e` | `opl_write(reg, val)` into the emulator, immediately, no delays |
| OPL timer detection (04h/02h, status bits E0h) | `1bd8:0028` | always succeeds; keep the register writes (F5h..01h = 0, 04h = 60h, BDh = 20h, 01h = 20h) |
| PIT ch 2 (43h = B6h once, 42h lo/hi) + port 61h bits 0–1 | `01cc`, `0bc9`, `0b92`, `0bde`, `0c0e` | speaker model: `pit2_divisor`, `port61` bits → square-wave synth (§9) |
| PIT ch 0 145.652 Hz, INT 8 hook, chaining | `0c1c:109f/10cf/10e8` (platform) | host tick (platform spec) calling `snd_tick` |
| INT 21h 3Dh/3Fh/42h/3Eh open/read/seek/close `INSTR.DAT` | `02fa` | host file API, read section 0 of `Game/INSTR.DAT` |
| INT 21h 48h/49h allocate/free the instrument block | `02fa`, `1bd7:000c` | static array / `malloc` |
| `cli`/`sti` around driver calls | game side, `05bf` | the host lock / single-thread tick (§9) |
| MPU-401 330h/331h, SAA1099 220h+, SN76489 C0h | parked | – |

**Platform note (not sound code):** `timer_install` (`0c1c:109f`) does `push cs; pop ds` for the
INT 21h 25h call and then executes `mov word [C5E4h], 2000h` with DS still = CS. The store lands at
`0c1c:C5E4` = image 187A4h = `185f:01b4`, overwriting the instruction `8B C1` (`mov ax,cx`) of the
graphics-library blitter `185f:000b` with `00 20` (`add [bx+si],ah`) at runtime. The platform/render
specs should check whether that path of `185f:000b` is reachable in mode 14h. DS:C5E4 itself is never
read.

---

## 7. Timing

* **Tick**: PIT ch 0 divisor 2000h → 1 193 181.67 / 8192 = **145.652 Hz** (6.866 ms). `snd_tick`
  runs once per tick, before the ISR increments DS:00A0. Every 8th tick the ISR chains to the BIOS
  handler (18.2 Hz). All music timing (delta units), effect timing (waits, loops), vibrato and
  slides are in these ticks; nothing depends on the frame rate.
* **Order inside a tick**: clear busy → effect modulation (pitch updates of the previous state) →
  effect script ops → music fade → music countdown/events. A note started by an effect op or a music
  event is modulated from the next tick on.
* **Per frame** (game, ≥ 5 ticks): `engine_sound` (pitch write), `sfx_play` calls from the
  simulation, key handlers. The engine pitch therefore changes at most ~29 times a second while its
  vibrato runs at 145 Hz.
* Music: first events on the tick after `music_play`; delta 0 = 1 tick; songs never end (§4.3).
* Effects: first opcode on the 2nd tick after `sfx_start`; first note on the 5th tick (§4.6).

---

## 8. Differences from Test Drive (1987) / Test Drive II

Nothing is shared: TD1/TD2 use DSI's PC-speaker stream players (TD2 `06c9:6269`/`75ea`), TD3 has
Accolade's own multi-device MIDI-style driver. Reusable from `../TestDrive2/td2port`: the host audio
plumbing — `host.c` opens an `SDL_AudioStream` (S16 mono), and `audio_for_one_tick()` pushes the
samples of one timer tick after each emulated tick with a phase-continuous square wave for the
speaker, a 50 ms lead-in and a 250 ms queue cap. TD3 extends that with the OPL emulator and a
different tick divisor (2000h instead of 2E9Ch).

---

## 9. Port design

### 9.1 Structure

* `sound/driver.c`: the core + AdLib + PC speaker exactly as §4, one state struct. The DS layout
  need not be reproduced: the overlaps CA05/CA15 and CB3B/CB3C have no audible effect, and the loop
  arrays can be `[3]` with an assert on the voice (only voice 0 carries loop events).
* Device routines as a function-pointer table selected by `snd_select_routines`; Tandy/CMS/MPU
  entries omitted. Config mapping: TD3.CFG 4 → AdLib; 0 → speaker; 1/2/3 (parked) → the port picks
  AdLib (`/* PORT: */`; the original would run the Tandy routines, or fall back to the speaker when
  the CMS/MPU probe fails, with effects forced off for MT-32).
* Tables (FNUM, steps, slots, perc maps, speaker divisors, effect priorities, script pointers, the
  script block 91BE–944B, the defaults C79A) exported from the EXE by a tool into generated C, like
  the other DGROUP tables; `INSTR.DAT` read at runtime.

### 9.2 OPL2 emulator and scheduling

* Emulator: an OPL2 (YM3812) core in C — e.g. Nuked-OPL3 in OPL2 mode (LGPL-2.1, register-exact)
  or ymfm's YM3812 (BSD-3, C++). Decision for the lead; waveform select must be honoured (01h = 20h,
  all E0h registers stay 0 anyway). Rhythm mode (BDh = 20h) must be supported.
* Sample rate: run the emulator at its native 49 716 Hz (3 579 545 / 72) and let the
  `SDL_AudioStream` resample (open the stream with `freq = 49716`); or generate at 48 kHz with the
  emulator's resampler.
* Same model as td2port: the host timer runs the platform ISR emulation once per due tick; the ISR
  calls `snd_tick()`, whose `adlib_write`s go **directly** into the emulator; then the host generates
  exactly one tick of samples (`rate × 8192 / 1 193 182` = 341.33 at 49 716 Hz, fractional
  accumulator) and queues them. Register writes thus take effect on tick boundaries, as on the real
  card (6.866 ms quantisation). Everything is on the main thread, so the game's `cli/sti` sections
  need no lock.
* Always clock the emulator for every tick (envelopes must advance), even when the queue is full; cap
  latency by dropping the *queued output*, not by skipping emulation. Lead-in ~50 ms.
* If the port ever moves sound into the SDL audio callback (driver ticks counted in samples), all
  game-side entry points (`music_play/stop`, `sfx_play_ax`, the `engine_sound` noteword write,
  `voice_release`) must take `SDL_LockAudioStream` — the equivalent of `cli`.
* `SDL_AUDIO_DRIVER=dummy` for headless runs (repo rule); a debug env var (e.g. `TD3_OPL_LOG=file`)
  should dump `(tick, reg, val)` for regression.

### 9.3 PC speaker synthesis

* State: `pit2_divisor` (u16, 0 = 65536), `port61` bits 0 (gate) and 1 (data enable). Sound only when
  both are set; frequency = 1 193 182 / divisor (32.7 Hz … ~8.4 kHz for note 96).
* Square wave, 50 % duty (PIT mode 3), fixed amplitude (velocity is ignored), phase-continuous across
  divisor changes; a new divisor applies from the next sample (the PIT reloads at the next half
  cycle — optional refinement). Reuse td2port's generator; add PolyBLEP or 2× oversampling + a gentle
  low-pass to tame aliasing at high divisors, plus a DC blocker for gate on/off clicks.
* Divisor/gate change only inside `snd_tick` (and the `cli` API calls), so per-tick generation is
  exact.

---

## 10. Regression references (`work/sound/`)

`python tools/td3snd.py all` renders `<song>.dro` (8 songs) and `sfx_NN.dro` (23 effects) from its
Python model of this driver (`AdlibDriver`), which the port must match:

* DRO v2.0, OPL2, delays in ms: recover the tick of each write as `round(ms / 6.8657)` (ticks are
  > 1 ms apart, so this is exact) and compare the **register file after each tick** with the port's
  `(tick, reg, val)` log. Compare from `music_play`/`sfx_start` on: td3snd's init writes only
  BDh = 20h, 01h = 20h, the 11 instruments and one silent snare (the game: full reset and all of it
  twice).
* Songs: fresh driver, `music_play(song)` right after init, one pass of the endless cycle, then
  `music_notes_off`. Effects: fresh driver, `sfx_start(script, prio)` with no music; id 1 with a
  synthetic RPM ramp (0 → 1500 over 3 s, one write every 5 ticks, +400h).
* Known td3snd deviations from the driver (checked by re-running the model with the fix):
  1. td3snd's melodic note off does not zero `voice_noteword/voice_note` (§4.8). Music renders are
     unaffected (register stream identical for all 8 songs); `sfx_07` has one redundant B0h write
     more; **`sfx_13`, `sfx_18`, `sfx_19` differ after their rests** (the real driver re-pitches the
     releasing voice near note 0). The port follows the driver; treat those three as approximate.
  2. td3snd creates a fresh loop state per song; the driver keeps it (§4.3).
  3. Unreachable differences: `music_claim_voices` releasing the owner id, `adlib_program` for voices
     11–13, op 11 `& 7Fh`, the BD noteword store offset.

---

## 11. Open questions and resolutions

Resolved (from `port/formats/sound.md`'s list):

* **DS:B6CC** = race song ("radio station") 0..2 → `<scene>A/B/C.MUS`: 0 at startup and on entering
  scene select, cycled by key M (`0e12:0630`, message "Change radio station").
* **DS:9486 bit 5**: set with effect 10 by `0e12:0e74` (skill ≥ 3 mechanical damage) and read by the
  simulation's power code: engine damage (likely). Clear → engine sound +4 semitones.
* **DS:9596**: byte 4 of the leg map (`<scene>A..E.DAT` → DS:9592); selects the two-tone siren (id
  23) instead of id 20. 0 in all shipped legs.
* **Uncalled routines** `1ace:0185` (music_resume — resumes, does not restart), `01b6` music_enable,
  `01bc` music_disable, `03ba` music_reset: no far-call references anywhere in the image; dead API.
  `1ace:0eab` (and 0ebe–0fcb) are reached through the opcode table DS:CB3C (the index should add it
  to `PTR_TABLES`).
* **Init's uninitialised register**: the silent snare in `snd_select_routines` uses CL (and DH) of
  the caller chain: music_init/sfx_init/adlib_detect_reset/load_bank all preserve CX, so it is the CX
  that `main` has after `0c1c:0e2d` (keyboard install, CX untouched) ← `01f4:1c9c` (returns early
  for VGA) ← `config_load 0000:092a` (RTL file calls, graphics mode set): not statically known. Its
  only effect is the fraction of channel 7's frequency and the key-on bit of B7h (`CL & 20h`) until
  the first snare note rewrites B7h. Port: 0. A DOSBox OPL capture of the game start would settle it.
* Effect ids: see §5.2 (MT-32 program names in `sfx.txt` are irrelevant on AdLib).
* Engine pitch range corrected: driver note 19–42 (+4 = 23–46), half-semitone resolution.

Still open:

* Listen/compare against a DOSBox OPL capture (`.dro` from DOSBox's `captureopl`): tempo, the C0h
  quirk, the effects' timbres.
* The menu music after attract → menus (THEME vs NEWWAVE, §5.1) depends on game_flow's state
  transitions.
* Effect meanings marked likely/guess in §5.2 (ids 2–4, 9, 12–14, 17–19) should be confirmed by the
  simulation spec.
* Key code 12h: which key produces it (sound.md says Ctrl-R) — platform key map.
* The `timer_install` DS = CS store into `185f:01b4` (§6) — platform/render.
