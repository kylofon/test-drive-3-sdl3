# Test Drive III: The Passion — SDL3 port plan

Same approach as TestDrive1987 / TestDrive2: first a **faithful C port** here
(reverse-engineer → document → port function by function against the original data,
read at runtime), then a separate **TD3 Enhanced** repo built on it.

TD3 (1990, Accolade in-house, Tom Loughry) has a new engine: filled-polygon 3D
world, free driving over a map with side roads, scenery disks. Little code should
carry over from TD1/TD2 apart from the tooling and the port skeleton.

## What we have (recon, 2026-09-23)

| File | Size | Notes |
|---|---|---|
| `TDIII.EXE` | 125 251 | EXEPACK; `tools/unexepack.py` → 166 544-byte MZ, entry 1940:0018, 1391 relocs. "MS Run-Time Library (c) 1988" → MSC 5.1 |
| `SETUP.EXE` | 5 245 | writes `TD3.CFG` (graphics, sound, MIDI flag; no joystick setting) |
| `TD3.CFG` | 6 | 3 words: video (0 VGA, 1 EGA, 2 Tandy), audio (0 PC speaker, 1 Tandy, 2 CMS, 3 MT-32, 4 AdLib/SB), MIDI flag |
| `DATAA.DAT`, `DATAB.DAT` | 125 K / 249 K | named `.LZ` images inside (TITLE1, SELECT, KEYS, COPSEQ, CREDIT*, TOPSCOR*, DIFFLEV*, WATER/WATEREGA, BROKE/BROKEGA …) |
| `DATAC.DAT` | 1.8 K | starts with 6-bit palette-looking bytes |
| `INSTR.DAT` | 1.9 K | AdLib instrument bank ("Ad-lib Sound Board") |
| `SCENE01/02.DAT` | 169 K / 209 K | compressed world (Pacific – Yosemite, …) |
| `SCENE0x.LST` | 1 638 | scene name + parameters |
| `SCENE0x.HI` | 450 | high-score table |
| `C*.DAT/.LST/.POB` | ×5 cars | CERV III, NSX, Diablo, Mythos, Stealth: graphics / specs+name / polygon object? |
| `PLAYDISK.DAT` | 179 | disk manifest: car + scene slots (`?????` = empty, for add-on disks) |

Video modes in the exe: **VGA/MCGA 320×200×256**, EGA 16, Tandy 16.
Sound: Tandy 3-voice, Game Blaster (CMS), MT-32/LAPC-1, AdLib/SB.

## Phases

### 0. Repository — done
- `C:\Coding\TestDrive3`, git `master`, no remote (GitHub later, on request).
- `Game/` original data (git-ignored; the port reads it at runtime).
- `_tools` → junction to `TestDrive1987/_tools` (Ghidra 12.1.3, JDK 21).
- Tools carried over: `unexepack.py`, `x86dis.py`, Ghidra scripts
  (`ApplySymbols`, `DecompileAll`, `SetDS`, `postprocess.py`).

### 1. Executable map — done (2026-09-23, see `port/RE_GUIDE.md`)
- Unpack to `work/`, build the segment/function list (`tools/td3index.py`,
  adapted from `td2index.py`): far-call targets, segment bases, DGROUP, entry.
- Identify and label the MSC 5.1 RTL so it drops out of scope early.
- Ghidra project in `_ghidra/`, `SetDS` + `DecompileAll` → `port/decomp/` (ignored).
- Quick check against TD2 (`td1match`-style) for shared asm helpers
  (keyboard/timer/joystick); expected low overlap.
- Output: `port/RE_GUIDE.md` (addresses as SSSS:OOOO, Ghidra = +1000),
  `port/symbols.csv` seeded with RTL + obvious names.

### 2. File formats — done (2026-09-23, see `FORMATS.md` + `port/formats/`)
Each format gets a decoder that dumps to `work/` plus an image/contact sheet so
it can be checked visually:
1. `DATAx.DAT` archive directory + `.LZ` decompressor → all screens as PNG.
2. Palettes (`DATAC.DAT`, headers of the car files).
3. Car files: `.LST` (name, specs, gear ratios, torque curve), `.POB`, `.DAT`
   (dashboard / car-select art / 3D model).
4. `SCENE*.DAT`: world map, road network, polygon objects, landmarks → top-down
   map render (this became the map viewer in TD2 — worth doing early here too).
5. `.HI`, `TD3.CFG`, `PLAYDISK.DAT`, `INSTR.DAT`, music data.

### 3. Specs — done (2026-09-23): game_flow, simulation, render3d, hud, platform, sound in `port/spec/`
Split as in TD2, adjusted for the new engine:
- `platform` — startup, config, memory, timer/IRQ, keyboard, joystick
- `video` — VGA mode 13h path, page flipping, palette fades, blitters
- `render3d` — camera, transform/projection, clipping, polygon fill, sort order, horizon/sky, dashboard overlay
- `world` — scene loading, road network, object placement, side roads, map
- `simulation` — car physics, gears, damage, radar detector, cops, traffic AI
- `game_flow` — title, car/scene/difficulty select, race, checkpoints, replay/crash sequences, high scores
- `sound` — AdLib music + effects (driver + INSTR.DAT)

### 4. `td3port/` skeleton — done (2026-09-23)
CMake + SDL3, adapted from `td2port`: EXE loader (EXEPACK in C), `mem.h` memory model with VGA
page 0 in `mem[]`, host (145.652 Hz tick, 70 Hz retrace wait, XT byte stream with E0/E1 prefixes,
mouse, gamepad, OPL2 via Nuked-OPL3 + speaker, `--frame-ticks` pacing, `TD3_SNAPSHOT_DIR` /
`TD3_KEYS` developer aids), mode 13h VGA model (DAC, CRTC start, compose), near/far code pointers,
generated `symbols.h`, `PORTING.md`. A placeholder `game_main` (test pattern, Esc quits) proves the
pipeline headless.

### 5. Port, subsystem by subsystem — first integration running (2026-09-23)
Order: platform/video → screens (title, menus: a visible milestone) → scene
loading + 3D renderer (static camera) → driving/physics → traffic/cops →
full game flow → sound. Each step verified against DOSBox captures of the
original (`DOSBOX/`, ignored) and headless snapshots (`SDL_VIDEO_DRIVER=dummy`,
`SDL_AUDIO_DRIVER=dummy`).

### 6. Release, then TD3 Enhanced
Release the faithful port like TD2 (v0.1.0 zip). Then a new
`TestDrive3Enhanced` repo. Because the world is already polygons, Enhanced can
likely render at native resolution / widescreen / 60 fps with longer draw
distance from the same geometry. That goes further than the TD1/TD2 overlays.

## Decisions (2026-09-23)

1. **Graphics**: VGA 320×200×256 first. EGA and Tandy → TODO.
2. **Sound**: AdLib/Sound Blaster through a bundled OPL2 emulator. MT-32, Tandy 3-voice,
   Game Blaster (CMS) → TODO.
3. **Copy protection**: bypassed in the port with a `/* PORT: */` note, documented briefly.
4. **Frame pacing**: design speed. The game advances physics, traffic and the race clock once per
   frame (1 clock second = 5 frames), tuned for ~5 fps. The port keeps the original rule "wait until
   ≥ N timer ticks (145.6 Hz) since the frame started", with DS:B70E = ticks actually elapsed, and
   defaults to N = 23 (~6.3 fps; chosen by play-testing, 2026-09-23 — 29 would run the race clock in
   real time). N is configurable (`--frame-ticks`, launcher "Game speed"; 5 = original engine cap).
   Smooth 60 fps belongs to the Enhanced repo.
6. **Launcher**: a wxWidgets launcher like TD2 Enhanced's (`td3port/launcher`, `-DTD3_LAUNCHER=ON`):
   game folder, car / course / skill (`--car`, `--course`, `--skill`), game speed, sound
   (`--sound adlib|speaker`), window size, full screen.
5. **OPL2 emulator**: Nuked-OPL3 (LGPL-2.1), compiled as a separate file with its licence shipped.

## TODO (parked)

- [ ] EGA 16-colour build
- [ ] Tandy 16-colour build
- [ ] MT-32 / LAPC-1 music
- [ ] Tandy 3-voice sound
- [ ] Game Blaster (CMS) sound
