# Sound: `.MUS` music, `INSTR.DAT`, sound effects and the 1ace driver

Addresses as in `port/RE_GUIDE.md` (`SSSS:OOOO` file segments, `DS:xxxx` = DGROUP 1BE4).
Status tags: **Verified** (read in the code and checked against every data file / re-implemented
in `tools/td3snd.py`), **Likely**, **Unknown**. Nothing here has been checked by ear yet: the
MIDI and DRO renders in `work/sound/` are there for that.

Tool: `python tools/td3snd.py all` writes to `work/sound/`:

| Output | What |
|---|---|
| `<song>.txt` | event listing in file order (offset, tick, seconds, raw bytes, decoded event, delta) |
| `<song>.mid` | standard MIDI file: all channels, loops unrolled as the driver plays them (intro + one pass of the endless cycle, `loopStart`/`loopEnd` markers), MT-32 programs mapped to GM (approximate; `--raw-programs` keeps them) |
| `<song>.dro` | DOSBox raw OPL v2.0: the OPL2 register stream the game's AdLib driver produces, from a Python model of that driver (AdPlug, foobar2000 + adplug, DOSBox-X can play it) |
| `instr.txt` | the three `INSTR.DAT` banks, AdLib instruments decoded per operator |
| `sfx.txt`, `sfx_NN.dro` | the 23 sound-effect scripts from the executable, disassembled, and rendered through the AdLib model (4 s each; id 1 = engine with an RPM ramp) |

## Overview

The driver is one assembly module, segments `1ace` (4.3 KB) + `1bd7` (DOS free) + `1bd8` (AdLib
port I/O and detection). It is a small MIDI-like sequencer with one routine table per device, plus
a script-driven sound-effect engine sharing the same voices. Everything runs from the timer IRQ.

```
01f4:1cf0 snd_init (once)            0c1c:10e8 timer ISR, 145.65 Hz
  1ace:0ffe sfx_init(dev)              1ace:03d6 snd_tick
  1ace:01cc music_init(dev)              clear voice_busy[]
    1ace:027d reset tables               [C912] 1ace:0d1c sfx_modulate (slide/vibrato -> freq)
    1bd8:0028 adlib_detect_reset         [C910] 1ace:0e55 sfx_step (one script op per slot)
    1ace:09ed adlib_load_bank            music: fade (048d), countdown, events -> DS:C975 handlers
      1ace:02fa instr_load(section)          -> [C906] note_on / [C908] note_off / [C90A] program
      1ace:092a adlib_load_instrument  every 8th tick: chain to the BIOS INT 8
    1ace:02a5 snd_select_routines
  0c1c:109f timer_install (PIT 2000h)
music: 1ace:004b music_play(far p, fade, vol)   1ace:00b4 music_stop(mode)
sfx:   0c1c:1110 / 0c1c:111d sfx_play(id)  -> 1ace:0db9 sfx_start, 1ace:00ec voice_release,
       1ace:1072 sfx_stop_all;  0e12:23df engine_sound (per frame)
```

## Timing (Verified)

* `timer_install` (`0c1c:109f`) programs PIT channel 0 with divisor 2000h: 1 193 182 / 8192 =
  **145.652 Hz** (6.866 ms). The ISR `0c1c:10e8` calls `snd_tick` (`1ace:03d6`) on **every**
  interrupt, then increments `DS:00A0` (game tick) and chains to the old INT 8 every 8th tick.
* One music tick = one timer tick. There is no tempo event and no tempo field: tempo is baked into
  the delta times. Typical songs step in multiples of 3..6 ticks (e.g. THEME: hi-hat every 29 ticks
  ≈ 0.2 s ≈ 8th notes at 150 BPM). One pass of a song lasts 25–114 s.
* The port must drive the sound model at 145.652 Hz independently of the frame rate (the game
  frame is ≥ 5 ticks). Order inside a tick: clear `voice_busy`, sfx modulation, sfx script step,
  music fade, music events.

## `.MUS` files (Verified)

Files: `THEME.MUS` (DATAA, title/attract/results), `NEWWAVE.MUS` (DATAB, menus),
`<scene>A/B/C.MUS` (scene archive, race; one of three chosen by `DS:B6CC`, Likely the music
selection). All 8 files in the two shipped scenes parse cleanly to the end marker.

| Offset | Size | Field |
|---|---|---|
| 0 | u16 | length of the event stream including the final `FC` (= file size − 3). Not read by the driver (`music_play` starts at +2) |
| 2 | … | event stream |
| size−1 | u8 | `FF` pad byte after `FC`, never read |

### Events

Each event: status byte, 0–2 data bytes (length from the table `DS:C985`), then — only if status
bit 7 is set — a delta time. Status bits 4–6 select the handler (`DS:C975`), bits 0–3 the channel.

| Status | Len | Handler | Meaning |
|---|---|---|---|
| `0n`/`8n` | 2 | `1ace:04de` | note off: `note`. Ignored unless the note is the one sounding on the voice (AdLib) |
| `1n`/`9n` | 3 | `1ace:04e8` | note on: `note`, `velocity` (0 = note off) |
| `2n`/`An` | 2 | `1ace:0536` | loop: `count`; 0 = loop start, else loop end (see below) |
| `3n`/`Bn` | 1 | `1ace:0591` | stop here if a "stop at loop point" was requested (`music_stop(80h)`) — unused in the data |
| `4n`/`Cn` | 2 | `1ace:05a6` | program change: MT-32 program number 0–127 |
| `5n`/`Dn` | 1 | `1ace:05b4` | jump to the exit of the last loop end (volta) — unused in the data |
| `6n`/`En` | 1 | `1ace:05bf` | callback through `DS:C914` (= `ret`) — unused |
| `7n`/`Fn` | 1 | `1ace:05ca` | `FC` = end of song: all music notes off, stop. Other `Fx`: nothing |

The high bit of the status is *not* "status vs running status": it means "a delta follows". Events
without it happen in the same tick as the next event. The data uses types 0, 1, 2, 4 and `FC` only.

Delta (`1ace:0468`): one byte `d` if `d < 80h`, else two bytes `lo, hi` → `(lo & 7Fh) | hi << 7`
(15 bits). The countdown `DS:C83B` is decremented at the start of each tick and events run when it
reaches ≤ 0, so **delta 0 and delta 1 both mean one tick**. `music_play` sets it to 1: the first
events run on the first tick.

Channels: 0–9 in the data; 9 = percussion (MT-32/GM key numbers 35–81). Channels are mapped to
driver voices by the bank's channel map (`INSTR.DAT`); on AdLib channels 6, 7, 8 and 10–15 are
silent (THEME ch 7 and SCENE02A ch 7 are MT-32-only parts).

### Loops (Verified, `1ace:0536`)

Per-voice state (index = voice of the event's channel, 3 entries, the data only uses channel 0):
`loop_start` `DS:C85F`, `loop_status` `DS:C865`, `loop_count` `DS:C853`, `loop_exit` `DS:C859`.

```
loop(count):
  if stop_requested (C5E2 & 80h): stop music
  if count == 0: loop_start = pos after event; loop_status = status; return
  c = loop_count
  if c == 0:
      if loop_start == 0: return
      loop_count = count; loop_exit = pos after event; jump
  elif c != 1: jump
  loop_count -= 1          (also after a jump)
jump: pos = loop_start; status = loop_status  (so no delta is read after the jump)
```

`count` is the total number of passes. Every file ends with `20 00 … A0 02 <d> 20 02 FC FF`:
the body plays twice, then the second loop end (`20 02`) finds the counter at 0 and starts a new
`2` loop to the same start, whose first end is taken by `A0 02` … so the song **loops forever** and
`FC` is never reached (Verified by simulation: the state repeats; `td3snd` reports the cycle).
The game stops music explicitly (`music_stop(0)`).

| File | Bytes | One pass | Cycle starts / length | Notes | Channels | Programs (ch:prog) |
|---|---|---|---|---|---|---|
| THEME | 3743 | 25.7 s | 51.3 s / 25.7 s | 33–86 | 0 1 2 7 9 | 0:82 1:28 2:47 7:34 9:115 |
| NEWWAVE | 5534 | 49.1 s | 98.3 s / 49.1 s | 26–78 | 0 1 9 | 0:70 1:67 9:115 |
| SCENE01A | 3626 | 32.7 s | 65.5 / 32.7 | 36–88 | 0 1 2 9 | 0:0 1:65 2:65 |
| SCENE01B | 2465 | 33.5 s | 67.1 / 33.5 | 43–84 | 0–3 | 0:58 1–3:51 |
| SCENE01C | 3212 | 41.4 s | 82.9 / 41.4 | 36–85 | 0 1 3 9 | 0:52 1:65 3:62 |
| SCENE02A | 2785 | 28.4 s | 56.9 / 28.4 | 31–81 | 0 1 3 4 5 7 9 | 0:78 1:67 3,4:88 5:89 7:91 |
| SCENE02B | 3495 | 113.6 s | 140.6 / 69.5 | 36–84 | 0 1 2 | 0:50 1:49 2:50 |
| SCENE02C | 3454 | 41.5 s | 83.1 / 41.5 | 24–83 | 0 1 3 9 | 0:47 1:67 3:67 |

Note-on and note-off counts are equal in every file; all notes lie in 24–88.

### Loading and playing (Verified)

| Caller | What |
|---|---|
| `01f4:1e48` | (states 1, 2, 3, 6 via `01f4:1d22`) `music_stop(0)`, load `THEME.MUS` into the far buffer `DS:E7F0`, `music_play(E7F0, 0, 0)`. `1d22` first checks that `DATAA.DAT` can be opened (disk check) |
| `01f4:53c4` | (front-end data, once, `DS:0084`) `music_stop(0)`, load `NEWWAVE.MUS` into the same buffer `DS:E7F0` |
| `01f4:1d3c` | (state 4, menus) `music_play(E7F0, 0, 0)` if sound is on (`DS:009C`) and music not muted (`DS:008E`) |
| `01f4:1db4` | (state 5, race, arg = `DS:B6CC`) builds `A:<scene>` + `'A'+arg` + `.MUS` from `DS:0ADA`, loads it into `DS:E77A` and plays it unless replaying (`DS:948B`). Only if music is on and (device ≠ PC speaker or effects are off `DS:008F`) |
| `0000:0f80` key handlers | Ctrl-Q `DS:008E` music on/off, Ctrl-S `DS:008F` effects on/off, Ctrl-V `DS:C900` engine sound on/off, Ctrl-R restart music |

Buffers: `DS:E7F0` = offset 9BDCh of the 65000-byte block `DS:E770`, 6000 bytes before the next
user (`DS:E866`); `DS:E77A` = `malloc(0E2Eh)` = **3630 bytes** for scene music (SCENE01A is 3626).
The loader is the archive reader `0000:0ee0` (raw bytes, no compression).

`music_play(far p, fade, vol)` (`1ace:004b`): all music notes off (`0006`), `pos = p + 2`,
`C5E2 = 10h`, `master_vol C5DE = vol` (or a fade-in setup when `fade ≠ 0`), countdown = 1,
reclaim voices (`0151`: every voice not held by an effect of priority > 40h becomes owner 1,
priority 40h), set "playing". The game always passes `fade = vol = 0`.
`music_stop(mode)` (`1ace:00b4`): 0 = stop now (`0006`); bit 7 = stop at the next loop event;
else a fade-out speed. The game only uses 0. The fade code (`048d`) has a bug that makes the fade-in
branch unreachable (it tests bit 5 after masking to the low nibble) — irrelevant, unused.

## `INSTR.DAT` (Verified)

Chain of device sections; `instr_load(n)` (`1ace:02fa`) skips `n` sections.

| Offset | Size | Field |
|---|---|---|
| 0 | u16 | size of the rest of the section (skip to the next) |
| 2 | ASCIIZ | device name (read byte by byte and ignored) |
| +0 | 0x162 | table block, read to `DS:C638` |
| +0x162 | count×size | instrument data, read into a DOS block (`INT 21h/48h`), far pointer `DS:C96A` |

| Section | Offset | Name | Instruments | Used for |
|---|---|---|---|---|
| 0 | 0x000 | `Ad-lib Sound Board` | 82 × 9 | AdLib / Sound Blaster (device 4) |
| 1 | 0x459 | `Roland MT-32 or LAPC-I` | 0 | MT-32 (TD3.CFG 3 → driver device 81h) |
| 2 | 0x5D4 | `Casio CT-460` | 0 | device 82h — not selectable by SETUP |

Table block (`DS:C638`, 0x162 bytes):

| Offset | DS | Size | Field |
|---|---|---|---|
| 0x000 | C638 | u8[128] | program map: MT-32 program → AdLib instrument index (MT-32/Casio: program number sent) |
| 0x080 | C6B8 | s8[128] | velocity offset per program, added to every note-on velocity (clamped 0..127) |
| 0x100 | C738 | u8[16] | not referenced (zeros) |
| 0x110 | C748 | u8[16] | channel map: MIDI channel → voice, > 15 = not played. AdLib: 0–5 → voices 0–5, 9 → 9 (rhythm). MT-32: ch n → n+1 (MT-32 default parts), 9 → 9 |
| 0x120 | C758 | u8[64] | percussion map for notes 36–99: AdLib = pitch (driver note) of the rhythm voice; MT-32 = note sent |
| 0x160 | C798 | u8 | instrument count |
| 0x161 | C799 | u8 | instrument size (9) |

AdLib instrument (9 bytes, `1ace:092a`):

| Byte | Register | |
|---|---|---|
| 0–3 | 20h, 40h, 60h, 80h + modulator slot | AM/VIB/EG/KSR/MULT, KSL/TL, AR/DR, SL/RR |
| 4–7 | same for the carrier slot | |
| 8 | C0h | feedback/connection; bit 0 also kept as `DS:CA15[voice]` |

No E0h (waveform) byte: `adlib_detect_reset` enables waveform select (reg 01h = 20h) but every
waveform register stays 0 (sine). Instruments 1–5 are the rhythm instruments (BD, SD, TT, CY, HH).

## Driver data model (Verified)

Voices are indexed 0–15 (AdLib uses 0–5 melodic, 6–10 rhythm, 9 also = "percussion channel").

| DS | Type | Name | Meaning |
|---|---|---|---|
| C5DE | s8 | master_vol | added to every music velocity (fade) |
| C5E0 | u8 | snd_ready | bit 0 music driver ready, bit 2 effects ready |
| C5E1 | u8 | snd_active | bit 0 music playing, bit 2 an effect is active |
| C5E2 | u8 | music_ctl | bit 4 song loaded, 5 fade in, 6 fade out, 7 stop at loop point, bits 0–3 fade speed |
| C5E3 | u8 | snd_device | device id: 0 speaker, 1 Tandy, 2 CMS, 4 AdLib, 80h MPU-401 (copied back to `DS:0096`) |
| C5E6 | u8 | adlib_bd | shadow of OPL register BDh (20h = rhythm mode) |
| C638…C799 | | instr_tables | `INSTR.DAT` table block (above) |
| C83B | s16 | music_wait | tick countdown |
| C83D | far ptr | music_pos | next event |
| C841 | u8 | music_status | status of the current event (bit 7 = read a delta) |
| C842 | u8 | music_halt | set by the end/stop handlers: leave the event loop |
| C843 | u8[16] | voice_program | current program per voice (index into C6B8) |
| C853/C859/C85F/C865 | u16[3] each | loop_count/exit/start/status | loop state per voice |
| C86B, C86C | u8 | fade_count, fade_target | |
| C86D | u16[16] | voice_noteword | driver note << 8 \| fraction (fraction ÷ 16 = 1/16 semitone) |
| C88D | u8[16] | voice_velocity | velocity of the last effect note |
| C8AD | u8[16] | voice_owner | 0 free, 1 music, 2–4 effect slot + 1 |
| C8BD | u8[16] | voice_note | driver note (AdLib: MIDI − 19) |
| C8CD | u8[16] | voice_priority | music 40h, effects 60h–7Fh |
| C8DD | u8[16] | voice_busy | a note was started on the voice this tick |
| C8ED | u8[16] | adlib_b0 | shadow of B0h+v (key-on bit 20h) |
| C8FD | u16 | cur_voice | voice of the current event |
| C8FF | u8 | cur_owner | owner id of the caller (1 music, slot+1 effects) |
| C900 | u8 | engine_off | Ctrl-V: no engine sound |
| C901 | u8[5] | device_ids | device index → C5E3 (0, 1, 2, 4, 80h) |
| C906–C90E | near ptr[5] | dev_note_on, dev_note_off, dev_program, dev_all_off, dev_set_freq | current device routines |
| C910, C912, C914 | near ptr | sfx_step, sfx_modulate, event hook (`ret`) | |
| C916, C918 | near ptr | freq_table, freq_step_table | per device (AdLib: C9AA, C992) |
| C91A–C956 | near ptr[5] × 7 | dev tables | per device index 0–4, see below |
| C960 | char[] | "INSTR.DAT" | |
| C96A | far ptr | instr_data | |
| C975 | near ptr[8] | music_handlers | event types 0–7 |
| C985 | u8[8] | event_len | 2 3 2 1 2 1 1 1 |
| C98D | u8 | force_off | note-off without the "same note" check |
| C992, C9AA | u16[12] | adlib_fstep, adlib_fnum | see below |
| C9BC+6 | u8[5] | adlib_rhythm_bit | BD register bit per rhythm voice 6–10: 10h 08h 04h 02h 01h |
| C9C7 | u8[18] | adlib_op_offset | slot → operator offset 0,1,2,3,4,5,8,…,21 |
| C9D9 | u8[22] | adlib_voice_slots | voice → (modulator slot, carrier slot), FFh = single operator |
| C9EF | u8[18] | adlib_is_carrier | |
| CA01 | u8[4] | adlib_op_regs | 20h 40h 60h 80h |
| CA05 | u8[18] | adlib_inst_tl | instrument 40h byte per slot |
| CA15 | u8[16] | adlib_inst_conn | instrument C0 bit 0 per voice |
| CA25 | u8[11] | adlib_voice_inst | default instrument per voice: 0 ×6, then 1–5 for voices 6–10 |
| CA30 | u8[40] | adlib_perc_voice | percussion note 36–75 → rhythm voice 6–10, 15 = none |
| CAF5 | u8[16] | voice_sfx_id | game effect id playing on the voice |
| CB05…CB3B | per slot (3) | sfx slot state | step, –, vib counter, vib period, slide, wait, script ptr, loop start, loop count, voice |
| CB3C | near ptr[13] | sfx_ops | effect opcodes 0–12 |

### Devices (Verified)

`TD3.CFG` byte 2 (`DS:0096`; the in-game setup `0000:092a` maps keys 1–5 → 0–4): 0 PC speaker, 1 Tandy, 2 Game
Blaster, 3 MT-32 (the game turns it into 81h and turns effects off, `DS:008F = 1`), 4 AdLib/SB.
`music_init`/`sfx_init` probe the hardware and fall back to the PC speaker (index 0) if it fails.
`snd_select_routines` (`1ace:02a5`, BX = index) copies one column of these tables:

| Index / id | note on C92E | note off C938 | program C942 | all off C94C | set freq C956 | freq tables C91A/C924 |
|---|---|---|---|---|---|---|
| 0 / 0 PC speaker (PIT ch 2, port 61h) | 0b92 | 0bde | ret | 0c0e | 0bc9 | CAAA (PIT divisors) / CA92 |
| 1 / 1 Tandy (SN76489 port C0h) | 0c15 | 0ca0 | ret | 0cd1 | 0c66 | CADA / CAC2 |
| 2 / 2 Game Blaster (SAA1099 220h–223h) | 0a59 | 0b16 | ret | 0b5f | 0abf | CA70 / CA58 |
| **3 / 4 AdLib (388h/389h)** | **07df** | **08c1** | **09a1** | **09c2** | **0884** | **C9AA / C992** |
| 4 / 80h MPU-401 (330h/331h, "D0h" send-data command per message) | 064f | 0703 | 0637 | 0787 | 06a3 | (AdLib's) |

Only the AdLib column is in scope for the port (PLAN.md); the others are listed for completeness.
PC speaker: one voice for everything (voices 0–2 → the speaker), note − 24, divisor ≫ octave; the
PC-speaker init also sets a tiny vibrato on effect slot 0 (`CB05 = 1`, `CB17 = 12`), which the music
voices use.

## AdLib mapping (Verified from code; to be confirmed by ear with the DROs)

OPL write `1bd8:000e`: AL = register, AH = value; index to 388h, 6 status reads, data to 389h,
35 status reads. `adlib_detect_reset` (`1bd8:0028`): timer detection (04h=60h, 80h, 02h=FFh,
04h=21h, check status bits), then registers F5h…01h = 0, 04h = 60h, **BDh = 20h (rhythm mode)**,
01h = 20h. Init then loads instrument `CA25[v]` into voices 10…0 and strikes a silent snare
(note 38, velocity 0, then note off).

**Voices**: MIDI channels → voices by the bank map: ch 0–5 → OPL channels 0–5, ch 9 → rhythm.
Only 6 melodic voices. **Effects take voices 5 and 4** (see below), so music on channels 4–5
(SCENE02A) is cut while an effect plays.

**Note on** (`04e8` → `07df`): `vel' = clamp(vel + C6B8[program], 0, 127) + master_vol`, clamped
to 1..127. Melodic: `n = note − 19` (notes < 19 are raised by octaves first). Allocation check
`05df`: rejected if the voice belongs to an effect with higher priority; within one tick a voice
keeps the **highest** note (a second note-on on the same voice in the same tick is dropped unless
higher). Then: key off, `voice_note = n`, TL of both operators, `B0 shadow = 20h`, set frequency.

**Volume** (`07ad`): no scaling curve — `TL = 63 − vel'/2` replaces the whole 40h byte (KSL = 0)
for the carrier; for the modulator the instrument's 40h byte is kept only if
`slot ≤ 6 and CA15[slot] == 0`, else it gets the velocity TL too. Because `CA15` is indexed by
voice when written and by slot when read, and slots 7/8 are > 6, the modulators of voices 3, 4, 5
follow different rules than voices 0–2 (voice 3 uses BD's connection bit, voices 4–5 always get
velocity TL). **Quirk, keep it.**

**Frequency** (`0884` → `086d`, `0d86`): `n` is folded into 0..96 by octaves;
`block = n / 12`, `fnum = FNUM[n % 12] + (fraction >> 4) × FSTEP[n % 12]`,
`A0h+v = fnum & FFh`, `B0h+v = fnum >> 8 | block << 2 | keyon`.

| `n % 12` (pitch) | 0 (G) | 1 (G#) | 2 (A) | 3 (A#) | 4 (B) | 5 (C) | 6 (C#) | 7 (D) | 8 (D#) | 9 (E) | 10 (F) | 11 (F#) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| FNUM (C9AA) | 205 | 223 | 244 | 267 | 28B | 2B2 | 2DB | 306 | 334 | 365 | 399 | 3CF |
| FSTEP (C992) | 1 | 2 | 2 | 2 | 2 | 2 | 3 | 3 | 3 | 3 | 3 | 3 |

Driver note 0 = MIDI 19 = G0 (24.5 Hz), so the pitch is exactly MIDI. Check: MIDI 60 → n 41 →
block 3, fnum 2B2h → 690 × 49716 / 2¹⁷ = 261.7 Hz = C4; MIDI 45 → 110.0 Hz.

**Note off** (`08c1`): melodic voices only if `(note − 19) mod 12`-folded equals `voice_note`
(otherwise a stale note-off is ignored); clears key-on in B0h. `force_off` skips the check.

**Program change** (`05a6` → `09a1`): voices 0–5 load instrument `C638[program]`; voices ≥ 6 just
set `voice_program = CA25[voice]` (the rhythm instruments are fixed). `092a` writes 20h/40h/60h/80h
of both slots, then **writes byte 8 to register C0h, not C0h + voice**: only OPL channel 0 ever gets
a feedback/connection value (the last instrument loaded anywhere); channels 1–8 stay at C0 = 0
(FM, no feedback). Verified in the bytes (`b0 c0` / `lcall 1bd8:000e`). **Quirk, keep it** — the
game sounds like this.

**Percussion** (voice 9): `i = note − 36`; rhythm voice `CA30[i]` (6 BD, 7 SD, 8 TT, 9 CY, 10 HH,
15 = ignored; the table has 40 entries, notes ≥ 76 read the next table — no data uses them);
pitch note `C758[i]`. Sets TL from velocity (both slots for BD), ORs the bit into BDh and writes it,
then (voices 6–8 only) the frequency of that channel with the key-on bit clear. Note off clears the
bit. Stopping the music (`0006`) only clears the BD bit (it passes note 0 → 36): other drum bits stay
set until their next note-off. Quirk.

## Sound effects (Verified mechanics; meaning of ids Likely/Unknown)

Game API `sfx_play(id)`: far wrapper `0c1c:1110(id)`, register version `0c1c:111d` (AX = id):

```
if !sound_on (DS:009C != 1): return
if id == 0: sfx_stop_all(); sfx_voice[0..23] (DS:9176) = FFh; return
if id not in (2, 6) and replaying (DS:948B): return
if effects off (DS:008F): return
if id == 14h and DS:9596: id = 17h
if id & 80h: stop effect id & 7Fh (voice_release if it still plays that id)
else: if already playing: return
      prio = DS:915E[id]; if prio == 7Fh: sfx_stop_all()
      v = sfx_start(DS:918E[id], prio); DS:9176[id] = v; voice_sfx_id[v] = id
      if id is 0Fh or 10h: DS:90DE = 1
```

`sfx_start` (`1ace:0db9`): the highest melodic voice of the channel map (AdLib: 5, owner slot 2);
if an effect already holds it, voice 4 (slot 1); if both are held, the one with the lower priority.
Fails (returns FFh) if that voice's priority is higher than the new one. Otherwise key off, owner =
slot + 1, priority, script pointer, wait = 1. Music notes cannot take the voice back until
`voice_release` (`1ace:00ec`) resets owner/priority to 0.

Scripts are arrays of u16 in DGROUP (`DS:91BE`–`944B`). `sfx_step` (`1ace:0e55`) runs **one opcode
per tick** per slot unless waiting; opcodes > 12 = end.

| Op | Args | Handler | Effect |
|---|---|---|---|
| 1 | sign, n | 0eab | slide: `slide = sign ? +n : −n` added to the note word every tick (n/256 semitone per tick) |
| 2 | – | 0ebe | slide off |
| 3 | period, step | 0ec5 | vibrato: `step` added every tick, sign reversed every `period` ticks (first after `period/2 \| 1`) — a triangle |
| 4 | – | 0edf | vibrato off |
| 5 | n | 0eea | wait n ticks (then the next op runs on tick n+1) |
| 6 | note, vel | 0ef4 | key off, note on (MIDI note, velocity; vel 0 = rest) through the device note-on with the effect's priority |
| 7 | x | 0f70 | loop start (after its argument) |
| 8 | x, count | 0f3e | loop end: count+1 passes, 0 = forever |
| 9 | – | 0f78 | end: `voice_release`, reset slot |
| 10 | – | 0f8d | reset slot state |
| 11 | p | 0fb1 | program `C638[p]`, then the device program routine maps it through `C638` **again** |
| 12 | addr | 0fcb | if the engine sound is allowed (`C900 == 0`) and no voice plays id 1: continue at `addr` (the engine script), priority 60h, the voice becomes effect id 1 (`DS:9177`); else end |

`sfx_modulate` (`1ace:0d1c`, each tick): update the vibrato counters of slots 2–0; for effect slots
2 and 1 and every music voice (with slot 0's values, normally 0) compute
`noteword + slide + step`; if it changed, store it and call the device set-frequency (`0884`).

Effect table (`td3snd sfx`; priority `DS:915E`, script `DS:918E`):

| id | prio | script | callers | Contents / guess |
|---|---|---|---|---|
| 1 | 60h | 93DE | 0e12:23df | **engine**: prog 12, note, vibrato (step 31/256 semitone, period 4), endless loop (Verified by the caller) |
| 2 | 7Fh | 926C | 0792:1c00 | stops all; two tones with wide vibrato, 3 × 40 ticks (also during replay) |
| 3 | 78h | 920C | 0e12:33f7 | falling tone with fast vibrato, 30 ticks |
| 4, 11 | 78h/7Ch | 922C | 0e12:4590; 0e12:01d8, 0e74, 0edb | rising tone 15 ticks (gear change?) |
| 5, 10, 12, 22 | 78h/7Ch/68h | 924C | 0e12:0e74, 0edb | fast upward sweep 30 ticks |
| 6 | 7Fh | 9388 | 0792:19ca | stops all; falling tone with big vibrato (also during replay) |
| 7 | 70h | 932C | 0e12:5e70 | two beeps 84, 80 (radar detector? Likely) |
| 8 | 7Ch | 93B4 | 0e12:5e70 | long low tone with slow fall (tyre squeal/skid? Unknown) |
| 9 | 7Ch | 92E2 | 0e12:4fc7 | falling noise-like sweep (crash? Unknown) |
| 13, 18 | 78h | 91BE | 0e12:33f7, 0e12:4631 | rising chirps (horn? Unknown) |
| 14 | 74h | 9318 | 0e12:0db4 | short high blip, 10 ticks |
| 15, 16 | 78h | 9354, 936E | 0977:0008 (started/stopped) | long warbling tone 150 ticks |
| 17 | 78h | 92C8 | 0977:0008 | low rumble, 16 ticks |
| 19 | 72h | 941E | 0977:0008 | falling thud |
| 20 | 7Ah | 92A8 | 0e12:5ffe (on / off 94h) | **police siren**: note 80 with a slow triangle sweep (±2.1 semitones, period 550 ticks = 3.8 s), endless (Likely) |
| 21 | 60h | 93DE | – | same script as the engine |
| 23 | 7Ah | 93FA | id 20 when `DS:9596` | two-tone siren 88/85, 50 ticks each (Likely) |

Most one-shot effects end with op 12, so a finished effect turns its voice into the engine sound when
the engine is not running.

**Engine** (`0e12:23df`, per frame): if `C900` (engine off) stop effect 1; else `sfx_play(1)` (no-op
while it runs), then `x = min(DS:129C, 1500)`, `cx = x × 4 + 1300h` (+400h if `DS:9486` bit 5 is
clear), writes `ch` into the script's note byte `DS:93E6`, and stores **`cx & FF80h` directly into
`voice_noteword[v]`**; `sfx_modulate` sends it to the chip on the next tick. So the engine pitch is
driver note 19–46 (+4) = MIDI 38–65 ≈ 73–350 Hz, with the ±31/256 vibrato. (It also computes
`(old & 7Fh) | cx` and throws it away — harmless bug.) The script note byte is a MIDI note while
the note word is a driver note (−19): only the very first note-on uses the patched byte.

## Port notes (SDL3 + OPL2 emulator)

* Re-implement the driver's AdLib path as C state machines driven by a 145.652 Hz tick generated in
  the audio callback (or a sample-accurate scheduler): per tick run exactly the sequence of
  `snd_tick`, producing OPL register writes into the emulator. `tools/td3snd.py` (`AdlibDriver`)
  is a reference model; its DRO output is the regression target (compare register streams).
* Keep the quirks: C0h-only writes, velocity TL rules, same-tick highest-note rule, 145.65 Hz
  delta units, delta 0 = 1 tick, infinite loops, effects on voices 4/5, direct engine note writes.
* OPL register write delays (6/35 status reads) are hardware waits: drop them.
* Data: `.MUS` and `INSTR.DAT` read at runtime; the effect scripts, priorities and driver tables
  come from DGROUP (export them from the executable like the other tables).

## Open questions

* Listen to the DROs/MIDIs: tempo feel, and whether the C0h quirk really sounds as in DOSBox
  (it must, if the reading is right).
* `DS:B6CC` (scene music A/B/C selection) and the flags `DS:0084`, `DS:008E`/`008F` belong to
  `game_flow`; the exact meaning of effect ids 2–19 needs the simulation/game-flow specs
  (callers listed above).
* `DS:129C` scaling (engine x = 0..1500: RPM / 4?) and `DS:9486` bit 5 (+4 semitones) — simulation.
* Init: `2a5` passes CL from the caller when striking the silent snare (the key-on bit of B7h then
  depends on it); assumed 0.
* The unused API: `1ace:0185` (restart), `01b6`/`01bc` (enable/pause), `03ba` (reset) have no callers.
* MT-32 path (parked): the MPU-401 is driven with the `D0h` ("send data") command before messages,
  i.e. intelligent mode, not UART — check before implementing MT-32 output.
