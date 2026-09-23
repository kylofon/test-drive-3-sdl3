# 3D objects: `SCENETTO.BIN` (O), `SCENETTP.BIN` (P), `C<car>.POB`, and the object part of `T.BIN`

Addresses as in `port/RE_GUIDE.md` (`SSSS:OOOO` file segments, `DS:xxxx` = DGROUP 1BE4). Tags:
**Verified** = parsed, rendered (`tools/td3obj.py` → `work/obj/`) and the pictures look right, or
read directly in the code; **Likely** = consistent with code and data but not fully traced;
**Unknown**.

Tool: `python tools/td3obj.py` renders every object to `work/obj/` (`SCENETTO_sheet.png`,
`POB_sheet.png`, `O/Onn.png`, `C<car>.png`, and the road tiles `SCENETTT/SCENE01T/SCENE02T_sheet.png`).
`dump FILE [IDX]`, `stats` and `paths` print the parsed data.

## Where the sets live (Verified, `0792:0d2e`, `01f4:53c4`)

| Buffer | File | Contents | Readers |
|---|---|---|---|
| far `DS:E54C` | `<scene>O.BIN`, else `SCENETTO.BIN` | **O**bjects: roadside objects + traffic vehicles, 64-entry table | `0e12:72f8` (static), `539d`, `52a3`, `5ad4`, `5cf2`, `4db6` (vehicles) |
| far `DS:CE9E` | `<scene>P.BIN`, else `SCENETTP.BIN` | traffic lane **P**aths per road-tile type | `0e12:5aa4`, `5466` |
| far `DS:E7E0` | `SCENETTT.BIN` | shared road tiles, map tile types 00–3F | `0e12:72f8` |
| far `DS:E770` | `<scene>T.BIN` | the scene's road tiles, map tile types 40h–7Fh | `0e12:72f8` |
| far `DS:CEA4` | selected car `C<car>.POB` | player car (object types 0, 1) | vehicle readers above |
| near `DS:CEBC`, `DS:D7A4` (0x8E8 bytes each) | opponents' `.POB` (`DS:CE9C`/`CEA2` pick the car from `PLAYDISK` names at `DS:09E0`) | object types 2 and 3 | same |

* `<scene>O.BIN` / `P.BIN` are loaded only when `DS:0B15` = 0; in both shipped scenes they are the
  7-byte `TJL 90\xFF` placeholders and the flag makes the game use the `SCENETT` sets (loaded by
  `01f4:53c4` together with `SCENETTT.BIN`, `SCENETTA.DAT` → `DS:9592`, `SCENETT1.DAT` → `DS:2500`).
* The object type of a placed/moving object is `A477[i] & 3Fh`: **0,1** → `CEA4` (player car),
  **2** → `CEBC`, **3** → `D7A4`, **≥4** → O table. Entries 0–4 of `SCENETTO` all point to the
  "43" route shield at 0080h (never used as types 0–3; type 4 Unknown/unused).

## Object table (Verified)

`u16 offset[n]` from the start of the file, then the objects. `n` is not stored: 64 for O and
`SCENETTT`, and the table ends where the first object begins (`SCENE01T` = 59 entries).
In the T sets an entry **< 11h** is an alias (`0e12:72f8`): use the previous entry's object and
draw `entry` fewer faces (`DS:BD24`, subtracted from the face count in `0e12:7487`).
`SCENETTO.BIN` ends with one extra `FFh` byte.

## Object layout

Two header forms. The kind is **not stored**: the game knows it from how the object is used
(`72f8` → 4-byte, vehicle code → 8-byte). The tool picks the one whose size reaches the next
object exactly (unambiguous for all 64+59+64 objects and the 5 POBs).

### Header, static object / road tile (4 bytes, `0e12:72f8`) — Verified

| Off | Size | Field |
|---|---|---|
| 0 | u8 | `nf` faces |
| 1 | u8 | `nv` vertices (count transformed by `0e12:75c1` into `DS:946B`) |
| 2 | u8 | `nc` sprite children (road tiles only; 0 in O) |
| 3 | u8 | 0 in all files — Unknown |

### Header, vehicle (8 bytes: POB, O entries 17–21, 46–48, 51–53, 55–57) — Verified

| Off | Size | Field | Code |
|---|---|---|---|
| 0 | u8 | `nf` faces (near) | `539d` `mov ch,[si]` |
| 1 | u8 | `nv` vertices (near) | `539d` |
| 2 | u8 | `nc` = 0 (child list, kept in the skip arithmetic) | `5cf2`, `5ad4`, `4db6` |
| 3 | u8 | `nk` collision boxes | `5cf2` / `5ad4` loop count |
| 4 | u8 | far-LOD `nf` | `539d` / `52a3` when `DX > 200h` (distance) |
| 5 | u8 | far-LOD `nv` | same |
| 6 | u16 | far-LOD data offset from byte 8. **0** = reuse the near arrays (first `nv`/`nf` of them) | `539d`: `si = obj+6; si += [si]; si += 2` |

### Body (both forms)

```
hdr | A[nv] | B[nv] | C[nv] | face[nf] (8 B) | child[nc] (8 B) | coll[nk] (8 B) | anim[8] (12 B) | far LOD: A/B/C[nv'] face[nf']
                                                ^ 4-byte form stops here           ^ vehicles only
```

**Vertices** (Verified, `0e12:7653`: `dx=[si]`, `ax=[si+bx]`, `cx=[si+2bx]`, `bx = 2·nv`):
three s16 arrays.
* `A` = height (up). Added to `DS:BD2E` (= object altitude `A839>>3`, or tile height `(map & 3F00h)`) → `DS:2502[]`.
* `B`, `C` = horizontal, rotated by the heading `DS:946A` (low 6 bits via the sine/cosine tables at
  `DS:B851`/`DS:B7CF`, top 2 bits = quarter turn), then added to `DS:BD30 = A5B9<<2` → `DS:3182[]`
  and `DS:BD32 = A6F9<<2` → `DS:3E02[]`. So object units are ¼ of an object-position unit;
  a road tile spans ±512 (tile = 1024 object units = 256 position units). `DS:BD38` adds a
  height-proportional shear to `A` (pitch on slopes).
* On vehicles `+B` is forward (headlight beams point to +B), `C` across. Cars ≈ 490 long
  (B −140..352 incl. beam; body −140..112), 145 wide, 56 high.

**Faces** (Verified by rendering; fields from `0e12:7487`, `361c`, `323e`, `3a7c`, `66ad`, `41d4`):
four u16 words `w0..w3`, each with a vertex index in bits 0–10 (relative to the object; `7487`
adds the running vertex base `DS:945E`).

| Bits | Field |
|---|---|
| `w0` 14–15 | vertex count − 1: 0 point (`3bba`→`66ad`), 1 line (`3c0c`), 2 triangle (`3a91`), 3 quad (`3b0e`); dispatch `0e12:3a7c` |
| `w0` 13 | polygons/lines: sort depth = farthest vertex instead of the average (`361c`); points: one-sided lamp (`66ad` `test bp,2000h`) — Likely |
| `w0` 11–12 | point size index into `DS:95D9` = {13, 2, 9, 8} (`66ad`, scaled by distance); on lines/polygons Unknown (line width? set on some sign posts) |
| `w1` 11–15 | colour code `c1` (even pixels) |
| `w2` 11–15 | colour code `c2` (odd pixels) |
| `w3` 11–15 | face type (`DS:BA9A = w3>>8 & F8h`); vertex index in bits 0–10 unused for points/lines/triangles (often 4) |

Face order is the draw list; the game depth-sorts faces of all objects (`0e12:361c` computes the
sort key from `DS:6382` depths, `7b06`/`7a18` sort). No back-face culling found — Likely (painter only).

**Colour** (Verified in code; `0e12:7487`, VGA branch `DS:E338 = 13h`): each code goes through the
32-byte remap `DS:E5B0` (built by `0e12:261c`: identity when `DS:95C7` ≠ 0; with `95C7` = 0 it maps
8→0 and 7→8, and `0792:0fce` darkens `B4B9` by 202h — Likely `95C7` = daylight; `DS:BC75`/`BC77`
(Likely weather: they also start the wipers) remap codes 2, 3, 10 and 8), then
* both codes < 16: word `DS:B4B9[(c2<<4) | c1]`;
* a code ≥ 16: `DS:B6B9[code & 15]` for that byte.

Result = two palette indices (low byte / high byte). The span filler `0e12:41d4` writes the word
with the bytes swapped on alternate rows → a checkerboard dither of the two colours.
`DS:B4B9` (256 words) and `DS:B6B9` (16 bytes) are **not** in the executable's image as used:
they are part of the leg's `<scene>A..E.DAT` loaded to `DS:9592` (file offsets **1F27h** and
**2127h**; the exe copy is only the EGA identity table `0792:0fce` builds). In `SCENE01A.DAT`
same-code pairs map to pixels 0–15 (palette 0–15 from the executable = EGA colours), mixed pairs to
smooth VGA shades 50–105, and `B6B9` codes 16–31 to pixels 12h..7Eh = `OTWCOL.BIN` colours.
Car models use only these tables too (no pixels ≥ 80h), so the car's `COL.BIN` at +80h is not needed.

**Face types** (`w3 >> 11`; runtime colour overrides in `0e12:4db6`, which scans an object's
records until a type < 4 or > 13) — Likely unless noted:

| Type (`BA9A`) | Use (from code + where it occurs) |
|---|---|
| 0 (00h) | drawn by OR-ing into the frame (`41d4`: `cmp [BA9A],bh`); colour 707h skipped by day → **headlight beams** on cars (Verified in data: quad from the headlights to B=352 on the ground) |
| 1–3, 14–31 | ordinary faces; 1–2 also tested by `34f1` (camera near-hit, type 1 → `0e12:0fd6`); 1Ah/1Bh → `4590`/`4631` (sound via `0c1c:111d`). Road tiles use 14–17, 1Ah — meaning Unknown |
| 4 (20h), 5 (28h), 6 (30h) | lamps lit depending on viewing side: 4 = red one way / white the other (tail/head light), 5 = white/off, 6 = yellow/off |
| 7 (38h) | blinks white with `DS:00D2 & 4` |
| 8 (40h) | strobe: white when `DS:BCC1 & 7 == 0` |
| 9 (48h) | brake light: 404h, 0C0Ch when the object flag 8000h is set (Verified: rear lamps on every car) |
| 10 (50h) | siren/beacon: cycles `DS:BCB3` 9,12,15 when object flag 4000h (police) |
| 11 (58h) | traffic-light lamp: green 0A0Ah / yellow 0E0Eh / red 0C0Ch / off, by lamp counter `DS:BCD0` and phase `DS:BCCF` (Verified: 3 points on O 5) |
| 12 (60h) | flasher 808h / 0C0Ch |
| 13 (68h) | vertex animation: vertices 0 and 1 take frame `DS:BCD1 & 7` (frame 0 unless `BC75`/`BC77`) — the **windscreen wipers** (two lines on every POB) |

**Children** (4-byte form, road tiles; Verified layout, `0e12:72f8`): `u16 id, s16 x, s16 z, s16 h`,
x/z rotated only by quarter turns, h added to the tile height. `id` goes to `DS:9A73[]`; ids with
low byte < 12h are always placed, others only when the next bit of the rotating seed `DS:BA56` is
set. They are drawn as scaled **sprites** by `0e12:28eb` (trees, rocks …), not as polygon objects.
Sprite ids/format: see the world/render specs (Unknown here).

**Collision boxes** (vehicles; `0e12:5cf2`, `5ad4`, Likely): `s16 A, B, C` (rotated like vertices,
but added to the position **without** `<<2`, i.e. in position units) + `u8 r`, `u8 h`. The test
compares an octagonal distance ((max+min/2)/2-ish) against `r` and height difference against `h`
at scales ×1 / ×8 / ×16 (bits 7/6/4 of `DS:946C`: hit / near / close). POBs have 2 boxes
(front, rear), traffic 2–4.

**Animation frames** (vehicles; `0e12:4db6`, Verified layout): 8 × 12 bytes =
`A0, A1, B0, B1, C0, C1` for vertices 0 and 1; present when the gap before the LOD is 96 bytes
(POBs, O 17/46/47/48/55 — the cars with wipers). Frames 0..7 sweep 0→4→0.

**Far LOD** (vehicles, Verified): used when `DX > 200h` in `539d`/`52a3`; own vertex and face
arrays at `obj + 8 + off`, same encoding, no children/collisions. O 21/56/57 use offset 0 (same data).

### Rendered content (Verified by eye, `work/obj/SCENETTO_sheet.png`)

| O index | Object |
|---|---|
| 0–4, 13, 14, 61–63 | route shields (43, 33, 150, 101, 221, 140) |
| 5 | traffic light (3 point lamps, face type 11) |
| 6 | stop sign |
| 7, 11 / 9 | diamond sign, edge-on and flat plate variants |
| 8, 28, 42, 60 | white regulatory / speed signs ("22", "2" …) |
| 10, 12, 22–26, 31, 32, 50 | yellow warning signs (curves, junctions, "410") |
| 15 | lighthouse with keeper's house |
| 16 | pole with arm and lamps (railway-crossing gate?) |
| 27 | lattice pylon (lines only) |
| 29 / 30 | motor boat / sailing boat on water |
| 33–36 | flat dark plates (patches/shadows? Unknown) |
| 37 | star-shaped splat (Unknown) |
| 38, 39 | small box on a post / crate |
| 40, 41 | red cone, red flag on black base |
| 43 | person |
| 44, 49 | billboards |
| 45 | zebra crossing |
| 54, 58, 59 | house, deer, tree |
| 8-byte: 17 | police car (beacons type 10, brake lights, wipers) |
| 8-byte: 46, 47, 55 | white car, blue sedan, station wagon |
| 8-byte: 18, 19, 20 | long vehicle (bus/truck), box truck, flatbed trailer |
| 8-byte: 21 | thin vertical object, 3 faces (Unknown) |
| 8-byte: 51, 52, 53 | barn/hangar, two houses (use the far LOD like vehicles) |
| 8-byte: 56, 57 | seagull, two wing poses |


## `SCENETTP.BIN` — traffic lane paths (Likely, `0e12:5aa4`, `0e12:5466`)

`u16 offset[123]` (one per road-tile type = map word low byte, `DS:9671[]`), then per tile:
`u8 count` (bit 7 = flag copied to `DS:BCB8`, low 7 bits = number of waypoints) and `count ×`
8-byte waypoints:

| Off | Size | Field |
|---|---|---|
| 0 | u16 | flags: `C0h|e` lane starts at tile edge `e` (0–3), `80h|e` lane ends at edge `e`; `40h`/`4000h` = stop/yield conditions (checked against `DS:BCCF`, traffic lights); low 6 bits of either byte ≠ 0 = signed jump to another waypoint of the list (branch), chosen by `DS:946E` |
| 2 | s16 | height; `(h + (map & 3F00h)) * 8` → target altitude `DS:9462` |
| 4 | s16 | x, rotated by the tile heading, + tile centre |
| 6 | s16 | z |

Lanes sit at ±32 (and ±96 on 4-lane roads), edges at ±480. Tile types without traffic point to a
1-byte `00` list (0F6h).

## Open questions

* Meaning of face types 1–3 and 14–31 beyond the collision/sound tests; of `w0` bits 11–12 on
  polygons/lines; of 4-byte header byte 3.
* Red/cyan checker patches on road tiles: code pairs whose `B4B9` entry is still the identity →
  probably a runtime-remapped colour (fields?) or genuinely dithered EGA red/cyan. Check in DOSBox.
* Palette 0–15 in game: assumed the executable's EGA colours (`DS:0B6A`); not checked whether a
  leg `.COL` or time of day changes them.
* The exact size/distance math of points (`66ad`, table at `0e12:12f6`) and of the collision test.
* O type 4 and entries 0–4; O 21 (tall thin 8-byte object) identity.
