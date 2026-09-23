# World / road data: leg maps (`<scene>A..E.DAT`), tile placement, lanes, sprites

Addresses as in `port/RE_GUIDE.md` (`SSSS:OOOO` file segments, `DS:xxxx` = DGROUP 1BE4).
Tags: **Verified** = parsed and rendered consistently (`tools/td3world.py` → `work/world/`) or read
directly in the code; **Likely** = consistent with code and data but not fully traced; **Unknown**.

The polygon model format (tiles in `T.BIN`/`SCENETTT.BIN`, objects in `O.BIN`, `.POB`), face colours
and the `P.BIN` lane waypoint record are documented in `port/formats/objects.md` (objects agent).
This file covers how the world is assembled from them, and the other world files.

Tool: `python tools/td3world.py` renders every leg of SCENE01, SCENE02 and the demo set SCENETT to
`work/world/<scene>_<leg>.png` (64 px per cell) and `<scene>_legs.png` (all 5 legs), plus
`<scene>_sprites.png`. `dump SCENE LEG` prints header, map and object list; `tiles SCENE` draws
every tile type top-down with its lanes (`<scene>_tiles.png`). Map legend: faces top-down
(terrain dimmed, road surfaces full colour), yellow = traffic lanes (`P.BIN`), cyan = lane
branches, dots = sprites (dark green = solid scenery, orange = knock-over, magenta/red ring = "go
back to the main road" / "wrong way" triggers, white = markers), white circle = player start,
red circles = opponents, pink circles = other moving objects, red box = finish cell, blue/green/
orange boxes = the three route-identification tiles, red/purple outline = event surfaces.

## Overview (Verified)

* A **scene** (SCENE01 "Pacific – Yosemite", SCENE02) has **5 legs**. Each leg is its own
  separate map. The legs are not parts of one big world: every leg map covers the same
  coordinate range and starts at one side and ends at a gas station on the other side.
  SCENE01 legs run west → east; SCENE02 legs run east → west.
* Leg map = **32 × 16 cells** (`DS:9671`), each cell = tile type + quarter-turn rotation +
  height. A tile type is a polygon model (`T.BIN` or `SCENETTT.BIN`) spanning exactly one cell,
  with its road surfaces, its sprite placements (trees, rocks, signs …) and, in `P.BIN`, its traffic
  lanes. Roads connect because tiles line up at the cell edges (lanes start/end at ±480 on an
  edge, see objects.md). There is no separate road graph: the network is the tiles plus the lanes.
* Extra objects (signs, houses, boats, traffic, train, opponents, player start) are in the leg's
  **object list** (`DS:A473..`), with absolute positions.
* The leg ends when the car is on a face of **surface type 1Fh**. Each leg has exactly one such
  cell: the gas-station tile (59h in SCENE01, 51h in SCENE02).

### Coordinates (Verified)

| Quantity | Unit / range |
|---|---|
| World X (east), Z (north) | u16 position units; X 0..7FFFh, Z 0..3FFFh (object list `A5B9`/`A6F9`, car `DS:949A`/`949C`) |
| Cell | 400h × 400h position units; column = `X >> 10` (0..31), row = `15 − (Z >> 10)` (row 0 = north edge) |
| Cell centre | X = col·400h + 200h, Z = (15 − row)·400h + 200h (`0e12:70cd`: `BD2A = ((X_hi & 7Ch) + 2) << 8`; `5466` same for lanes) |
| Cell index | `(X_hi >> 2) + (15 − (Z_hi >> 2)) · 32`, word at `DS:9671 + 2·index`, `& 1FFh` wraps (`0e12:70cd`, `5aa4`) |
| Model X/Z (tiles, objects) | ¼ position unit: added to `BD30 = BD2A << 2` (`0e12:75c1`); a tile spans ±800h model units = ±200h position units |
| Sprite children, lanes | position units (added to `BD2A`/`BD2C` directly, `72f8`, `5466`) |
| Height Y | model Y + `BD2E`; cell height `(cell_hi & 3Fh) << 8`; object height `A839 >> 3`; lane height `(h + (cell & 3F00h)) · 8` → `DS:9462` |
| Rotation | cell bits 14–15 / object heading hi byte bits 6–7 = quarter turns r: r=1 maps (x, z) → (z, −x), r=2 → (−x, −z), r=3 → (−z, x) (`0e12:7653`, `72f8` at `7442`, `5466`) |
| Edge directions | 0 = north (+Z, cell index −32), 1 = east (+X, +1), 2 = south (−Z, +32), 3 = west (−X, −1); a lane's edge number is rotated by adding r (`0e12:5466`) |

## Loading (Verified, `0792:0d2e` → `0792:0fce`, `01f4:53c4`)

| Buffer | File | Loader |
|---|---|---|
| near `DS:2500` (10455 / 12467 bytes) | `<scene><d>.DAT`, d = char `DS:0B0C[leg]` (all legs '1' in both scenes: the `.LST` holds "11111111") | `0792:0d2e` (`0000:0e74`) |
| near `DS:9592` (8504 bytes, to `DS:B6C9`) | `<scene><'A'+leg>.DAT`, leg = `DS:0B0A` | `0792:0fce` |
| far `DS:E770` | `<scene>T.BIN` (tile types 40h–7Fh) | `0792:0d2e` |
| far `DS:E7E0` | `SCENETTT.BIN` (tile types 00h–3Fh, shared by all scenes) | `01f4:53c4` once |
| far `DS:E54C`, `DS:CE9E` | `<scene>O.BIN`, `<scene>P.BIN` when `DS:0B15` = 0, else the shared `SCENETTO/P.BIN` from `01f4:53c4` | both |

* After loading, `0e12:2789` builds pre-scaled copies of every sprite from `DS:2500` into the far
  buffer `DS:E53C` (D010h bytes; "BARF! Bad Course Data! REBOOT!", message 1Ah, on overflow), then
  `0e12:255e` seeds the random word (`DS:B6ED`/`DS:BA56`) and resets per-leg state; it copies
  the player's start X/Z `DS:9CF3`/`DS:9F73` into `DS:949A`/`DS:949C`.
* Demo/attract (`01f4:53c4`): `SCENETTA.DAT` → `DS:9592`, `SCENETT1.DAT` → `DS:2500` (byte-identical
  to `SCENE011.DAT`). The demo map uses only tile types < 40h, because `DS:E770` holds front-end
  pictures at that time.
* `SCENE01O/P.BIN` are 7-byte `TJL 90\xFF` placeholders, so SCENE01 uses `SCENETTP.BIN`. Its
  types 40h–7Ah are SCENE01's tiles. SCENE02 ships its own `O.BIN` and `P.BIN`. The tool uses the
  scene file when it is real and the SCENETT file otherwise.

## Leg map file `<scene>A..E.DAT` (8504 bytes = memory image of `DS:9592..B6C9`)

The file is copied as-is over a block of globals: leg parameters, the map, the initial object list
and the VGA colour tables. Much of it is runtime state that the file only initialises. File offset
= DS address − 9592h.

| File off | DS | Size | Field | Readers | Tag |
|---|---|---|---|---|---|
| 000 | 9592 | u16 | 0 | – | Unknown |
| 002 | 9594 | u16 | 258h (600) in all legs; simulation constant | `0977:0008` | Unknown |
| 004 | 9596 | u8 | sound-related flag (tested with event 14h) | `0c1c:111d` | Unknown |
| 005 | 9597 | u8[2][10] | behaviour script of the two opponent cars (object models 2, 3): at the end of each lane the next entry becomes the object's behaviour type (flags bits 8–10), i.e. which branch it takes | `0e12:5466` (`-6A69h`) | Likely |
| 019 | 95AB | u16 | → `DS:E817/E818` (sky/horizon) | `0e12:61fd` | Unknown |
| 01B | 95AD | u16 | → `DS:E832/E833` | `0e12:61fd` | Unknown |
| 01D | 95AF | u16[4] | traffic speeds by object flag bits 6–7 = {18h, 1Eh, 37h, 78h} | `0e12:5466` (`-6A51h`) | Likely |
| 025 | 95B7 | s16 | X drift per frame of sprites with id bit 80h (clouds, birds); sign follows the car | `0e12:3352`, `76ec` | Likely |
| 027 | 95B9 | s16 | Z drift, sprite id bit 40h | same | Likely |
| 029 | 95BB | u16 | leg reference time in seconds (510, 888, 486, 736, 624 for legs A–E in both scenes; demo 1200); caps the time used for scoring | `0000:0000`, `01f4:55f0` | Likely |
| 02B | 95BD | u8 | low 3 bits: some cyclic state | `0e12:6e92` | Unknown |
| 02C | 95BE | u8 | | `fn_0edde` | Unknown |
| 02D | 95BF | u8 | number of sprite scale steps pre-built (11h/10h/0Eh) | `0e12:2789`, `2810` | Verified |
| 02E | 95C0 | u8[4] | sky/horizon colours: day pair (C0, C2), alternate pair (C1, C3) selected by `95C8` | `0e12:373c`, toggled by debug key `fn_0e787` | Likely |
| 032 | 95C4 | u8 | | `0e12:373c` | Unknown |
| 035 | 95C7 | u8 | colour mode: 0 → face codes 8→0, 7→8 (darker road/greys; 5 of the 10 legs), also darkens `B4B9` by 202h in `0792:0fce` | `0e12:261c`, `0792:0fce`, render | Verified (effect) / Likely (dusk/night) |
| 036 | 95C8 | u8 | selects the alternate sky pair; set by weather ramps | `0e12:373c`, `0792:000c` | Likely |
| 037 | 95C9 | u8[3] | **route-identification tile types**: when the car's cell has one of these types, `DS:0A74` = 0/1/2 = which of the leg's three alternative routes it is on (names in the `.LST`; shown by `01f4:4954`) | `0e12:70cd` (`7150`) | Verified (code) |
| 03A | 95CC | u8 | turnaround delay reload for traffic reaching a dead end | `0e12:5466` | Likely |
| 03B | 95CD | u16 | sprite distance limit | `0e12:316c` | Unknown |
| 03D | 95CF | u16 | sprite collision distance | `0e12:33f7` | Likely |
| 03F | 95D1 | u16 | | `0e12:5ad4` (vehicle collision) | Unknown |
| 041 | 95D3 | u16 | clearance for surface types 14h–17h (bridges / overpasses) | `0e12:3a7c` (`3d52`) | Likely |
| 043 | 95D5 | u8 | rotation speed of the O-model 0Fh object (lighthouse lamp) | `0e12:5df9` | Likely |
| 044 | 95D6 | u8 | railway-crossing gate speed (object 10h) | `0e12:5f57` | Likely |
| 045 | 95D7 | u8 | | `0977:0008` | Unknown |
| 047 | 95D9 | u8[4] | point sizes {13, 2, 9, 8} (see objects.md) | `0e12:66ad`, `81c7` | Verified |
| 04F | 95E1 | u8[32] | per-sprite flags, `& 7`: 0 = own scaled copies, 1 = …, ≥2 = shares the previous sprite's copies | `0e12:2810`, `28eb`, `2e49` | Likely |
| 06F | 9601 | 16 | | – | Unknown |
| 07F | 9611 | u16[48] | **sprite animation chain**: for sprite id i, low 6 bits = next id, bits 8–12 = frames until the next switch (e.g. 09h ↔ 1Eh flashing) | `0e12:3352`–`33aa` | Verified (code) |
| 0DF | 9671 | u16[512] | **cell map**, 32 columns × 16 rows, row 0 = north: bits 0–7 tile type, 8–13 height (·100h), 14–15 rotation | `0e12:70cd`, `5aa4`, `5466` | Verified |
| 4DF | 9A71 | u16 | sprite instance count | `0e12:3085`, `72f8` | runtime |
| 4E1 | 9A73 | u16[320] | sprite instances: id (see Sprites); file content is leftover | `72f8`, `323e`, `33f7` … | runtime |
| 761 / 9E1 / C61 | 9CF3 / 9F73 / A1F3 | s16[320] | sprite instance X / Z / Y; entry 0 = player position | same, `0e12:255e` | runtime |
| EE1 | A473 | u16 | number of objects n | `0e12:70cd`, `4631` … | Verified |
| EE3 | A475 | u16 | end of the moving-object range scanned by the train / crossing code | `0e12:5e70`, `509b`, `5ad4` … | Likely |
| EE5 | A477 | u16 | first static object: 0..A477−1 move (vehicles, drawn by `52a3`/`539d`), A477..n−1 are static (drawn through `72f8` with AH=1) | `0e12:70cd` (`727f`) | Verified |
| EE7 | A479 | u16[160] | object flags: bits 0–5 model (0/1 player .POB, 2/3 opponent .POB, ≥4 `O.BIN`), 6–7 speed class (`95AF`), 8–11 behaviour type (0/1/9 = turn round at dead ends, 8 = fixed speed, 2–7 branch choice), 12 = 1000h, 13 = 2000h moving vehicle, 14 = 4000h police/obeys signals, 15 = 8000h stopped | `0e12:5466`, `33f7`, `5e70` | Likely |
| 1027 | A5B9 | s16[160] | object X (position units) | many | Verified |
| 1167 | A6F9 | s16[160] | object Z | many | Verified |
| 12A7 | A839 | u16[160] | object height × 8 | `52a3`, `539d`, `70cd` | Verified |
| 13E7 | A979 | u16[160] | heading: hi byte = current (bits 6–7 used for static objects), lo byte = target | `5466`, `52a3`, `70cd` | Likely |
| 1527 | AAB9 | u16[160] | hi byte → `DS:BD38` height shear (pitch), lo = runtime | `52a3`, `539d` | Likely |
| 1667.. | ABF9, AD39, AE79, AFB9, B0F9, B239, B379 | u16[160] each | runtime per object: speed, current cell pointer, waypoint index/timer, vertex/face ranges | `5466`, `75c1`, `7487` | runtime |
| 1F27 | B4B9 | u16[256] | VGA colour pair table (objects.md) | `0e12:7487` | Verified |
| 2127 | B6B9 | u8[16] | direct colours for codes ≥ 16 | `0e12:7487` | Verified |
| 2137 | B6C9 | u8 | last byte | – | Unknown |

Object list, SCENE01 leg A (from `dump`): 0 = player start (flags 2001h, model 1 = player car,
X 2080 Z 4576 at the west edge), 1–2 = opponents (62C2h/63C3h, models 2/3), 3–44 = traffic
(27xxh/22xxh, O models 2Eh–30h, 37h, 11h = police), 36–41 = a train (12h–14h at the same
point), 45–51 = boats etc. (10xxh), 52.. static: 0Fh lighthouse, 25h, 3Ah–3Dh route shields and
signs, 10h crossing gate, 05h traffic light, 06h stop sign, 2Ch/28h …

### Per leg (Verified, `tools/td3world.py`)

| Leg | Start (X, Z) → cell | Finish cell (col,row) | Route tiles 95C9..CB | Ref. time | 95C7 |
|---|---|---|---|---|---|
| SCENE01 A | 2080, 4576 → (2,11) | (30,2) tile 59h | 70h 22h 51h | 510 | 0 |
| SCENE01 B | 1056, 14816 → (1,1) | (29,15) | 37h 22h 1Bh | 888 | 1 |
| SCENE01 C | 32, 2528 → (0,13) | (29,1) | 3Fh 37h 32h | 486 | 1 |
| SCENE01 D | 1056, 2528 → (1,13) | (30,4) | 75h 35h 6Dh | 736 | 0 |
| SCENE01 E | 2080, 11744 → (2,4) | (27,2) | 70h 22h 39h | 624 | 0 |
| SCENE02 A | 28448, 14816 → (27,1) | (2,1) tile 51h | 2Dh 28h 20h | 510 | 1 |
| SCENE02 B | 31520, 1568 → (30,14) | (1,1) | 7Bh 5Fh 55h | 888 | 0 |
| SCENE02 C | 31264, 32 → (30,15) | (1,0) | 69h 35h 68h | 486 | 0 |
| SCENE02 D | 31264, 6176 → (30,9) | (0,4) | 5Eh 7Eh 1Bh | 736 | 1 |
| SCENE02 E | 32288, 4128 → (31,11) | (1,8) | 0Dh 16h 4Ah | 624 | 1 |
| SCENETT A (demo) | 2592, 2560 → (2,13) | none | 21h 30h 1Bh | 1200 | 1 |

The start is object 0; its X/Z are also sprite instance 0 (`9CF3/9F73[0]`), which `0e12:255e`
copies to the car position `949A/949C`. The checkpoint between legs is the gas station reached
at the finish cell; there is no other checkpoint data.

## World assembly per frame (Verified, `0e12:70cd` → `0e12:72f8`)

1. Cell of the car: `(X_hi >> 2) + (15 − (Z_hi >> 2))·32`; recomputed only when the cell or the
   heading octant changes (`DS:BD34`/`BD36`). Visible cells are taken from a CS table at
   `0e12:7068` (per heading octant, 3/6/10 entries by detail and tile class: types 70h–73h and
   ≥ 77h get the long view) of (dX, dZ, …) offsets at `0e12:7004`.
2. Per visible cell: `BD2A/BD2C` = cell centre, `BD2E` = height, `946A` = rotation, and
   `72f8(AL = tile type, AH = 0)`: type < 40h → `SCENETTT` (`E7E0`), else `T.BIN[type − 40h]`
   (`E770`); table entries ≤ 10h alias the previous model with fewer faces (`BD24`).
   Vertices → `75c1`/`7653`, faces → `7487`, then the model's **sprite children** go into the
   sprite instance list (`9A73`/`9CF3`/`9F73`/`A1F3`), rotated by the quarter turn. Ids with low
   byte ≥ 12h are kept only if the next bit of `BA56` rotated by `(BD2A+BD2C) >> 12` is 1, so
   vegetation density varies from cell to cell but is fixed for a given cell.
3. Static objects `A477..A473−1` whose cell matches (`X_hi & 7Ch`, `Z_hi & 3Ch`) are added with
   `72f8(AH = 1, AL = model)` from `O.BIN`, positioned at their own X/Z/Y and heading.

## Surface types (w3 >> 11 of a face) used by the world (Verified in code, meanings Likely)

`0e12:3a7c` (`3d28`–`3da4`) keeps the face under/around the car: `DS:BA9A = w3_hi & F8h`,
`dl = BA9A >> 3`:

| Type | Effect | Occurs on |
|---|---|---|
| 0 | none | |
| 1 | solid: crash (`BAA8`/`BAAE`) unless slow (`|DS:1296| < 14h` → `0e12:0fd6`) | walls, rails |
| 2–0Dh | not a ground surface (lamps etc., objects.md) | |
| 0Eh–13h | ground type → `DS:BA9B`, used by the simulation `0977:0008`: **0Eh water** (blue), **0Fh terrain** (grass/dirt), **10h road** asphalt + white markings, **11h** verge/shoulder, 12h dark (0,0) | tiles |
| 14h–17h | raised ground, only when within the clearance `95D3` (bridge decks) | |
| 18h–1Bh | not ground (1Ah = large 6/12 checker faces) | |
| 1Ch | zone: `0792:000c` sets `DS:E540` | SCENE02 tiles 75h, 61h |
| 1Dh / 1Eh | zones: ramp weather effect `DS:BC75` / `DS:BC77` up or down (`BC77` turns codes 2, 3, 0Ah grey/white = snow; both slow traffic) | none in shipped legs |
| **1Fh** | **leg finish**: `0792:000c` sets `DS:008A = 3` | gas-station tile 59h (SCENE01) / 51h (SCENE02), 8–10 faces |

Types ≥ 1Ch stick for the frame (`cmp [BA9B],1Ch; jge`).

## Lanes (`P.BIN`) as the road graph (Verified by rendering)

Record format: objects.md. How the world uses it (`0e12:5466`, `5aa4`):
* A moving object keeps `AD39[i]` = pointer to its map cell and `AE79[i]` lo = waypoint index.
  `5aa4` looks up `P[type of cell]` and stores the rotation in `946A`.
* Waypoint flags `C0h|e` = lane enters from edge e, `80h|e` = lane leaves by edge e. At an exit,
  the object moves to the neighbour cell in direction `(e + r) & 3` and searches that cell's list
  for the entry `C0h | ((e + r) ^ 2) − r'` (the opposite edge in the new cell's frame).
  Junctions are waypoints whose low 6 bits (or high-byte low 6 bits) hold a signed offset to
  another waypoint of the same list. `DS:946E` (from the behaviour type, random `DS:00D4` for
  types ≥5) chooses whether to take it.
* 4000h/0040h flags: stop at traffic lights / signs (`DS:BCCF`, `BCCE`).
* Target point: `X = lane_x' + col·400h + 200h`, `Z = lane_z' + (15−row)·400h + 200h` (x', z'
  rotated), `Y = (lane_h + (cell & 3F00h))·8`.

The rendered lanes form a continuous network across all cells in all legs, including dead ends
with turnaround loops, the oval loops, the four-lane highways and the railway (train objects
follow lanes too).

## Sprites `<scene><d>.DAT` → `DS:2500` (Verified by rendering `<scene>_sprites.png`)

| Off | Size | Field |
|---|---|---|
| 0 | u16 | 0 |
| 2 | u16 | start of pixel data (690h) — bytes in [this, remap) are remapped for non-VGA modes |
| 4 | u16 | offset of a remap table (bytes ≥ 10h → `table[b − 10h]`, EGA/Tandy only; `0e12:276e`, skipped when `DS:E338` = 13h) |
| 6 | u16 | n = number of records including this header (20h → sprites 1..31) |
| 8·i | 8 B | sprite i: `u8 w` max row width, `u8 h` rows, `u16 rowptr` → h words = offsets of each row's pixels, `u16 rowlen` → h words, low 6 bits = pixel count m of the row (bits 6–7 = 1, Unknown), `u16` = w again |

All offsets are from the file start (= `DS:2500`). Each row has m pixels (VGA palette indices,
0–15 base colours, 16–127 `OTWCOL.BIN`) **centred** in the width: (w − m)/2 transparent pixels on
each side (`0e12:2f84`). Rows run top to bottom. Records `2 1 100h 102h` are empty 2×1
placeholders. SCENE01 set: 2 aeroplane, 4/5 moon/sun, 9/1Eh/1Fh animals, 0Bh–0Dh clouds/rocks,
12h pine, 15h bush, 18h/1Bh trees, small ones = lights/markers. `0e12:2789`/`2810`/`289a`/`2d54`
pre-scale each sprite into `95BF` sizes (`DS:BA0E` size table), in the far buffer `E53C`.

**Sprite instance id** (tile children `u16 id`, `9A73[]`): bits 0–5 sprite (`95E1`/`9611` index),
bit 6 / bit 7 = drifts with `95B9` / `95B7`, bits 8–12 = animation countdown, bits 13–15 =
collision class (`0e12:33f7`):

| Class (id hi & E0h) | On contact | Tag |
|---|---|---|
| 00h | none (markers, road posts, ids < 12h are always placed) | Verified |
| 20h, 40h | knocked over: sound, `DS:947C` = 5, id \|= 1 (id < 21h → becomes 1Fh) | Likely |
| 60h | solid: crash (trees, rocks: ids 6012h–601Ch in tiles) | Likely |
| 80h | removed, hits the windscreen (`0e12:47fc`, screen splat) | Likely |
| A0h | removed, message 0Dh "Go back to the main road!" | Verified (strings) |
| C0h | removed, message 18h "WRONG WAY!" | Verified (strings) |
| E0h | removed + sound | Likely |

## Open questions

* Header bytes marked Unknown (`9594`, `9596`, `95AB`/`95AD`, `95BD`, `95BE`, `95C4`, `95CD`,
  `95D1`, `95D7`, `9601..9610`) — owned by the simulation/render specs.
* Ground types 11h/12h and 1Ch (`DS:E540`) exact meaning; 1Dh/1Eh weather zones are coded but unused in the shipped legs.
* Tile 67h of SCENE02T is an empty model (0 faces, 40 cells): the game draws nothing there (black
  in the PNGs); what the player sees there (background ground fill?) is up to the render spec.
* Behaviour types 2–7 of moving objects and the exact branch choice (`946E`) are only sketched.
* `A475` meaning (≠ A477) not pinned down.
* Sprite `rowlen` bits 6–7, header byte `+7` (= width) purpose.
