# Test Drive III: The Passion (1990, Accolade) — file formats & internals

Addresses as in `port/RE_GUIDE.md` (`SSSS:OOOO` file segments, `DS:xxxx` = DGROUP 1BE4).
Status tags: **Verified** (decoded and checked visually or byte-exact), **Likely**, **Unknown**.

## Tools (run from the repository root; Python 3.12 + Pillow + capstone)

| Tool | Does |
|---|---|
| `tools/unexepack.py Game/TDIII.EXE work/TDIII_unp.exe` | EXEPACK unpacker |
| `tools/td3res.py list` / `extract [dir]` | archive directory with recovered names; extracts every archived file of every car and scene to `work/res/<archive>/` |
| `tools/td3img.py [dir]` | every LZW+RLE picture → `work/img/*.png` + `sheet.png` |
| `tools/td3obj.py` | 3D objects, road tiles, car `.POB` → `work/obj/` sheets |
| `tools/td3world.py` | leg maps → `work/world/` (per leg, per scene, sprites, tiles) |
| `tools/td3car.py` | car/scene `.LST`, `.HI`, `TD3.CFG`, `PLAYDISK.DAT`, code sheet → `work/desc/` |
| `tools/td3snd.py all` | music → event listings, MIDI, OPL2 `.dro` renders; `INSTR.DAT`; effect scripts → `work/sound/` |

Detailed format documents (field tables, reading code, open questions):

| Document | Covers |
|---|---|
| [port/formats/descriptions.md](port/formats/descriptions.md) | car `.LST` (gearbox, physics, gauges, picture counts), scene `.LST`, `.HI`, `TD3.CFG`, `PLAYDISK.DAT`, copy-protection code sheet (`MASTERQ.BIN`) |
| [port/formats/objects.md](port/formats/objects.md) | 3D object format (O set, road tiles in `T.BIN`, car `.POB`), faces/colours/face types, lane paths `P.BIN` |
| [port/formats/world.md](port/formats/world.md) | leg maps `<scene>A..E.DAT` (32×16 tile grid, coordinates, objects, colour tables), tile placement, surface types, finish/route detection, sprites `<scene><d>.DAT` |
| [port/formats/sound.md](port/formats/sound.md) | `.MUS`, `INSTR.DAT`, driver data model, AdLib (OPL2) mapping and quirks, sound effects |

## Files

| File | What |
|---|---|
| `TDIII.EXE` | EXEPACK'd MS C 5.1 program; holds the archive directory (below) |
| `TD3.CFG` (6 bytes) | 3 words: video (0 VGA, 1 EGA, 2 Tandy), audio (0 PC speaker, 1 Tandy, 2 CMS, 3 MT-32, 4 AdLib/SB), MIDI flag (descriptions.md) |
| `DATAA.DAT`, `DATAB.DAT`, `DATAC.DAT` | archives `a`, `b`, `c` (front-end pictures, palettes, music, shared scene data) |
| `C<car>.DAT` / `.LST` / `.POB` | car archive `d`; car description + its directory part; 3D model of the car (objects.md) |
| `SCENE<nn>.DAT` / `.LST` / `.HI` | scene archive `e`; scene description + its directory part; high scores |
| `PLAYDISK.DAT` | "MASTER PLAY DISK" + car base names + scene base names, `?????` = empty slot (add-on disks) |
| `INSTR.DAT` | AdLib instrument bank |

## Archive directory (Verified)

`DS:049E` in the executable, 14-byte entries, ended by a zero hash:

| Offset | Size | Field |
|---|---|---|
| 0 | u16 | h2 |
| 2 | u16 | h1 |
| 4 | u8 | archive: `a`/`b`/`c` = `DATAA/B/C.DAT`, `d` = selected car's `.DAT`, `e` = selected scene's `.DAT` |
| 5 | u8 | 0 |
| 6 | u32 | offset in the archive |
| 10 | u32 | size |

* Name hash (`0000:1760`, of the name without drive): `h1` = over all characters from the last to the
  first, `h = h*0x101 + c`; `h2` = Σ `c[i]*i` over all characters except the last; 16-bit, signed
  chars. Lookup `0000:151c` compares both words, sets the size in `DS:E86A` and seeks the archive;
  a name not in the directory is opened as a plain file.
* The 15 `d` entries (`DS:074C`) and 29 `e` entries (`DS:081E`) are overwritten from the selected
  car's `.LST` (offset 1D1h) and scene's `.LST` (offset 4D0h). The executable holds CCERV's and
  SCENE01's.
* Loading (`0000:0ee0` far, `0000:0e74` near, `0000:0d62` palette) reads the raw bytes; decompression
  is done later by the caller.
* All 93 names are recovered (hash collisions `CSTELSC.BIN` = `CMYTH8T.BIN`, `KEYCOLR.BIN` =
  `CSTELJT.BIN` are resolved by context). Scene names are built as `<scene>` + suffix,
  `<scene><leg digit>` + `.DAT`/`.COL`, `<scene><'A'+leg>` + `.DAT`; `.COL` is then patched in place to
  `.BLZ` / `.ALZ` for that leg's two pictures.

### Contents

| Archive | Files |
|---|---|
| `a` DATAA | `ACCO.LZ` + `ACCOCOLR.BIN`; `TITLE1/2.LZ`, `TITLEANI/CAR/LET/L2.LZ` + `TITLCOLR.BIN`, `TITL2COL.BIN`; `CREDITA/B/C.LZ` + `CREDCOLR.BIN`; `THEME.MUS`; in-game overlays `COMPASS`, `CHASE`, `WATER`, `WATEREGA`, `BROKE`, `BROKEGA` `.LZ` |
| `b` DATAB | police `COPA/B/SEQ.LZ` + `COPCOLR.BIN`; `KEYS.LZ` + `KEYCOLR.BIN`; `SELECT.LZ`; `DIFFLEVA/B/C.LZ`; `SSBJ.LZ`; `TOPSCORA/B/C.LZ` + `TOPCOLR.BIN`; `NEWWAVE.MUS`; `MASTERQ.BIN`; shared scene data `SCENETT1.DAT`, `SCENETTA.DAT`, `SCENETTT.BIN`, `SCENETTO.BIN`, `SCENETTP.BIN` |
| `c` DATAC | `SELCOLR.BIN`, `DIFFCOLR.BIN`, `OTWCOL.BIN` (in-game "out the window" palette), `DETAIL1/2.LZ`, `COMPASS.LZ` |
| `d` car | `<car>COL.BIN` (cockpit palette), `<car>SC.BIN` (select-screen palette), `<car>SIC.BIN` (32 colours), `.ICN` (select picture 208×83), `.BIC` (112×83), `.SID` (112×117), `.SIC` (72×40 icon), `.ETC` (gear shift, 56×72), `.TOP` (320×16 top bar), `1.BOT` / `2.BOT` (cockpit 320×44), `L.BOT` / `R.BOT` (steering wheel turned left/right, 168×48), `FL1.LZ` / `FL2.LZ` (spec card pages, 208×117 / 208×200) |
| `e` scene | `.ICN` (banner 320×33), `.SIC` (72×40), `T.BIN` (road tiles 40h+, 65 KB), `O.BIN` / `P.BIN` (objects / lane paths; SCENE01 has 7-byte `TJL 90\xFF` placeholders and uses the shared `SCENETT` sets, flag `DS:0B15`; SCENE02 has its own), `<n>.COL` + `<n>.ALZ` (320×50) + `<n>.BLZ` (320×19) per leg n = 1..5 (legs without pictures have 7-byte placeholders), `1.DAT`, `A`..`E.DAT`, `A/B/C.MUS` |

## Pictures (Verified, `tools/td3img.py`)

* Compression (`0ab4:0047`): LZW, codes LSB-first, 9 bits growing to 12 (one more bit once the next
  free code reaches `1 << bits`), 100h = clear (back to 9 bits, next code 102h), 101h = end, first
  code after a clear is a literal. Every picture file (`.LZ`, `.ALZ`, `.BLZ`, and the car's `.ICN`,
  `.BIC`, `.SID`, `.SIC`, `.ETC`, `.TOP`, `.BOT`) starts with a clear code.
* Decompressed: (colour, count) byte pairs. The draw call (`01f4:59ac` → VGA `0c1c:0c45`) gets the
  pair count, the width, x and the **bottom** row y: runs fill the rectangle bottom-up, left to right,
  wrapping into the row above; `DS:90F0` (0 or 80h) is added to every colour. The target is a linear
  320×200 page (segment from `DS:90CC[DS:009A]`: page 0 = VGA memory A000h, page 1 = RAM).
* Pair counts and widths are hard-coded at the call sites or kept in the car/scene `.LST`
  (`DS:0B14`..`0B68`). Widths not known from a call are found by `fit_width` (best vertical
  coherence among exact divisors).

## Palettes (Verified)

* `*COLR.BIN`, `*COL.BIN`, `<scene><n>.COL`, car `COL.BIN`/`SC.BIN` = 337 bytes: 112 × 6-bit RGB +
  `FF`. `0000:0d62` reads 336 bytes to colour **16 + DS:90F0** of the palette buffer `DS:0B6A`
  (256 × RGB): colours 16–127, or 144–255 for the second set (car graphics are drawn at +80h into the
  car's palette). Colours 0–15 come from the executable's initial buffer.
* `<car>SIC.BIN` = 97 bytes: 32 colours + `FF`, read by `0000:0df6` to `DS:0C2A` = colour **64** +
  `DS:90F0` (game_flow port; the icon now renders correctly).
* Fades scale the buffer (`(c * level + 8) >> 4`).

## Summary of the rest (details in port/formats/)

* **World** (world.md): each scene has 5 separate leg maps. `<scene>A..E.DAT` is a memory image of
  `DS:9592..B6C9`: header, 32×16 cell map (tile type, height, quarter-turn rotation), initial
  objects (player start, 2 opponents, statics, traffic), colour tables. X east 0..7FFFh, Z north
  0..3FFFh, cell 400h. A leg ends on the face with surface type 1Fh (gas station tile); the route
  taken is recorded from three route tiles named in the header. Legs run west→east (SCENE01) /
  east→west (SCENE02). Sprites (`<scene><d>.DAT`): trees, rocks, animals, sun/moon, invisible
  "wrong way" / "back to main road" triggers.
* **Objects** (objects.md): one format for roadside objects, vehicles (8-byte header with far LOD,
  collision boxes), road tiles (with sprite children) and car `.POB`; faces are points/lines/
  triangles/quads with two dithered colour codes and a face type (lights, brake lights, beacon,
  wipers...). `P.BIN` = traffic lane waypoints per tile type (Likely).
* **Descriptions** (descriptions.md): car `.LST` = 10 blocks read to fixed DS addresses (picture
  counts, gearbox with real ratios, gauges, units mph/km, physics: grip ≈ lateral g, governor
  ≈ top speed; 1 speed unit ≈ 1.64 mph, 1 rpm unit ≈ 10 rpm); scene `.LST`; `.HI` with checksum;
  `PLAYDISK.DAT` also holds the last selections (car, scene, skill, players, steering response).
* **Sound** (sound.md): `.MUS` = MIDI-like events with tick delays at 145.6 Hz, looping forever;
  `INSTR.DAT` = AdLib / MT-32 / Casio banks; AdLib driver in rhythm mode (channels 0–5 + drums),
  with register quirks the port must copy; 23 sound effects as small scripts on voices 4–5, engine
  = effect 1 with its pitch set every frame.

## Still open

* Leg header bytes, some surface and face types, the zero-face tile 67h in SCENE02, traffic
  behaviour details (world.md / objects.md).
* Physical units of some physics fields (descriptions.md).
* Listening check of the `.dro` renders against DOSBox; meaning of most effect ids (sound.md).

## Errata from the phase 3 specs

* objects.md: the `w1` colour code of a face goes on pixels where (x ^ y) is **odd**, not even
  (render3d.md, span filler `0e12:41d4`).
* world.md: `DS:95D5` / `95D6` are the lighthouse beam radius and the gate arm length (not speeds);
  `DS:95CD` is a minimum projection distance (not a limit) (render3d.md). `DS:95BE` is most likely the
  leg's compass north offset (hud.md). `DS:9596` set = two-tone siren (sound.md; 0 in all shipped legs).
* descriptions.md: the third byte of a time record is neither hundredths nor tenths: the clock stores
  0–3, the maths treats it as tenths, the record screen prints `(x % 100) / 10` (game_flow.md). The
  odometer position `DS:CC78/CC7A` is in page-1 coordinates (inside the stored speedometer picture),
  not screen coordinates (hud.md).
* `L.BOT` / `R.BOT` are the steering wheel turned left / right, not look-left/right views; `COMPASS.LZ`
  is a 152×8 heading strip (hud.md). `COPA/COPB/COPSEQ.LZ` are used only by the dead copy-protection
  code.
* The game runs in graphics library mode **13h** (platform.md); `DS:90CC` = {A000h, RAM page 1}.
