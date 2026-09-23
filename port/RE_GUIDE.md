# Reverse-engineering guide for the SDL3 port

Shared conventions for everyone writing port specs. Target: **TDIII.EXE**, VGA path (graphics
library mode 13h, see platform.md). EGA, Tandy and the MT-32 / Tandy / Game Blaster sound drivers are parked (PLAN.md, TODO).

TD3 has a new engine (Accolade in-house, not DSI). Unlike TD2, it shares no game or assembly-library
code with TD1/TD2: only the Microsoft C runtime matches (`port/tdiii_td2_matches.csv`). The TD1/TD2
repos (`../TestDrive1987`, `../TestDrive2`) are useful for workflow and port skeleton, not for names.

## Files

| Path | What |
|---|---|
| `work/TDIII_unp.exe` | EXEPACK-unpacked MZ (`tools/unexepack.py Game/TDIII.EXE work/TDIII_unp.exe`); image offsets exclude the MZ header |
| `port/tdiii_functions.json` / `.csv` | Capstone index (`tools/td3index.py`): extent, near/far, callers/callees, DS reads/writes, strings, ints, ports, jump tables |
| `port/tdiii_td2_matches.csv` | Functions matching a TD2 function (`tools/td2match.py ../TestDrive2`): the C runtime |
| `port/decomp/tdiii_ds.c` | Ghidra decompilation of every indexed function with the merged names from `port/symbols.csv`; DGROUP globals renamed to DS offsets |
| `port/decomp/tdiii_globals_xref.txt` | For each DS global: which functions use it |
| `port/symbols.csv`, `port/symbol_conflicts.txt` | Merged symbols of all specs (`tools/merge_symbols.py`) |
| `tools/x86dis.py work/TDIII_unp.exe dis SSSS:OOOO LEN` | Ground-truth disassembly when the decompile looks wrong |
| `FORMATS.md` | Decoded file formats (phase 2) |
| `_ghidra/TD3.gpr` | Ghidra project (`_tools/ghidra_12.1.3_PUBLIC/ghidraRun.bat`, JDK in `_tools/jdk-21*`) |
| `Game/` | Original game files |

## Addresses

* Microsoft C 5.1 medium model: 40 code segments (from the relocation table), far calls between
  them (`call far` = `9A`, `retf`), near calls inside a segment.
* Write code addresses as **`SSSS:OOOO`** with the segment as stored in the file: `0e12:0080`.
  Image offset = SSSS×16 + OOOO (`0x0E1A0`). Ghidra loads the image at segment `1000`, so the same
  function is `FUN_1e12_0080` in Ghidra; `port/decomp/tdiii_ds.c` has names rewritten back to
  `FUN_0e12_0080`.
* **DGROUP** is segment `0x1BE4` (image `0x1BE40`). `DS:xxxx` = image `0x1BE40 + xxxx`. C code always
  runs with DS = DGROUP; assembly routines that load DS themselves are flagged `sets_ds` in the index.
* A relocated segment value is where code is *called*, not always where its module starts: jump
  tables can run past the next "segment" start (e.g. `1905:026b`).
* In `tdiii_ds.c` a global is written `<type>_DSxxxx` or `DS_xxxx`. Ghidra's `CONCAT11`, `._1_1_`
  etc. are byte-level artifacts of 16-bit code: simplify them in the specs.

## Segment map (first pass, to be refined by the specs)

| Segment(s) | Size | Contents |
|---|---|---|
| `0000` | 8 K | `main` area: startup, RAM check ("556K VGA & Tandy / 526K EGA"), TD3.CFG, fatal messages, high-score file write, crash/water screens (`BROKE.LZ`, `WATER.LZ`, `CHASE.LZ`, `COMPASS.LZ`) |
| `01f4` | 23 K | Front end: Accolade logo, title animation, credits, menus, top scores (`*.LZ` + `*COLR.BIN` palettes). `01f4:1ec2` = copy-protection check, **already patched out** in this copy (NOPs; dead code `01f4:1ed7`–`01f4:262a`) |
| `0792` | 8 K | Car and scene loading: `C*.LST/.DAT/.POB`, `SCENExx.LST`, `OTWCOL.BIN` ("out the window" palette), `L.BOT`/`R.BOT`, `T/O/P.BIN` |
| `0977` | 5 K | One 5 KB function `0977:0008`, run by `frame_update` unless paused/replay: no drawing, most likely the car simulation |
| `0ab4` | 6 K | Small helpers `0ab4:000a`–`01f0` (`0ab4:006a` has no direct callers) + `0ab4:1220` (per frame, from `frame_update`) |
| `0c1c` | 8 K | Platform: timer (PIT 40h/43h, PIC 20h), keyboard ISR `0c1c:0e98` (INT 9), config (`TD3.CFG`), DOS file calls; ASCII key map at `0c1c:0cae` |
| `0e12` | 36 K | The engine (170 functions): held-controls poll `0e12:0080`, key dispatch `0e12:0084`, 3D world, driving. Also lookup tables (`0e12:0ff2`–`22fe`, `267e`, `7004`) |
| `16cf`–`1937` | 9 K | Graphics library, one module per primitive, each dispatching on the mode (see below). `16e6`–`16f5` = mouse (INT 33h), `16ff` = BIOS time (INT 1Ah), `1905:000b` = `gfx_set_mode` |
| `1940` | 6 K | MS C 5.1 runtime (entry `1940:0018` `_astart`) |
| `1ace`, `1bd7`, `1bd8` | 4 K | Sound/music driver: device routine tables, MIDI-style event dispatch |

### Graphics library dispatch

Library routines index a 21-entry table (one per mode 00h–14h) at the current mode:
`lea bx,[table] / add bx,[DS:BD44] / jmp cs:[bx]`. `DS:BD44` = mode × 2, set by `gfx_set_mode`.
Modes: **13h = BIOS 320×200×256, the VGA build's only mode** (TD3.CFG video 0 → table DS:00EA
`13 0D 09` → DS:E338; `gfx_set_mode` is called only by config_load and the quit paths). 0Dh = EGA,
09h = Tandy (parked). 14h = unchained 320×200×256 exists in every primitive but is never selected
(an earlier version of this guide wrongly called it the game mode). Page 0 = A000h (visible), page 1 =
a 64000-byte RAM page; `DS:90CC[DS:009A]` picks the draw target. The port implements the 13h path only.

### Code reached through pointer tables

`tools/td3index.py` seeds these by hand (`PTR_TABLES`): keyboard handlers `CS:0000` of `0e12`
(codes 40h–7Fh) and `DS:B6EF` (81h–8Ah); sound device routines `DS:C906`, per-device tables
`DS:C92E` (5 tables × 5 devices, installed by `1ace:02a5`), MIDI status handlers `DS:C975`.

Code still not reached (resolve during the specs): `0e12:06c3`, `1ace:0185` (ISR-like: pushes all,
no DS load), `1ace:0eab`, `192e:0063`, `1940:188d`, `173c:01ca`–`02be` (a module linked at segment
`1758` with no callers — assembled for another CS, probably dead).

## Program flow (first pass)

* `main` (`0000:0000`) is a state machine on `DS:0086`: 1 title/attract, 2 copy-protection check
  (`protection_ok` DS:0088; without it the game returns to the menus after one run), 4 menus
  (`01f4:144c` returns the choice in `DS:E5AC`), 5 race: legs × players, each leg runs
  `race_run` (`0792:000c`), then times/averages and high scores; 3/6 results.
* `race_run` loads the stage and loops per frame: `frame_update` (`0e12:76ec`) → `race_input(0)` →
  `frame_draw` (`0e12:77d5`) → `race_input(1)` → dashboard/HUD updates (`0792:10c8`, `15d4`,
  `16c4`, `1952`) → `race_input(2)`, then waits until **≥ 5 timer ticks** have passed since the
  frame started.
* Timer: `timer_install` (`0c1c:109f`) sets PIT channel 0 to divisor 2000h = **145.6 Hz**; so the
  game runs at most ~29 fps. `timer_restore` (`0c1c:10cf`) puts back 18.2 Hz.
* The engine in `0e12` does no port I/O: it draws into the linear pages (page 0 = VGA memory,
  page 1 = RAM); each race frame waits for vertical retrace before the 3D view is copied (platform.md,
  render3d.md).

## Specs (phase 3, done 2026-09-23)

`port/spec/game_flow.md`, `simulation.md`, `render3d.md`, `hud.md`, `platform.md`, `sound.md`, each
with `<spec>_symbols.csv`; merged by `tools/merge_symbols.py` (owner by segment range, otherwise
priority render3d > simulation > platform > sound > game_flow > hud > format docs > seed; other names
kept in `other_names`). Phase 2 format documents are in `port/formats/`, errata in `FORMATS.md`.

## Subsystem split (phase 3 specs)

Split by call tree, not address range: `0e12` mixes update and drawing. Roots are the functions
called from `frame_update` / `frame_draw`.

| Spec file | Code | Scope |
|---|---|---|
| `game_flow` | `0000`, `01f4`, `0792` except the per-frame HUD | `main` state machine, startup/RAM check, config, title/credits/menus/car and scene select/difficulty/top scores, results and high-score files, messages (`0000:179c`), `race_run` and stage loading, disk/Play Disk handling (briefly), copy protection (briefly: dropped) |
| `simulation` | `0977:0008`, `0ab4:1220` tree, `0e12` trees of `095a`, `23df`, `6e92`, `70cd`, `0b1d`; key handlers (`0e12:0080` tables) | car physics, gears, damage, steering/throttle input, road following, traffic, police, radar detector, checkpoints, time and fuel, replay (`DS:948B`?) |
| `render3d` | `0e12` trees of `34c3`, `323e`, `6421`, `46c0`, `4c51`, `7c21` + remaining `0e12` functions not reached above; lookup tables in `0e12` | camera, transform/projection, clipping, polygon fill, sorting, sky/horizon, objects and cars, rear-view mirror, the frame buffer and how it reaches VGA |
| `hud` | `0792:0414`, `0792:0922`–`1c66` per-frame parts | dashboard, gauges, map/compass, messages drawn over the 3D view |
| `platform` | `0c1c`, `16cf`–`1937` (VGA mode 13h paths only), `0ab4:000a`–`01f0`, `1940` (identify only) | timer, keyboard ISR, joystick/mouse, config, file and memory helpers, unpacker (`.LZ`?), graphics library primitives and blitters, palette |
| `sound` | `1ace`, `1bd7`, `1bd8`; `INSTR.DAT` | music/effects driver, AdLib/SB device routines (others: note only), music data format |

If a function clearly belongs to another subsystem, list it with a "see `<spec>`" note instead of
analysing it in depth. Deliverable format as in TD2 (`../TestDrive2/port/RE_GUIDE.md`, "Deliverable
format"): overview + call graph, function table, globals table, pseudocode, file formats,
hardware/DOS dependencies with the SDL3 replacement, timing, open questions, and
`port/spec/<subsystem>_symbols.csv` (`kind,address,name,type,notes`).

## Regenerating

```
python tools/unexepack.py Game/TDIII.EXE work/TDIII_unp.exe
python tools/td3index.py work/TDIII_unp.exe port/tdiii
python tools/td2match.py ../TestDrive2
python tools/merge_symbols.py
tr -d '\r' < port/symbols_ghidra.txt > work/symbols_ghidra.txt
tr -d '\r' < port/tdiii_starts.txt > work/tdiii_starts.txt
analyzeHeadless _ghidra TD3 -import work/TDIII_unp.exe -overwrite -scriptPath tools/ghidra \
    -preScript SetDS.java 1BE4 -postScript FixNearFlows.java 1BE4 \
    -postScript ApplySymbols.java work/symbols_ghidra.txt 1BE4 \
    -postScript DecompileAll.java work/tdiii_starts.txt port/decomp/tdiii.c 1BE4 120 only
python tools/ghidra/postprocess.py port/decomp/tdiii.c 1BE4
```

`FixNearFlows.java` is needed because segment `0e12` crosses linear 0x20000 (at offset 1EE0): Ghidra
wraps near branch targets there in the wrong segment, so some backward calls resolved into DGROUP.
The script re-targets them with call-override references and removes the phantom DGROUP functions.
`only` restricts the output to the indexed functions (Ghidra also "finds" functions in data).

(`JAVA_HOME` = `_tools/jdk-21.0.12.1+1`; `analyzeHeadless` is in
`_tools/ghidra_12.1.3_PUBLIC/support/`.)
