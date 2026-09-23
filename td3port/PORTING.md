# Porting rules — Test Drive III: The Passion → SDL3

Faithful reimplementation of **TDIII.EXE**, VGA (graphics library mode 13h) with AdLib sound.
Behaviour, integer arithmetic and visible quirks must match the original; the only intended change of
feel is the frame pacing default (PLAN.md decision 4). Specs: `../port/spec/*.md` (read
`../port/RE_GUIDE.md` first), data formats: `../FORMATS.md` and `../port/formats/*.md`. Symbols:
`../port/symbols.csv` → generated `src/symbols.h` (`python tools/gen_symbols.py` from the repository
root, after `python tools/merge_symbols.py`). Ground truth when a spec is unclear:
`python ../tools/x86dis.py ../work/TDIII_unp.exe dis SSSS:OOOO LEN` (run from `td3port/`). The Python
tools are working reference decoders: `tools/td3res.py` (archive, LZW), `td3img.py` (pictures),
`td3obj.py` (3D objects), `td3world.py` (leg maps), `td3car.py` (car/scene files), `td3snd.py`
(the AdLib driver, rendering OPL register streams to `work/sound/*.dro` — regression references).

## Architecture

```
main.c          args, mem_load_exe(), host_init(), vga_init(), modules_init(), game_main()  (coordinator)
portcfg.h/.c    command-line options applied inside the game (--car/--course/--skill/--sound) (coordinator)
launcher/       the wxWidgets launcher "Test Drive III" (-DTD3_LAUNCHER=ON, launcher/README.md)  (coordinator)
mem.h/.c        real-mode memory: TDIII.EXE image at segment 0x1000, DGROUP 0x2BE4, heap above,
                VGA page 0 at A000:0000                                                       (coordinator)
host.h/.c       SDL3: window/present, 145.652 Hz tick, 70 Hz retrace, XT keyboard bytes, mouse,
                gamepad, OPL2 (Nuked-OPL3) + speaker audio, files, frame pacing setting        (coordinator)
codeptr.h/.c    far / near code pointers stored in game data -> C functions                   (coordinator)
symbols.h       generated DS_* / FN_* constants                                               (coordinator)
platform/vga.*  mode 13h hardware model: DAC, CRTC start (shake), frame composition            (coordinator)
platform/*      platform.md: timer ISR body, keyboard ISR + tables, joystick, mouse driver, DOS
                file wrappers and allocator, graphics library (mode 13h paths), text/font,
                pictures (LZW + RLE), palette upload/fades, view presenter, MSC runtime bits   (platform)
sound/*         sound.md: driver core, AdLib and PC-speaker device routines, effects          (sound)
game/flow*.c    game_flow.md: main (game_main), start-up, screens, menus, race_run, results   (game flow)
game/hud*.c     hud.md: cockpit set-up and per-frame instruments, overlays                    (hud)
game/sim*.c     simulation.md: car physics, controls/keys, traffic, police, collisions, replay (simulation)
game/render*.c  render3d.md: world build, camera/projection, faces, sprites, mirror, sky       (render3d)
game/game.h     cross-subsystem prototypes of the game code                                   (coordinator)
```

Only `host.c`, `mem.c`, `main.c` and the file wrappers include SDL. Game, platform and sound code talk
to the host through `host.h`. Each module declares its API in its own header.

## Memory model (`mem.h`)

* The original's data stays **in `mem[]` at its original address**. Every global, table, string, the
  leg map image (DS:9592..B6C9), the car `.LST` blocks, the vertex/projection arrays (DS:2500..8DC1)
  and the heap blocks are read and written there: `DSW(DS_game_state)`, `ds_far(DS_x)`. Names come
  from `symbols.h`; for an offset without a name write the raw offset with a comment:
  `DSW(0x94B8) /* units_miles */`. Never shadow game state in C variables that outlive a function.
* **VGA page 0 is in `mem[]` at A000:0000** (mode 13h, 320 bytes per row). The game draws to it and to
  RAM page 1 through the same far pointers (`DS:90CC[DS:009A]`); the VGA model composes the screen
  from there. DAC and CRTC writes go through `platform/vga.h`.
* Code-segment tables and variables (sine/atan tables in 0e12, the replay ring buffer in 0ab4, the key
  handler table 0e12:0000, keyboard tables in 0c1c) are read and written in their segment:
  `SEGW(0x0E12, 0x0FF2) /* sin_tab */`. Self-modifying code (the clip constants 0e12:409c patches into
  the span filler) becomes ordinary variables in `mem[]` at the patched addresses, read where the
  instruction used its immediate; mark it `/* PORT: */`.
* **Pointers follow the original model (MSC medium model):** a near data pointer is a DGROUP offset
  (`u16 name_ds`); a far pointer is a `FarPtr` `{off, seg}` passed by value.
* Code pointers stored in data (key handler table, sound device routine tables, MIDI handler table)
  keep their stored values in `mem[]`; map them with `codeptr.h` (`codeptr_lookup_near` for near
  tables).
* Fixed-width arithmetic: use `u8/s8/u16/s16/u32/s32` exactly as the original register widths; cast at
  every step where the original truncates. Signed right shift = `(s16)x >> n`. Emulate carries/borrows
  explicitly where the asm uses `adc/sbb/rcl/rcr`. The MSC long helpers are plain `s32/u32` operators;
  where a spec says a 32-bit product wraps, it wraps (u32).
* **Division** goes through `div32_16 / idiv32_16 / div16_8 / idiv16_8` for every DIV/IDIV the
  original performs; a divide error ends the program with R6003 as the runtime does, except at the
  call sites render3d.md lists as overflow risks, which clamp (`/* PORT: */`).
* Keep the original's bugs and quirks the specs mark as faithful (sawtooth torque, the nose-pitch
  clamp that never clamps, the bubble-sort off-by-one, the AdLib C0h register quirk, ...), but never
  read or write outside `mem[]`.

## Timing (`host.h`)

* The host calls the tick handler at exactly 145.652 Hz (PIT divisor 2000h) from `host_pump()`. The
  platform timer module installs the ISR body: `snd_tick`, `DS:00A0++`, the BIOS tick every 8th.
* **Every busy-wait loop of the original must call `host_pump()` once per iteration** (tick waits, key
  polls, delays). Port 3DAh retrace polls become `host_wait_vretrace()` (70.086 Hz).
* **Race frame pacing (PLAN.md decision 4):** `race_run` keeps the original rule "loop until at least
  N ticks have passed since the frame started" with N = `host_frame_ticks()` (default 23, option
  `--frame-ticks`, original constant 5), and `DS:B70E` = the ticks the frame actually took. Nothing is
  converted to real time: the simulation and the race clock stay per frame. Unpaced loops of the
  original (the main menu's rotating car, water roll, dissolves) are paced as the specs recommend.
* The screen is composed from VGA memory and presented by the host when it changed (at most every
  8 ms, VSync). CRTC start changes (screen shake) are part of the composition.

## Input

* SDL key events arrive as the XT byte stream (E0/E1 prefixes included) through
  `host_set_kbd_handler`; the platform keyboard module implements `kbd_isr` (0c1c:0e98) on top of it,
  including the synthetic `FA` after its LED command (platform.md 4.4.1).
* Mouse: `host_mouse_read` / `host_mouse_motion` replace INT 33h. Joystick: `host_joy_read` (gamepad)
  replaces port 201h with synthesised counts (platform.md 4.4.3).

## Sound

* The sound driver writes OPL2 registers with `host_opl_write` and the speaker with `host_speaker`,
  inside `snd_tick` (tick handler) or from game calls between ticks; the host generates one tick of
  samples after each tick. Compare against `work/sound/*.dro` (td3snd.py renders).
* Audio device: the port behaves as `TD3.CFG` audio 4 (AdLib/SB) unless the file says 0 (PC speaker);
  1–3 (Tandy, Game Blaster, MT-32) are parked and fall back to AdLib (`/* PORT: */`).

## Dropped / replaced (mark with `/* PORT: ... */`)

* Copy protection (01f4:1ec2): the "passed" path (`protection_ok` = 1 as after a correct answer).
* Disk handling / Play Disk prompts: the files are always there; `A:` drive prefixes of built paths are
  ignored; files are looked up case-insensitively in the game directory (`host_game_path`), written
  files (`.HI`, `PLAYDISK.DAT`) go there too.
* Video modes other than 13h, EGA/Tandy/Hercules paths, MT-32/Tandy/CMS sound: not ported (PLAN.md
  TODO).
* Port I/O, interrupt vectors, DOS memory allocation: see platform.md §6.

## Porting a function

1. One C function per original function, named as in `symbols.h` (`FN_<name>`), with a leading
   comment `/* 0e12:76ec frame_update — game_flow.md §4.13 */`. Assembly routines with register
   arguments become C functions with explicit parameters/returns named after the registers they model.
2. Follow the spec pseudocode; confirm against the disassembly wherever the spec says `likely`/`guess`,
   or where signedness, carries or evaluation order matter.
3. Mark deliberate deviations with `/* PORT: ... */`. Mark unresolved doubts with
   `/* TODO(verify): ... */` and list them in your report.
4. No `static` game state: state lives in `mem[]` (a `static` is fine for pure host-side caches).
5. Don't reformat or restructure files owned by another module. If you need a declaration that isn't in
   a shared header, add it to **your own** header and list it in your report.

## Build and checks

From `td3port/` with MinGW on PATH (`export PATH="/c/msys64/mingw64/bin:$PATH"`):

```
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/td3port.exe --game-dir ../Game --check
```

Headless run (no window, no sound): `SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy
TD3_SNAPSHOT_DIR=../work/snap TD3_KEYS="5:1c" ./build/td3port.exe --game-dir ../Game` saves a frame
every 2 s and presses Enter after 5 s (host.h, developer aids).

Your own files must **compile without warnings**. Check a single file with
`gcc -std=c11 -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing -fsyntax-only -Isrc -I/c/msys64/mingw64/include src/<file>.c`.
You may write throwaway verification programs in your scratch area (e.g. compare the LZW decoder or a
picture against the Python tools). Do not create HTML pages. Do not launch the game window; always
run with `SDL_AUDIO_DRIVER=dummy`.
