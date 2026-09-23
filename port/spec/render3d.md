# render3d — Test Drive III, TDIII.EXE, segment `0e12` (3D engine) and its presentation

Porting spec for the 3D view: camera, world assembly, the angular projection and its lookup tables, face
sorting, sky/ground, the polygon/line/point rasterizers with the two-colour checkerboard dither, face types,
sprites (trees, signs, sun/moon, clouds), the rear-view mirror (a second 3D pass), weather/wiper/dashboard
overlays, and how the RAM view buffer reaches the VGA screen. Conventions follow `port/RE_GUIDE.md`
(`SSSS:OOOO`, `DS:xxxx` in DGROUP 1BE4). File formats are cited from `port/formats/objects.md` and
`port/formats/world.md` (not re-derived).

Everything was read from the disassembly (`tools/x86dis.py`); the engine is hand-written assembly with register
interfaces, so the Ghidra output is only a guide. Confidence is **verified** unless a table row says otherwise.
Where the simulation spec owns a routine (`0977`, `0ab4:1220`, key handlers, traffic `5466/5aa4`, police
physics) it is only referenced.

**Video mode.** The VGA build runs library mode **13h** (config video 0 → `DS:00EA[0]` = 13h → `gfx_set_mode(13h)`,
`DS:E338 = 13h`), linear 320 bytes/row — not the unchained mode 14h the first-pass RE_GUIDE assumed. Page 0
(`DS:90CC`) is the visible A000h memory, page 1 (`DS:90CE`) a 64000-byte RAM page (platform spec).

-----------------------------------------------------------------------------------------------

## 1. Overview

### 1.1 Buffers and screen layout

| Buffer | Where | Size | Written by | Reaches the screen |
|---|---|---|---|---|
| **V** 3D view | seg `DS:90D0` = far `CC5C` (7810h bytes) normalised | 320 × 96 bytes, row table `DS:B8D3`; only the top `BA91` rows and left `BA95/32+1` columns are used | sky/ground (`373c`, during frame_update), faces + sprites (`323e`), overlays (`46c0`), mirror rim (`7c21`) | `0c1c:13d8` copies it to page 0 at `(x0, 16)` |
| **M** mirror | seg `DS:90D2` = DOS block `E7DE`+1 (also the LZ decompressor's dictionary) | 88 × 19, row table `DS:B993` | mirror sky/ground (`7b9b`), mirror faces/sprites (`3a9e`/`28eb`), frame (`7c21`) | `0c1c:17cd` copies it to page 0 at x 168..255, rows 11..29 |
| page 0 | A000h (`DS:90CC`) | 320 × 200 | HUD/dashboard (hud spec) drawn directly; V and M copied in | visible |
| page 1 | RAM (`DS:90CE`) | 64000 | library draw page during the 3D pass (`DS:009A = 1`); dashboard and top strip backups | never shown directly |

Near DGROUP scratch: `DS:2500..8DC1` is **one shared area**. At stage load it holds the sprite set
`<scene><d>.DAT` (world.md), which `2789` pre-scales into the far cache `E53C`; during the race the same bytes
are the vertex/projection arrays (`2502`, `3182`, `3E02`, `4A82`, `5702`, `6382`, `7002`, `7C82`, 640h words
each) and the sprite lists (`8902`, `8A32`, `8B62`, `8C92`); the crash pictures are decompressed there too
(`0792:1c00`). The raw sprite file is never read after `2789`.

View windows (`0e12:409c`, placed by `DS:90DC` = 0010h → screen row 16):

| Mode | Flag | V used | On screen | Mirror |
|---|---|---|---|---|
| full | `B6DC = 0` | 320 × 96 | x 0..319, rows 16..111 | x 168..255, rows 11..29 (rows 16..29 overlap view rows 0..13) |
| half (F1) | `B6DC = 1` | 240 × 64 | x 40..279, rows 16..79 (F1 blacks x 0..39, 280..319 and rows 80..111 once) | same place = V columns 128..215 |
| attract/title demo | `09C4 = 1` | 256 × 86 | x 32..287, from row `lo(90DC)` (16h in `01f4:144c`) + 5 | none (`CC92 = 1`) |

**How a frame reaches page 0 (mode 13h, no page flipping):** after the HUD is drawn, `race_run` calls
`0c1c:13d8`. It first calls `0c1c:17cd`, which busy-waits for vertical retrace (port 3DAh bit 3) and then —
if the mirror is on (or needs one more refresh, `B6D1`) and not invalid (`BD3F`) and not in the replay
layout (`CC92`) — copies M to page 0: five rows with a rounded top (screen rows 11..15, above the view) and
14 full rows (rows 16..29, x 168..255). Then `13d8` copies V to page 0 row by row, starting at view row
`min(BAD4, BAD6)` (the topmost row any geometry touched this or last frame; rows above keep the sky already on
screen), or 15h rows down after a message box (`BA82`). For the first 14 view rows (screen rows 16..29) it skips
screen x 168..255 so the mirror is not overwritten: full view: copy x 0..167, skip 88, copy x 256..319; half
view: copy x 40..167, skip 88, copy x 256..279 (the half view ends at x 279; V columns 240..319 are simply
unused in that mode, and screen x 280..319 are the black border). The hole is cut whenever `CC92 = 0`, even with
the mirror switched off: the mirror area then shows the grey glass that `7c21` painted into M once. There is no
double buffering: V is the back buffer and page 0 the front; the copy is timed right after retrace. The
library blitters are not used by the 3D path: `1818:0003` (rectangle copy between pages) is only used by the HUD
and the layout switches (`0792:04ca`/`059a`, `0e12:0cbe` compass), `185f` by HUD digits, `17eb` only by the front
end, and `17be` has no caller.

### 1.2 Frame order (race_run `0792:000c`, ≥ 5 ticks of 145.6 Hz per frame)

1. `frame_update` `0e12:76ec`: screen shake, simulation (`095a`, `23df`, `0b1d`, `0977:0008`, `0ab4:1220`),
   camera (`6e92` + camera select), page 1, **`70cd`** world build (only when the cell/octant changed),
   **`34c3`**: vehicles/objects (`4d60`), crossing gates (`5e70`), **project** (`394c`), **sort** (`361c`),
   near-face hits (`34f1`, cockpit only), **sky + ground into V and M** (`373c`), runtime face colours
   (`4d6d`), police (`5ffe`).
2. `race_input(0)`.
3. `frame_draw` `0e12:77d5`: page 1; **`323e`**: sprite list/project/sort/animate/collide, then the
   **merged back-to-front loop over faces and sprites** (faces rasterized into V, or into M when they face
   backwards and the mirror is on — this also finds the ground face under the camera); then (unless `948C`):
   `6421` terrain slope under the car (physics input), water-crash test, cockpit overlays `46c0` (rain/snow,
   windscreen drops, headlight beams, dashboard edge, wipers), replay start `4c51` after a crash, mirror frame
   `7c21`; page 0.
4. `race_input(1)`, HUD (hud spec), `0c1c:13d8` present (4.12), `race_input(2)`, wait.

### 1.3 Call graph

```
0000:0000 main ─ 0000:11d2 (allocs: E5B8 faces, CC5C view, E53C sprite cache …) ─ 0e12:2537 frame_buffers_init
0792:0d2e stage_load ─ 0e12:2789 sprites_prescale (2810 289a 2eb6 2d54/2f84 2dc6/3004 276e)
                     ─ 0e12:409c view_setup       ─ 0e12:255e leg_state_reset ─ 25f4, 2465 (261c, 03d4, 61d2)
race_run loop
 ├ 0e12:76ec frame_update
 │   ├ 0fa1 shake · 095a 23df 0b1d 0977:0008 0ab4:1220 (simulation) · 6e92 (6f9c) camera · 1714:000e page 1
 │   ├ 70cd world_build_visible ─ 70b8 · 72f8 model_place ─ 75c1 (7653) · 7487
 │   └ 34c3 scene_prepare
 │       ├ 4d60 ─ 4fc7 (539d ─ 75c1 7487) · 509b (5466 5aa4 · 52a3 ─ 75c1 · 7487) · 5ad4 (5cf2 7653 22fe) · 5df9 (234b)
 │       ├ 5e70 ─ 5f57 (234b)
 │       ├ 394c vertices_project
 │       ├ 361c faces_sort_keys ─ 35f9 · 7a18/7a35 · 7b06
 │       ├ 34f1 near_face_hits ─ 4590 · 4631 (0edb) · 0fd6
 │       ├ 373c sky_ground_draw ─ 3fe5 · 3f6c ─ 41d4 · 7b9b ─ 7cd9 · 7d94 ─ 7e11
 │       ├ 4d6d ─ 4db6
 │       └ 5ffe ─ 61d2
 ├ 0e12:77d5 frame_draw
 │   ├ 323e draw_faces_and_sprites
 │   │   ├ 3085 · 316c · 312e (78a7 78cc 794b 797b · 3147) · 3352 · 33f7 (0edb 0fd6 47fc)
 │   │   ├ sprites: 2e49 · 28eb ─ 2b74 (2c7c) | 2999 (2a9e)
 │   │   └ faces: 3a7c ─ 3a9e
 │   │        ├ front: 39fd · 66ad (43db) · 67af (44c2 | 41d4) · 682e/685c/68b0/6909/6956 · 69da · 41d4
 │   │        │        · around-camera: 6316 · 0fd6 · 3ebf ─ 3f6c/3fe5
 │   │        └ mirror: 8b5f · 81c7 (8016) · 82d6 (80fb | 7e11) · 836e/839c/8413/8490/8501 · 85a9 · 7e11
 │   ├ 6421 (6604 6666) · 0f31 · 46c0 ─ 46dc (477c 47aa 47d1 47fc) · 4b37 (69da) · 4bf3 (4cb0) · 4a5c (487e 48f5 495c 4cb0)
 │   ├ 4c51 · 7c21
 └ 0c1c:13d8 view_present ─ 17cd mirror_present
```

-----------------------------------------------------------------------------------------------

## 2. Function table

Merged table for the whole subsystem (core rasterizer rows first written by hand; the sprite, object and
overlay rows come with their sections 4.13, 4.14, 4.12). Library/platform rows are listed only for what the
renderer calls (platform spec owns them).

| address | proposed name | signature | purpose | confidence |
|---|---|---|---|---|
| 0792:04ca | chase_view_exit | far void() | restore top strip + dashboard from P1, CC92=0, B6D1=1 | verified |
| 0792:059a | chase_view_enter | far void() | save top strip to P1, blank it, 0602 replay panel, CC92=1 | verified |
| 0792:19ca | water_overlay_start | far void() | splash image on P1, mirrored halves, then 1a72 animation | likely |
| 0792:1c00 | broken_glass_overlay | far void() | decompress crash image, draw into V, mirror frame, full present | verified |
| 0c1c:13d8 | view_present | far void() | 17cd, then copy V → S with row skip and mirror hole | verified |
| 0c1c:16f8 | ega_shift_copy | near | Tandy/EGA packer; not used in mode 13h | verified |
| 0c1c:1759 | rle_draw_viewbuf | far (src, runs, row) | RLE (colour,count) bitmap into V from row upward, colour 0F transparent | verified |
| 0c1c:17cd | mirror_present | near void() | wait VR, copy M → S (fixed rectangle) | verified |
| 0e12:012d | key_mirror | near void() | B6D2 ^= 1 | verified |
| 0e12:026f | key_chase_view | near void() | toggle external camera on a frozen scene (948B) | verified |
| 0e12:0302 | key_wipers | near void() | BC7B ^= 1 (not in replay) | verified |
| 0e12:030f | key_headlights | near void() | B6D8 ^= 1 (not in replay) | verified |
| 0e12:03d4 | detail_apply | near | view window `B6DE` by detail, sprite seed mask, traffic thinning by skill | verified |
| 0e12:0534 | key_instant_replay | near void() | start replay (4c51) / re-request it | verified |
| 0e12:059c | key_replay_pause | near void() | during replay toggle B6D4 (1220 returns early = paused) | likely |
| 0e12:05e3 / 0606 / 0667 | key_debug_rain / key_debug_snow / key_debug_night | near void() | debug (only if B6EA): cycle BC75 / BC77 0..8, toggle 95C7 + sky colours, rebuild remap | verified |
| 0e12:0751 (0832..08d8) | controls_poll | near (cl = direction bits) | orbit distance/angle/height/pitch from stick in replay | verified (bit→direction: guess) |
| 0e12:0edb | random_damage | far, no args | 1/8: sfx 0Ch + `9486 \|= 4<<b`; 1/64: message 2Eh + `9486 \|= 1<<b` | verified (code) |
| 0e12:0f31 | crash_start | near void() | once: B78D=1, music_stop, shake counter 947C=0x10, BAAB = object whose id range holds BAA9 | verified |
| 0e12:0fa1 | screen_shake_step | far void() | if 947C: 947C--, unless E776==0x13 set CRTC display start from table 0e12:0F81[947C] | verified |
| 0e12:0fd6 | bounce_back | near, keeps BX,CX | `947C`=4, copy car state `DS:1282[6]` → `DS:125E[6]` | verified (code), likely (meaning) |
| 0e12:22fe | atan2_xz | CX=x, DX=z (s16) → AX=angle; clobbers BP,DI,CX,DX | `angle = atan2(−z, x)`, 10000h/turn, table 0e12:12F6 | verified |
| 0e12:234b | polar | far cdecl (angle, r) → `9460`=r·sin, `9462`=r·cos | 512-step sine table 0e12:0FF2 | verified |
| 0e12:2465 | life_reset | far | optional restart pos (B9CB..), colour remap, 03d4, reset ~40 race vars | verified |
| 0e12:2537 | frame_buffers_init | far void() | `90D0` = normalised seg of far `CC5C` (7810h bytes), `90D2` = seg `E7DE`+1 (DOS block of 300h paragraphs) | verified |
| 0e12:255e | leg_state_reset | far | seed (10–12 bits set) → `BA56`/`B6ED`, 25f4, 2465, zero leg state, car pos = sprite 0 | verified |
| 0e12:25f4 | view_row_tables_init | near void() | `B8D3[r]=r·140h` (r<60h), `B993[r]=r·58h` (r<13h) | verified |
| 0e12:261c | build_colour_remap | far void() | 32-byte colour-code remap at far `E5B0` (day/night, rain, snow); settled name (alias colour_code_map_build) | verified |
| 0e12:276e | sprite_pixels_remap | in DI = first pixel byte, DX = end, SI = remap table (all DS-relative, ES = DS); clobbers AX, BX | if `(u8)DS:E338 != 13h`: every byte ≥ 10h in [DI, DX) becomes `table[b-10h]` | verified (code); purpose (non-VGA) likely |
| 0e12:2789 | sprites_prescale | far, no args; saves SI DI ES | builds the pre-scaled sprite cache in the far buffer `E53C` from the sprite file at `DS:2500`; fatal message 1Ah on overflow | verified |
| 0e12:2810 | sprite_prescale_one | near; in AL = sprite id, DI = cache write offset, ES = cache seg; out DI = new write offset | writes the sprite's step directory and its scaled copies according to `95E1[id] & 7` | verified |
| 0e12:289a | sprite_scale_setup | near; in B9D6/B9D7 = scale, B9D8 = id, DI, ES; out DI | loads the masks, writes the copy header, dispatches to shrink/grow | verified |
| 0e12:28eb | sprite_draw (was draw_sprite_child) | near; in BX = k (visible index), B9D5, B9D6, B9D8; saves BX ES | picks the cached copy, converts x, calls the main or mirror blitter | verified |
| 0e12:2999 | sprite_blit_mirror | as 2b74 | same for the rear-view mirror (88 px wide, 19 rows, half vertical scale) | verified |
| 0e12:2a9e | sprite_rows_mirror | as 2c7c | row copy into `seg 90D2` (stride 58h, end 688h) | verified |
| 0e12:2b74 | sprite_blit_main | near; in BX = k, SI = copy pixel data, ES = cache seg, 947E = centre x px, 9480/9481/B9DB/B9E1 | horizontal clip (0..319), vertical projection with roll, top clip, then 2c7c | verified |
| 0e12:2c7c | sprite_rows_main | near; in AL = top row (≥ 0), SI, B9D4, B9DE, B9DF | transparent row copy into `seg 90D0` (stride 140h) until `BA93` or H rows | verified |
| 0e12:2d54 | sprite_scale_rows | near; in SI = row-pointer table, CX = row-length table, DI, B9E1, B9E3, 9480 = w, 9481 = h; out DI | shrink (scale ≤ 1): keeps a row when the next vertical bit is 1 | verified |
| 0e12:2dc6 | sprite_scale_rows_up | same as 2d54 | grow (1 < scale ≤ 2): stores every row once, with a repeat count `B9D7 + bit` | verified |
| 0e12:2e49 | sprite_pick_scale | near; in BX = 2·k, DX = depth key `8902[k]`, B9D5; out B9D8 = sprite number, B9D6 = apparent size; saves BX | `B9D6 = hi(atan(size/depth))` in 8-px units | verified |
| 0e12:2eb6 | sprite_scale_patterns | near; in B9D6, B9D7 (caller 0); out B9D7, masks B9EF..BA08 and BA0A..BA0C | converts the scale value to a doubling count + a 24-step remainder and loads the horizontal (56-bit) and vertical (24-bit) keep masks from `CS:267E` | verified |
| 0e12:2f84 | sprite_scale_row | near; in BX = source pixels, CL = row-length word low byte, DI (ES), 9480 = w; out DI advanced, DX = DI at entry | one row, shrink: emits pad/pixel only where the horizontal mask bit is 1 | verified |
| 0e12:3004 | sprite_scale_row_up | same as 2f84 but no DX output | one row, grow: emits every pad/pixel once, and again when the mask bit is 1 | verified |
| 0e12:3085 | sprite_list_build | near, no args; saves SI DI | rebuilds the visible-candidate list `8B62[]`, count `94AE` | verified |
| 0e12:312e | sprite_sort | near; saves SI | full sort (quicksort) after a list rebuild, else bubble sort | verified |
| 0e12:3147 | sprite_sort_bubble | near | calls 797b on [first, 94AE-1] | verified |
| 0e12:316c | sprite_project | near; saves BP | per listed sprite: screen angle 8C92, clamped distance 8A32, depth key 8902 | verified |
| 0e12:323e | draw_faces_and_sprites | near; saves ES SI DI BP | frame driver: list/project/sort/animate/collide, then the face+sprite merge loop | verified |
| 0e12:3352 | sprite_animate | near | drift (95B7/95B9), animation chain (9611), crash-debris flight (ids 6..8) | verified |
| 0e12:33f7 | sprite_collisions | near | radius test on the depth key against 95CF, effect by collision class | verified (code); effects likely |
| 0e12:34c3 | scene_prepare | near void() | `BAD4`=60h; `4d60`, `5e70`, `394c`, `361c`, `34f1` (cockpit only), `373c`, `4d6d`, `5ffe` | verified |
| 0e12:34f1 | near_face_hits | near | walk sorted faces with depth ≤ `94C1`: types 18h/19h/1Bh → 4631, 1Ah → 4590, 1/2 → crash or bump | verified |
| 0e12:35f9 | face_order_reset | near void() | `order[i] = E5B8 + 10·i` for 640h entries | verified |
| 0e12:361c | faces_sort_keys | near void() | per face depth key (average or bit-13 farthest rule), then full quicksort (list rebuilt) or one bubble pass-set | verified |
| 0e12:373c | sky_ground_draw | near void() | sky-flash countdown `BAA6` (lightning / police); horizon line from `9496`/roll; sky (flat or 5-band gradient) + ground polygon into V; mirror sky/ground `7b9b` | verified |
| 0e12:394c | vertices_project | near void() | for every vertex: screen x (`4A82`, 1/32 px angle), horizontal distance (`7002`), depth (`6382`); mark y lazy (`5702`=`7C82`=8000h) | verified |
| 0e12:39fd | vertex_project_y | near (BX=2·v) | front view y (`5702`) with pitch/roll; `BAD4`=min | verified |
| 0e12:3a7c | draw_face_dispatch | near (BX=2·v0, SI=2·v1, DH=w0_hi&C0h, ES:BP=&face.w1) | by vertex count: point `3bba`, line `3c0c`, triangle `3a91`→`3a9e`, quad `3b0e` | verified |
| 0e12:3a9e | draw_face_polygon | near (BX,SI,DI [,BP] = vertex offsets) | front/mirror visibility tests, lazy y projection, choose rasterizer; face-under-camera path; quad split | verified |
| 0e12:3ebf | fill_around_camera | near (BX,SI,DI) | triangle enclosing the camera: for its 3 edges (sorted by x) fill below (ground) or above (`BACF`) | verified |
| 0e12:3f6c | fill_below_edge | near (AX=x0, CX=x1, BX/SI=vertex offsets, `5702`) | fill the region below the edge down to the view bottom (between x0..x1) | verified |
| 0e12:3fe5 | fill_above_edge | near (same) | fill the region above the edge, from `BA9C` (or the edge top) | verified |
| 0e12:409c | view_setup | far void() | view size from `09C4`/`B6DC`: `BA8F`,`BA91`,`BA93`,`BA95`,`BA97`,`BA99`,`BD3D`,`B7AF[8]`; patches 24 clip immediates in CS (table `DS:BAE4`) | verified |
| 0e12:41d4 | fill_spans | near (SI=x left, BP=x right in 1/32 px; `BA9C`,`BAC1/3/5`,`BAB3/5`,`9460/2`,`BABD/F`,`BAC7..BACD`,`BAAF/BAB0`,`BAB1`,`BA9A`) | 3-segment edge walker writing dithered spans (or OR-ing when type 0) into V | verified |
| 0e12:43db | blob_spans | near (SI,BP=x; `BAB3`,`BAB5`,`BA9C`,`BAC1`) | disc-like blob, no dither | verified |
| 0e12:44c2 | line_thin_spans | near (BP=x top, SI=x step; `BA9C`,`BAC1`) | 1-px line as per-row spans, no dither | verified |
| 0e12:4590 | plate_fold_down | ES:BP=&face.w3, keeps BX,CX | type-1Ah face pair: fold the 2nd face flat, both faces → type 3; sfx 4 | verified |
| 0e12:4631 | knockover_hit | CH=type, ES:DI=&sort ptr, keeps BX,CX | find the object owning the face, replace its model (knocked-over variant / bird takes off), force world rebuild, damage roll | verified |
| 0e12:46c0 | cockpit_overlays | near void() | 46dc; if !948B: 4b37, 4bf3, 4a5c | verified |
| 0e12:46dc | precipitation_draw | near void() | snow (BC77) flakes / rain (BC75) streaks into V, spawn windscreen drops; no weather: retire one drop slot | verified |
| 0e12:477c | rain_streaks | near (es=V, al=colour, di=rand base, si=n-1, bx=limit) | 2n vertical streaks, length ((n+1)>>1)+3 | verified |
| 0e12:47aa | snow_flakes | near (same regs) | 2n single pixels + copies at offset ^0x4000 | verified |
| 0e12:47d1 | windscreen_spawn | near (si=n, dl=type) | 0..2 new drops per frame from RNG DS:00D2/00D4 | verified |
| 0e12:47fc | windscreen_splat | near (ax=rand word, dl=type) | put drop in 64-slot ring; compute wiper key | verified |
| 0e12:487e | windscreen_drops_draw | near void() | age and draw the 64 drops into V | verified |
| 0e12:48f5 | drop_shape_fresh | near (es:di, ax=colour pair, bx=limit) | 5-row blob (age ≤ 1) | verified |
| 0e12:495c | drop_shape_old | near (same) | 7/8-row speckle, 2 variants by colour | verified |
| 0e12:4a5c | wipers_update_draw | near void() | wiper state machine, wipe drops, draw drops, draw 2 wiper blades | verified |
| 0e12:4b37 | headlight_beam_draw | near void() | 2 quads OR-ed with colour 08 (brightening) when headlights on at night / in weather | verified (OR effect: likely) |
| 0e12:4bf3 | dashboard_edge_draw | near void() | 4-quad silhouette of the dash/hood at the view bottom (crashed variant after crash) | verified |
| 0e12:4c51 | replay_start | near void() | enter replay camera (948B=1, 94B2=1), end-mark the recorder ring | verified |
| 0e12:4cb0 | overlay_quads4_draw | near (stack: outline colour pair) | 4 quads from slots C68..C7E + 2 edge lines | verified |
| 0e12:4d60 | objects_update | near, no args | calls 4fc7, 509b, 5ad4, 5df9 | verified |
| 0e12:4d6d | obj_runtime_colours_all | near; CX in = leftover (see 4db6) | per-frame counters, traffic-light phase; 4db6 for every object with faces | verified |
| 0e12:4db6 | obj_face_runtime_colours | BX=slot, CX=loop limit (not initialised) | override colour word of the object's leading faces of types 4–12; type 13 animates model vertices 0,1 (wipers) | verified |
| 0e12:4fc7 | parked_vehicles_emit | near; clobbers all | emit objects 0..A477−1 with flags 1000h set / 2000h clear (parked vehicles; lightning object) via 539d | verified |
| 0e12:509b | moving_objects_update | near | per moving object (flags 2000h): timers, `5466`, integrate velocity, window test, emit via 52a3 + 7487 (face cache) | verified |
| 0e12:52a3 | obj_emit_vehicle_moving | BX=slot, DX=Manhattan dist → CX=nf (normal path) | set placement from object arrays, pick model/LOD, transform vertices (75c1) | verified |
| 0e12:539d | obj_emit_vehicle | BX=slot, DX=dist, ES=O-set seg | as 52a3 for parked O-models; reuses last frame's vertex+face ranges when slots unchanged; emits faces | verified |
| 0e12:5466 / 5aa4 | traffic_follow_path / path_tile_lookup | BX=slot | see the simulation spec; called from 509b | — |
| 0e12:5ad4 | vehicle_collisions | near | clears/sets stop flags; pairwise box test of moving objects vs lower-index visible objects; sets 8000h/9000h, crash `BAA8` | verified |
| 0e12:5cf2 | vehicle_collision_boxes | BX=slot j, box centre in BCC4/BCC6/BCC8, `[9462]`=r,h of i's box, ES/SI clobbered | test all boxes of object j, OR bits into `946C` | verified |
| 0e12:5df9 | lighthouse_rotate | near | moves 2 vertices of the lighthouse's first face on a circle (beam) | verified |
| 0e12:5e70 | crossing_gate_update | near | train (models 12h–14h) near first gate → gate angle `BCD8`, bells; calls 5f57 for up to 2 gates | verified |
| 0e12:5f57 | crossing_gate_arm_place | SI=gate slot | place arm tip vertices from `BCD8`, arm length `95D6` | verified |
| 0e12:5ffe | police_update | near | police (models 11h, 30h): radar level, pursuit start, catch/ticket, siren sound | verified (code), likely (meaning) |
| 0e12:61d2 | police_pursuit_clear | near | `flags &= BFBFh` for all police models; `BCB6`=0 | verified |
| 0e12:61fd | opponent_times_finalize | far (race_run end, if `B09`) | 624a for both opponents, copy times to results records 1,2 (`DS:E80F`, `E82A`) | verified |
| 0e12:624a | opponent_time_estimate | BX=opp slot (2/4), SI=0/3 | unfinished opponent: estimate time from distance; add 20 s per ticket | verified |
| 0e12:6316 | plane_height_at_camera | near (BX,SI,DI) → AX | height of the triangle's plane at camera X/Z (`BCDF`), `BACF`=1 if above camera Y | verified |
| 0e12:6421 | ground_slope_update | near void() | no drawing: terrain triangle under the car (BA89/8B/8D) → roll code BCDC, clamped pitch delta BCDD | verified |
| 0e12:6604 | rotate_by_heading | near (ax=a, cx=c, [946A]=angle byte) → ax, cx | rotates vector by 256-step angle with Q15 tables DS:B7CF (sin, 65 entries) / DS:B851 (cos) | verified |
| 0e12:6666 | slope_to_roll_code | near (al) → al | sign(al) × DS:BD01[(abs(al)&0x7F)>>2]; roll is a *shift count* (0 = level) | verified |
| 0e12:66ad | point_draw | near (BX=2·v, SI=w0) | lamp blob: size `95D9[k]` over distance, width by the owning object's heading (one-sided with w0 bit 13) → `43db` | verified |
| 0e12:67af | line_draw | near (BX,SI=vertex offsets, AX,DX=their x, ES:BP=&w0) | thin (`44c2`) or width `95DD[k]` scaled by distance (parallelogram via `41d4`) | verified |
| 0e12:682e | tri_fill | near (BX,SI,DI) | sort by y; flat row `685c`, flat bottom `68b0`, flat top `6909`, general `6956` → `41d4` | verified |
| 0e12:685c | tri_fill_row | entry inside 682e (also from 69da) | one row from min x to max x | verified |
| 0e12:68b0 | tri_fill_flat_bottom | entry inside 682e (also 69da) | apex top | verified |
| 0e12:6909 | tri_fill_flat_top | entry inside 682e (also 69da) | apex bottom | verified |
| 0e12:69da | quad_fill | near (BX,SI,DI,BP; `BAD0`=face) | y-sort with top-vertex word tracking (`BAD2`), degenerate cases → triangle entries, else 2- or 3-segment chains → `41d4` | verified |
| 0e12:6e92 | camera_pos_update | near void() | cockpit/replay camera X/Z, sun/moon sprite 8, crash object sprite 7 | verified |
| 0e12:6f9c | polar_small | near (bl=angle, dl=dist) → cx=dX, dx=dZ | −2·dist·sin/cos via CS table 0e12:0FF2 | verified |
| 0e12:70b8 | obj_ranges_clear | near void() | `B0F9[i]=0`, `B379[i]=0` for 160 objects | verified |
| 0e12:70cd | world_build_visible | near void() | when the car's cell or heading octant changes: reset lists, emit the 3/6/10 visible cells (`72f8` AH=0) and the static objects in them (`72f8` AH=1); else only route id `0A74`. Settled name (was world_cells_collect / world_build_visible) | verified |
| 0e12:72f8 | model_place | near (AL=model, AH=0 tile / 1 O-object; BD2A/2C/2E, 946A, BD38, BD26) | resolve model (alias rule), `75c1` vertices, `7487` faces, then tile sprite children into the sprite instance list. Settled name (was obj_emit_static / model_place) | verified |
| 0e12:7487 | model_emit_faces | near (ES:SI=first face, CX=nf, 945E=vertex base) | append `nf−BD24` faces (max 640h) as 10-byte records with remapped colour pair; record object range; note gate/lighthouse objects. Settled (was obj_emit_faces / model_faces) | verified |
| 0e12:75c1 | model_emit_vertices | near (ES:SI=first A word, CX=nv, 946B=nv) → SI past C[] | reserve `nv` vertices at `BADA` (max 630h), world-space transform via `7653`, record object range. Settled (was obj_transform_verts / model_vertices) | verified |
| 0e12:7653 | vertex_rotate | near (ES:SI=&A[i], BX=2·nv, BD38, 946A) → DX=A', AX=B', CX=C' | pitch shear, fine rotation (64 steps/quarter, Q15 tables `B7CF`/`B851`), quarter turn. Settled (was vert_rotate / vertex_rotate) | verified |
| 0e12:76ec | frame_update | far void() | per frame: shake, sim, camera select, `70cd`, `34c3` (3D build + sky/ground into V) | verified |
| 0e12:77d5 | frame_draw | far void() | page 1; `323e` faces+sprites into V/M; overlays; mirror frame; page 0 | verified |
| 0e12:78a7 | sprite_sort_quick | near; sets BX = 2 | calls 78cc on [first, 94AE-1] | verified |
| 0e12:78cc | sprite_qsort | near, cdecl-like: [bp+6] = first ptr, [bp+4] = last ptr (into 8902), BX = 2 | recursive quicksort, descending on 8902; ranges ≤ 20 elements go to 797b | verified |
| 0e12:794b | sprite_rec_swap | near; SI, DI = pointers into 8902 | swaps two records in the four parallel arrays (stride 130h) | verified |
| 0e12:797b | sprite_bubble_sort | near; [bp+6] = first ptr, [bp+4] = last ptr | bubble sort, descending, pass ends at the last swap | verified |
| 0e12:7a18 | faces_quicksort_all | near void() | `7a35(keys[0], keys[BAD8−1])` | verified |
| 0e12:7a35 | faces_quicksort | near cdecl (lo, hi byte ptrs into key array, ES=E5BA, BX=2) | ascending quicksort on u16 keys, parallel pointer array at −C80h; ≤ 21 elements → `7b06` | verified |
| 0e12:7b06 | faces_bubble_sort | near cdecl (lo, hi) | ascending bubble passes ending at the last swap; stops when the remaining range is ≤ 2 elements (quirk) | verified |
| 0e12:7b9b | mirror_sky_ground | near void() | mirror horizon (from `948F`, roll sign ignored) → `7cd9` sky, `7d94` ground into M | verified |
| 0e12:7c21 | mirror_frame_draw | far void() | clear M grey when invalid/turned off; draw M border; draw mirror lower rim into V | verified |
| 0e12:7cd9 | mirror_fill_above_edge | near (as 3fe5, y halved) | clone of `3fe5` for M | verified |
| 0e12:7d94 | mirror_fill_below_edge | near (as 3f6c, y halved, bottom 13h) | clone of `3f6c` for M | verified |
| 0e12:7e11 | fill_spans_mirror | as 41d4 | mirror clone: segment `90D2`, rows `B993`, 13h rows, width B00h | verified |
| 0e12:81c7 / 8016 | point_draw_mirror / blob_spans_mirror | as 66ad / 43db | mirror clones (distance ×2, `14h` for `BA99`, mirror x/y) | verified |
| 0e12:82d6 / 80fb | line_draw_mirror / line_thin_spans_mirror | as 67af / 44c2 | mirror clones | verified |
| 0e12:836e / 839c / 8413 / 8490 | tri_fill_mirror (+row / flat bottom / flat top entries) | as 682e.. | mirror clone of 682e with mirror x transform | verified |
| 0e12:85a9 | quad_fill_mirror | as 69da | mirror clone of 69da | verified |
| 0e12:8b5f | vertex_project_y_mirror | near (BX=2·v) | mirror y (`7C82`), half scale, centre row 0Ah | verified |
| 16d9:00c7 / 16e5:0007 | gfx_page_segment | far → ax | returns BD88 | verified |
| 1703:0001 | gfx_set_colour | far (c) | mode 13h: BD41 = BD42 = c | verified |
| 1714:000e | gfx_set_page | far (page) | mode 13h: BD40=page, BD88 = DS:BD8C[page] (page segment) | verified |
| 1776:0008 | gfx_set_origin | far (x, y) | mode 13h: CRTC start = y·80 + x/4 (byte offset y·320 + (x&~3)); waits VR end | verified |
| 1785:000b | gfx_fill_rect | far (x0, x1, y0, y1) | mode 13h: fill rows y0..y1, cols x0..x1 of current page with BD41 (bottom row first) | verified |
| 1818:0003 | gfx_copy_rect | far (x0, x1, y0, y1, dx, dy_bottom, srcpage, dstpage) | mode 13h: copy rows y1..y0 upward to dest rows dy_bottom..; dest bottom-left = (dx, dy_bottom) | verified |

**Settled names** (competing names in port/symbol_conflicts.txt): 0e12:70cd world_build_visible (was world_cells_collect), 0e12:72f8 model_place (was obj_emit_static), 0e12:7487 model_emit_faces (obj_emit_faces / model_faces), 0e12:75c1 model_emit_vertices (obj_transform_verts / model_vertices), 0e12:7653 vertex_rotate (vert_rotate), 0e12:261c build_colour_remap (colour_code_map_build), 0e12:3a7c draw_face_dispatch (the face-under-car test is done later in 3a9e/6316). Where the simulation / hud / platform specs already named a shared routine (0c1c:13d8 view_present, 0e12:0f31 crash_start, 0e12:6421 ground_slope_update, ...) their name is used here.

Other `0e12` functions (not in this table) belong to other specs: key dispatch and handlers `0080`, `0084`, `00d5`, `00d9`, `0133`, `0149`, `0156`/`0188`/`01c5`/`01d8`/`0229` (gears), `0269`, `056a`, `05cd`, `0630`, `06b6`, `0751`, `08d9`, `09d6`, `0b02`; `044e`/`04f1` (life lost / end of replay: messages, lives `09DE`); `095a`, `0b1d`/`0b85`/`0cbe`/`0db4`, `23df`, `0e74`, `0ec5`, `23b1`, `6686`, `5466`, `5aa4` → **simulation**. The render-relevant handlers `031c` (F1 window size) and `03ab` (F2 detail) are described in 4.2 / 4.4.

## 3. Globals

| DS offset | proposed name | type/size | meaning | written by | read by |
|---|---|---|---|---|---|
| 009A | draw_page | u16 | library draw page (1 during 3D frame, 0 otherwise) | 77d5, 76ec, 0084, race_run… | 1714:000e arg |
| 00D2/00D4 | rng_state | u32 | RNG (advanced by 0000:0f58, also in the frame-wait spin) | 0000:0f58 | 46dc, 47d1, 33f7 |
| 09C4 | attract_mode | u8 | 1 = title demo (only object 0 moves, no parked cars) | 01f4:144c | 4fc7, 509b, 70cd |
| 16D4 / 16D6 | rec_head / rec_mark | u16 | replay recorder ring index (CS 0AB4:020F, &0xFFF) | 0ab4:1220, 4c51 | |
| 16D8 | rcam_manual | u8 | 1 = user moved replay camera (no auto-follow) | 026f, 0751, 0534(0) | 76ec |
| 2500 | sprite_file | bytes | `<scene><d>.DAT`: header + 8-byte records (see world.md "Sprites") | 0792:0d2e, 01f4:53c4 | 2789, 289a |
| 2502 / 3182 / 3E02 | vert_y / vert_x / vert_z | s16[640h] | world-space vertices of this frame (x, z in position·4 units) | 75c1, 5df9, 5f57, 4db6 (wipers) | 394c, 6316, 39fd, 6421 |
| 2502 / 2504 / 2506 | spr_pix_start / spr_remap_off / spr_count | u16 | pixel data start, remap table offset, record count n (incl. header) | file | 2789 |
| 4A82 | scr_x | u16[640h] | screen x = bearing angle in 1/32 px; overlay polygons use slots 630h..63Fh | 394c, overlays | rasterizers, 66ad |
| 5702 | scr_y | s16[640h] | front-view row, 8000h = not yet projected | 394c (8000h), 39fd, 373c (temp v0/v1), overlays | rasterizers |
| 6382 | vert_depth | u16[640h] | `abs(dy) + dist` painter key | 394c | 361c |
| 7002 | vert_dist | u16[640h] | horizontal distance (position·4 units) | 394c | 39fd, 8b5f, 66ad, 67af |
| 7C82 | mir_y | s16[640h] | mirror row, 8000h = not yet projected | 394c, 8b5f, 7b9b (temp) | mirror rasterizers |
| 8902 | spr_key | u16[98h] | depth key `abs(dy) + dist` (FFFFh for id 0) | 316c, sort | 323e, 33f7, 2e49 |
| 8A32 | spr_dist | u16[98h] | horizontal distance, clamped ≥ 95CD for ids > 8 | 316c, sort | 2b74, 2999 |
| 8B62 | spr_inst | u16[98h] | instance index ×2 | 3085, sort | all |
| 8C92 | spr_angle | u16[98h] | screen x in 1/32 px = angle − heading + view centre (multiple of 20h, see 28eb) | 316c, sort | 323e, 28eb, 2b74, 2999 |
| 90CC | screen_seg | u16 | page-0 segment (A000) | config_load 0000:0cde | 13d8, 17cd |
| 90CE | page1_seg | u16 | page-1 RAM segment | 0000:0ccb | lib |
| 90D0 | view_seg | u16 | V segment | 2537 | overlays, 13d8, 7c21, 1759 |
| 90D2 | mirror_seg | u16 | M segment (88×19) | 2537 | 7c21, 17cd, 3a9e |
| 90DC | view_origin | u16 | lo = screen row of view top (0x10 in race, 0x16 in 01f4:144c), hi = x add | race_run 0027 | 13d8 |
| 945E | vert_base / tmp | u16 | vertex base of the model being emitted (75c1 → 7487); temp in 3a9e/69da/6316 | 75c1 | 7487 |
| 9460 / 9462 | dxl2 / dxr2 | s16 | span engine: segment-2 slopes (also temps in 6316) | rasterizer setups | 41d4 |
| 9464 / 9466 / 9468 | quad temps | s16 | x of top, heights to b / c | 69da | 69da |
| 946A | place_rot / flag | u8 | model rotation byte (7653); reused as "flip"/"opposite" flag by 3fe5 and 69da | 70cd, 52a3, 539d | 7653, 69da |
| 946B | place_nverts | u8 | loop counter (vertices / sprite children) | 72f8, 75c1 | 75c1, 72f8 |
| 946C | coll_bits | u8 | 1 hit, 2 near, 4 close | 5ad4, 5cf2 | 5ad4 |
| 947C | shake_count | u8 | screen shake frames left (0x10 on crash) | 0f31, race_run 03ca (=1) | 0fa1 |
| 947D | swap_flag | u8 | 4 when the atan ratio was inverted | 2e49 | 2e49 |
| 947E | spr_x | u16 | sprite centre x in px, then left x after clip | 28eb, 2b74, 2999 | 2c7c, 2a9e |
| 9480 / 9481 / 9482 | spr_w / spr_rows / row_m | u8 | build: source w, h, row pixel count; draw: W, remaining H | 289a, 28eb, 2f84 | many |
| 9486 | damage_bits | u16 | random damage bits | 0edb, 2465 | simulation/HUD |
| 948B | ext_camera | u8 | 1 = external/replay camera (no cockpit overlays, no physics) | 4c51, 026f, 044e, 0ab4:1220, 2465 | many |
| 948C | freeze_debug | u8 | only ever cleared (race_run 0012) → always 0; if set: no overlays, camera Y free | race_run | 77d5, 76ec, 0751, 06b6 |
| 948F / 9491 / 9493 | car_pitch / car_heading / car_roll | i16/u16/i8 | car attitude (heading 0x10000=360°, roll = shift code) | physics 0977 | 76ec, 6421, 7b9b |
| 9494 / 9495 | car_x_frac / car_z_frac | u8 | fine position; top 2 bits → 94A1/94A3 | 0977 | 6e92 |
| 9496 | cam_row | s16 | horizon row = pitch + `BA8F` | 76ec | 39fd, 373c, 2b74 |
| 9498 | cam_heading | u16 | camera heading (10000h = 360°, 0 = +Z) | 76ec | 394c, 70cd, 316c, 66ad |
| 949A / 949C | cam_x / cam_z | u16 | camera cell coords (&7FFF / &3FFF) | 6e92 | 3D |
| 949E | cam_y | s16 | eye height | 76ec | 394c, 39fd, 6316, 3a9e |
| 94A0 | cam_roll | s8 | roll shift code | 76ec | 39fd, 8b5f, 373c, 7b9b |
| 94A1 / 94A3 | cam_x4 / cam_z4 | u16 | camera X/Z ·4 (+2 fraction bits) | 6e92 | 394c, 6316, 316c |
| 94A5 | rcam_pitch | i16 | replay pitch; live = copy of 948F; −0x18..0x30 | 6e92, 4c51(−0x10), 026f(0), 0751 | 76ec |
| 94A7 | rcam_y | i16 | replay camera Y; live = copy of A1F3 | 6e92, 77d5, 026f, 0751 | 76ec, 6e92 |
| 94A9 | rcam_height | i16 | replay height above ground 0x2E..0x140 | 4c51, 026f, 0751 | 77d5 |
| 94AB | rcam_angle | u8 | orbit angle (256 = 360°) | 4c51, 026f, 0751, 76ec | 6e92, 76ec |
| 94AC | rcam_dist | u8 | orbit distance (multiple of 8, 0x10..0xF8) | 4c51(0x30), 026f(0x40), 0751 | 6e92 |
| 94AD | rcam_roll | u8 | replay roll (always 0) | 4c51, 026f | 76ec |
| 94AE | sprite_vis_count | u16 | entries in the four arrays below (≥ 9, ≤ 98h) | 3085 | 316c, 312e, 323e, 3352, 33f7 |
| 94B0 | sprite_vis_first | u16 | first k of the merge loop; static 0, no writer found | – | 323e |
| 94B2 | replay_state | u8 | 0 live/recording, 1 replay playing, 2 post-crash hold | 4c51, 0ab4:1220, 044e, 2465 | 6e92, 026f, 0534, … |
| 94B3 | crash_frame | u8 | crash animation countdown (14h..) | 0ab4 | 3352 |
| 94B4 / 94B6 | car_x_saved / car_z_saved | u16 | car X/Z when not replaying | 6e92 | ? |
| 94BF | car_eye_height | u16 | subtracted from `A1F3[0]` for debris height | sim | 3352 |
| 94C1 / 94C3 | near_depth / near_dy | u16 | depth threshold / vertical tolerance for 34f1 | (camera) | 34f1 |
| 94CB / 94CD | car_cockpit_colour / car_cockpit_edge | u16 | dash fill / dash edge-line colour pairs (0404 / 0C0C default) | car .LST | 4bf3 |
| 94CF[8] | beam_verts | (y, x<<5)×8 | headlight trapezoid rows 72..95 | const | 4b37 |
| 94E7 | wiper2_dx | i16 | 0x1040 = 130 px shift of the right wiper | const | 4a5c |
| 94E9 | wiper_radius | u16 | 0x78: drops farther than this from a pivot are never wiped | const | 47fc |
| 94EB[5][6] | wiper_verts | u16 (hi=y, lo=x) | wiper blade polygon per phase 0..4 | const | 4a5c |
| 9525[6] | wiper_keys | i16 | 0x602C, 0x7FFF, 0x137, 0x8A, 8, −0x42 (angle keys, index −1..4) | const | 4a5c |
| 9531[12] / 9561[12] | dash_verts_crashed / dash_verts | (x, y) words | dash silhouette | const | 4bf3 |
| 95B7 / 95B9 | sprite_drift_x / _z | s16 | per-frame X / Z drift of ids with bit 80h / 40h; sign re-set every frame from the heading (`76ec`: 95B9 sign = heading sign, 95B7 sign = sign of heading-4000h) | leg file, 76ec | 3352 |
| 95BF | sprite_scale_count | u8 | number of scale steps (11h SCENE01/TT, 10h SCENE02) | leg file | 2789, 2810 |
| 95C0..95C3 | sky_colours | u8[4] | EGA sky (C0 / alt C1), VGA sky (C2 / alt C3); alt when `95C8` | leg file, key N | 373c |
| 95C4 | ground_colour | u8[2] | ground colour codes (lo, hi) → `B4B9` pair | leg file | 373c |
| 95C7 | colour_mode | u8 | 1 day, 0 night | 0667 | 4b37, 6e92, 41d4 |
| 95C8 | sky_alt | u8 | weather active (overcast sky) | 46dc, race_run, 05e3/0606 | 373c, 6e92 |
| 95CD | sprite_min_proj_dist | u16 | 17Bh in all legs: minimum distance used for the *vertical* projection of sprites with id low byte > 8 (stops near sprites diving off the bottom) | leg file | 316c |
| 95CF | sprite_hit_dist | u16 | 80h: collision radius on the depth key | leg file | 33f7 |
| 95D1 | coll_prefilter | u16 | per-axis prefilter distance for 5ad4 | leg file | 5ad4 |
| 95D5 | lighthouse_radius | u8 | beam radius /16 (model units): **not** a speed | leg file | 5df9 |
| 95D6 | gate_arm_len | u8 | arm length (model units): **not** a speed | leg file | 5f57 |
| 95D9 / 95DD | point_sizes / line_widths | u8[4] / u8[4] | world size of points / thick lines by `w0` bits 11–12 | leg file | 66ad, 81c7 / 67af, 82d6 |
| 95E1 | sprite_flags | u8[32] | bits 0-2 copy mode (0 own steps, 1 one 1:1 copy, 2 = use id-1's copies, ≥3 = use id-2's), bits 3-7 world size: `size = (f & F8h) << 3` | leg file | 2810, 28eb, 2e49 |
| 9611 | sprite_anim_next | u16[32] | per sprite: new id (low 6 bits) + countdown (bits 8-12) when the countdown expires | leg file | 3352 |
| 9A71 | sprite_count | u16 | static instances from the leg file (0..8 are special, see below) | leg file | 3085, 72f8 |
| 9A73 | sprite_id | u16[320] | bits 0-5 sprite, 6/7 drift Z/X, 8-12 countdown, 13-15 collision class | leg file, 72f8, 3352, 33f7, 0ab4, 6e92 | many |
| 9A83,9D03,9F83,A203 | sprite[8] type/x/z/y | | sun (5) / moon (4) / none (0) placed at camera ± 0x900 | 6e92 | sprites |
| 9CF3 / 9F73 / A1F3 | sprite_x / _z / _y | s16[320] | position units (X, Z), height units (Y). Instance 0 = the player car | … | 3085, 316c, 2b74, 3352 |
| 9D01,9F81,A201 | sprite[7] x/z/y | | crash object in replay (94B2==2) | 6e92 | sprites |
| A473 | obj_count | u16 | objects n | leg file | 4d6d, 4631 |
| A475 | obj_moving_end | u16 | moving objects are 0..A475−1 | leg file | 509b, 5ad4, 5e70, 5ffe, 61d2, 03d4 |
| A477 | obj_first_static | u16 | 0..A477−1 non-static | leg file | 4fc7 |
| A479[i] | obj_flags | u16 | bits 0–5 model, 6–7 speed class, 8–11 behaviour, 800h (set by 5ad4), 1000h hold/parked, 2000h moving, 4000h police pursuit / obeys, 8000h stop | leg, 5466, 5ad4, 4631, 5ffe, 61d2, 03d4 | all |
| A5B9/A6F9[i] | obj_x / obj_z | u16 | position (X 0..7FFF, Z 0..3FFF) | 509b, 2465 | many |
| A839[i] | obj_y8 | u16 | height ×8 | 509b, 2465 | 52a3, 539d, 5ad4 |
| A979[i] | obj_heading | u16 | hi = heading (256/turn), lo = target | 5466, 2465 | 52a3, 539d, 5ad4, 5f57, 4db6, 5ffe |
| AAB9[i] | obj_pitch_vy | u16 | hi = pitch shift → `BD38`; lo = s8 vertical speed | 5466 | 509b, 52a3 |
| ABF9[i] | obj_vel | u16 | lo = s8 vx, hi = s8 vz (per frame, position units) | 5466, 4631 | 509b, 5ad4 |
| AE79[i] | obj_path_state | u16 | bits 6–15 countdown (steps of 40h), lo = waypoint | 5466, 5ffe, 4631 | 5466 |
| AFB9/B0F9[i] | obj_vert_base / obj_vert_count | u16 | vertex range this frame | 75c1, 509b, 4fc7 | 509b, 539d, 52a3, 5ffe |
| B239/B379[i] | obj_face_base / obj_face_count | u16 | face range this frame (0 count = not drawn) | 7487, 509b, 4fc7, 539d | 4d6d, 4631, 5df9, 5f57, 5ad4, 5e70, 5ffe, 0f31 |
| B4B9 / B6B9 | colour_pairs / colour_direct | u16[256] / u8[16] | VGA colour tables (leg file) | leg file, 0792:0fce | 7487, 373c |
| B6D1 | mirror_dirty | u8 | copy M once more (after switching mirror off / layout restore) | 04ca, 083c, 7c21 | 7c21, 17cd (clears) |
| B6D2 | mirror_on | u8 | rear-view mirror pass enabled (key R) | 012d | 3a9e, 7b9b, 7c21, 17cd |
| B6D4 | replay_paused | u8 | F9 during replay | 059c | 0ab4:1220 |
| B6D8 | headlights | u8 | 'H' | 030f, 255e | 4b37 |
| B6DC | half_window | u8 | F1: 240×64 view | 031c | 409c, 13d8, overlays |
| B6DD | detail | u8 | F2: 0 low, 1 medium, 2 high (visible cells, gradient sky, sprite/vehicle ranges) | 03ab | 70cd, 373c, 03d4 |
| B6DE / B6E0 / B6E2 | view_half / ×2 / ×4 | u16 | 300h / 440h / 700h by detail (B6E2 also limits sprite depth, next row) | 03d4 | 4fc7, 509b, 323e |
| B6E2 | sprite_far_limit | u16 | max depth key for sprites with id low byte > 5: C00h/1100h/1C00h by detail `B6DD` | 03d4 | 323e |
| B707 | race_clock_running | u8 | 0 = objects don't move (cockpit) | 0b1d etc., 2465 | 509b |
| B70B / B70C | clock_sec / clock_min | u8 | player's race clock | race, 5ffe (+20 s), 255e | 624a |
| B710 | radar_level | u8 | max police closeness this frame (0–6Fh) | 5ffe | 0db4 (detector) |
| B78D | crashed | u8 | crash in progress | 0f31 | 77d5, 4bf3, race_run |
| B78F / B79F / B7AF | oct_y / oct_half / oct_base | u16[8] | octant tables for 394c/39fd/316c (B7AF set by 409c) | image / 409c | projection |
| B7CF / B851 | sin64 / cos64 | s16[65] | Q15 sin/cos, 64 steps per quarter | image | 7653, 6604 |
| B8D3 / B993 | row_ofs / mrow_ofs | u16[60h] / u16[13h] | row → byte offset in V / M | 25f4 | span writers |
| B9CB/CD/CF/D1, B9D3 | restart_x/z/y/heading, restart_flag | u16/u8 | restart point used by 2465 when B9D3 or B78D | simulation | 2465 |
| B9D4 | spr_vis_w | u8 | visible width after horizontal clip | 2b74, 2999 | 2c7c, 2a9e |
| B9D5 | spr_view | u8 | `hi(8C92[k]) + 8`; bit 7 = mirror | 323e | 2e49, 28eb |
| B9D6 | spr_scale | u8 | scale value (≤ 17h shrink, 18h..2Fh grow) / apparent size at draw | 2810, 2e49 | 2eb6, 28eb |
| B9D7 | spr_grow | u8 | number of 24-step octaves above 1:1 (0 shrink, 1 grow; 2, 3 unreachable) | 2810, 2eb6 | 289a, 2dc6 |
| B9D8 | spr_num | u8 | current sprite id (316c stores the full low byte, 2e49 the id & 3Fh) | 2810, 316c, 2e49 | 289a, 28eb, 316c |
| B9D9 | pad | u8 | `(w − m) >> 1` for the row being scaled | 2f84, 3004 | same |
| B9DA | rows_out | u8 | output rows of the copy being built | 2d54, 2dc6 | same |
| B9DB | spr_h | u16 | H of the copy being drawn (low byte written; B9DC is never written, 0) | 28eb | 2b74, 2999 |
| B9DE | rep_count | u8 | remaining repeats of the current stored row | 2b74, 2999 | 2c7c, 2a9e |
| B9DF | clip_left | u16 | pixels clipped on the left (0 = not clipped) | 2b74, 2999 | 2c7c, 2a9e |
| B9E1 | rep_ptr | u16 | cache offset of the current repeat byte | 289a, 2d54, 2dc6, 28eb, blitters | same |
| B9E3 | copy_hdr | u16 | cache offset of the copy's (W, H) bytes | 289a | 2810, 2d54, 2dc6 |
| B9E5 | cache_limit | u16 | D000h | 2789 | 2789, 2d54, 2dc6 |
| B9E7 | copy_h | u16 | h byte (length of the repeat table) of the copy being drawn | 28eb | 28eb |
| B9E9 | last_wh | u16 | (W, H) of the previous step (dedup) | 2810 | 2810 |
| B9EB / B9ED | sprite_cache_seg / _seg2 | u16 | normalised segment of the cache: `(E53C >> 4) + 1 + E53E`; B9ED = copy set per draw | 2789 / 28eb | 28eb, 2a9e, 2c7c |
| B9EF..BA08 | hmask | u8[26] | horizontal keep mask; bytes 0-6 = entry bytes 0-6, repeated (only bytes 0-6 are reachable: w ≤ 54) | 2eb6 | 2f84, 3004 |
| BA0A..BA0C | vmask | u8[3] | 24-bit vertical keep mask (rotating) | 2eb6 | 2d54, 2dc6 |
| BA0D | sprite_max_size | u8 | `BA3F[95BF-1]` (1Ah / 17h / 12h) | 2789 | 28eb |
| BA0E | size_to_step | u8[31h] | apparent size (0..30h) → step index (table below) | static | 28eb |
| BA3F | step_to_size | u8[17] | step → scale value B9D6 used when building (table below) | static | 2810 |
| BA55 | world_rebuilt | u8 | set when 70cd rebuilt the lists (also F5/replay): sprite list rebuild + full face sort | 70cd, 026f, 4c51 | 323e, 361c |
| BA56 | veg_seed | u16 | vegetation thinning bits (by detail: all / 5555h mask / 0) | 03d4, 255e | 72f8 |
| BA56, B6ED | sprite_seed, seed | u16 | B6ED = random seed; BA56 = B6ED & {0, 5555h, FFFFh} by detail | 255e, 03d4 | 72f8 |
| BA58 / BA5B / BA5E | debris_vx / debris_vz / debris_h | s8[3] / s8[3] / u8[3·12] | crash debris velocities and height profile | static | 3352 |
| BA82 | msg_protect | u8 | 1 after a message box: present skips 15h rows | 0000:179c | 13d8; 373c clears |
| BA83 | roof_height | u16 | ceiling face above the camera (bridge types 14h–17h out of clearance) | 373c (0), 3a9e | 46dc |
| BA85 | ground_height | u16 | highest ground face under the camera this frame (0 = none) | 373c (0), 3a9e | 6421, 77d5, 026f |
| BA85 / BA87 | ground_y / ground_y_last | u16 | height of face under camera this / last frame (0 = none) | 373c, 3a9e / 6421 | 6421, 77d5, 026f |
| BA89 / BA8B / BA8D | ground_tri | u16×3 | its vertices (byte offsets) | 3a9e | 6421 |
| BA8F | horizon_base | u16 | horizon row at zero pitch (38h / 28h) | 409c | 76ec |
| BA91 | view_rows | u16 | 60h / 40h / 56h | 409c | spans, 13d8 |
| BA93 | view_bytes | u16 | `BA91·140h` | 409c | sprites |
| BA95 | view_w32m | u16 | (width−1)·32 | 409c | 373c, 13d8 |
| BA97 | view_cx32 | u16 | view centre x in 1/32 px | 409c | 39fd, 373c, sprites |
| BA99 | view_cx_hi | u8 | view centre / 100h (14h / 0Fh / 10h) | 409c | 66ad |
| BA9A | face_type | u8 | `w3_hi & F8h` of the face being drawn (70h for sky/ground/overlays, 0 = OR mode) | 323e, 373c, overlays | 41d4, 3a9e |
| BA9B | surface_under_car | u8 | ground type of the face around the camera (FFh = none); ≥ 1Ch sticks | 373c (FFh), 3a9e | race_run, sim |
| BA9C | span_top | s16 | first row of the current fill | setups | fill routines |
| BA9E | sky_top | s16 | top row of the sky pass | 373c | 373c |
| BAA0 / BAA2 / BAA4 | sky_pair_ega / sky_pair / ground_pair | u16 | colours of the sky / ground this frame (BAA2/BAA4 reused by the mirror) | 373c | 7b9b |
| BAA6 | sky_flash | u8 | flash frames left | 4fc7, 5ffe | 373c |
| BAA7 | bump_ctr | u8 | 3 / 5 after knock-downs | 4590, 4631 | 0977 |
| BAA8 | crash_req | u8 | crash requested this frame (cleared by 76ec) | 77d5, collisions | 77d5 |
| BAA8 / BAAE / BAA9 | crash / crash_type / crash_face | u8/u8/u16 | set by 34f1, 5ad4 (`\|= 1`); BAA9 = face index | 76ec (reset), 34f1, 5ad4, 3a9e | 77d5, 0f31 |
| BAA9 / BAAB | crash_id / crash_obj | u16 | collision id / object index×2 hit | collisions, 76ec(FFFF) / 0f31 | 0f31 / 6e92 |
| BAAD | water_grace | u8 | frames of water-crash immunity after respawn (2 with debug B6EA) | 77d5 | 77d5 |
| BAAF / BAB0 | reload2 / reload3 | u8 | sides reloaded at segment 2 / 3 (bit 0 left, bit 1 right) | setups | 41d4 |
| BAB1 | colour | u16 | colour pair of the current fill (low byte on (x^y) odd) | 323e, 373c, overlays | span writers |
| BAB3 / BAB5 | dxl / dxr | s16 | per-row x decrement of the left / right edge (1/32 px) | setups | 41d4, 43db |
| BAB7 / BAB9 / BABB | around_sorted | u16×3 | vertices sorted by x (3ebf) | 3ebf | 3ebf |
| BABD / BABF | dxl3 / dxr3 | s16 | segment-3 slopes | 69da | 41d4 |
| BAC1 / BAC3 / BAC5 | rows1 / rows2 / rows3 | u16 | row counts of the three segments | setups | 41d4 |
| BAC7 / BAC9 / BACB / BACD | xl2 / xr2 / xl3 / xr3 | u16 | x at the start of segment 2 / 3 | setups | 41d4 |
| BACF | plane_above | u8 | 1 = the face around the camera is above the eye (fill upwards) | 6316, 3a9e | 3ebf |
| BAD0 | face_ptr | u16 | current face record offset (overlays: DS offsets of fake records) | 323e, overlays | 3a9e, 69da |
| BAD2 | quad_top_word | u16 | record word offset of the top vertex | 69da | 69da |
| BAD4 / BAD6 | top_row / top_row_prev | s16 | topmost row touched this / previous frame (present skips rows above the min) | 34c3 (60h), 39fd, 373c, sprites, overlays / 13d8 | 13d8, 373c |
| BAD8 / BADA | face_count / vert_count | u16 | emitted faces (≤ 640h) / vertices (≤ 630h) | 70cd, 7487, 75c1, 4d60 tree | everything |
| BADC / BADE | static_face_end / static_vert_end | u16 | end of the world part (vehicles appended after) | 70cd | 4fc7 |
| BAE0 / BAE2 | parked_face_end / parked_vert_end | u16 | end of parked part | 4fc7 | 509b |
| BAE4 | clip_patch_ptrs | u16[24] | CS addresses of the immediates patched by 409c | image | 409c |
| BB14[16] | precip_offsets | u16 | const pseudo-random offsets for flakes/streaks | const | 477c, 47aa |
| BB34 | drop_ring_idx | u8 | ring index 0..63 | 47fc, 46dc | |
| BB35[64] | drop_state | u8 | bits7-6 type (00 bug brown, 40 rain, 80 snow, C0 black), bits5-0 age; 0 = empty | 47fc, 487e, 4a5c, 46dc | 487e |
| BB75[64] | drop_pos | u16 | V byte offset (rand>>1) | 47fc | 487e |
| BBF5[64] | drop_key | i16 | wiper angle key; 0x8000 = unwipeable | 47fc | 4a5c |
| BC75 / BC77 | rain_level / snow_level | u16 (0..8) | set per road section by race_run (±4 steps) | race_run, 05e3/0606, 255e | 46dc, 4b37 … |
| BC7B / BC7C | wipers_on / wiper_phase | u8 | switch; phase 0, 1..4 out, 83..81 back | 0302 / 4a5c | 4a5c |
| BCA5 | ticket_speed | u8 | `E55F` at pursuit start | 5ffe | (HUD) |
| BCA6[3] | tickets | u8 | tickets of player / opp1 / opp2 | 5ffe, 255e | 624a |
| BCA8[3] | opp_hold_ctr | u8 | [1],[2] used: frames standing (→1Eh) | 509b, 255e | 509b |
| BCAA[3] | opp_go_timer | u8 | [1],[2]: frames allowed to move (i·10h+1Eh) | 509b, 255e | 509b, 5466 |
| BCAD, BCAE, BCAF | obj_timers | u8 | saturating per-frame countdowns (BCAF used by 5466) | 509b, 5466, 255e | 5466 |
| BCB0 | pending_msg | u8 | message to show when sky flash ends (21h caught, 2Ch opponent ticketed) | 5ffe | 373c |
| BCB1 | police_turn_slot | u16 | police car that must turn round | 5ffe, 255e | 5466 |
| BCB3 | beacon_colour | u16 | 9 → 0Ch → 0Fh → 9 (+3 per beacon face) (initial 0) | 4db6 | 4db6 |
| BCB5 | police_cooldown | u8 | 96h after a catch | 5ffe, 255e | 5ffe |
| BCB6 | pullover_ctr | u8 | frames with a police car very close; 1Eh = caught | 5ffe, 61d2 | 5ffe |
| BCBB[2][3] | opp_time | u8 | {frac, sec, min} of opp1 (BCBB) / opp2 (BCBE) | 624a, 255e | 61fd |
| BCC1 | strobe_counter | u8 | +1 per frame | 4d6d | 4db6, 5466 |
| BCC3 | obj_full_transform | u8 | 1 = do not use the translated-vertex cache | 509b, 5466 | 52a3 |
| BCC4/BCC6/BCC8 | obj_move_dx/dz/dy | s16 | this frame's movement (dy: see 509b); scratch = box centre in 5ad4 | 509b, 5ad4 | 52a3, 5cf2 |
| BCCA/BCCC | saved_vert_base/count | u16 | object's AFB9/B0F9 before 52a3 | 509b | 509b |
| BCCE / BCCF | tlight_timer / tlight_phase | u8 | phase 0–3; timer 5Ah (even phase) / 9 (odd) frames | 4d6d, 2465 (BCCE=1) | 4db6, 5466 |
| BCD0 | tlight_lamp | u8 | running index 0–5 of type-11 faces this frame | 4d6d, 4db6 | 4db6 |
| BCD1 | wiper_frame | u8 | +1 per frame | 4d6d | 4db6 |
| BCD2 / BCD4 / BCD6 | lighthouse_slot / gate_slot1 / gate_slot2 | u16 | object slots noticed by 7487 (FFFFh = none) | 70cd, 7487 | 5df9, 5e70 |
| BCD8 | gate_angle | u8 | 0 = arm up … 40h = arm down; ≠0 → flashers active | 5e70, 2465 | 5f57, 4db6, 5466, 33f7, 34c3(sfx) |
| BCD9 | train_near | u8 | 1 = train within 240h of gate 0 | 5e70, 2465 | 5e70 |
| BCDA | train_min_dist | u16 | min train distance (reset to FFFF only when no gate) | 5e70 | 5e70 |
| BCDC / BCDD | terrain_roll_code / terrain_pitch_delta | i8 / i16 | from 6421 for physics | 6421 | 0977:0008 |
| BCDF | plane_height | s16 | 6316 result | 6316 | – |
| BD21 | sprite_children | u8 | sprite children appended this rebuild (≤ F0h) | 70cd, 72f8 | 72f8, 3085 |
| BD22 | vert_overflow | u8 | 1 = vertices did not fit: skip faces | 72f8, 75c1 | 72f8 |
| BD23 | faces_resort | u8 | full re-sort requested | 70cd, 509b | 361c |
| BD24 | model_face_cut | u16 | faces not emitted (tile alias) | 72f8 | 7487 |
| BD26 | cur_obj_slot1 | u16 | object slot·2+2 of the model being emitted, 0 = tile | 70cd, 52a3, 539d | 75c1, 7487 |
| BD28 | model_ptr | u16 | offset of the model header | 72f8 | 72f8 |
| BD2A / BD2C / BD2E | place_x / place_z / place_y | u16 | model origin (position units; y = height) | 70cd, vehicles | 75c1, 72f8 |
| BD30 / BD32 | place_x4 / place_z4 | u16 | origin ·4 | 75c1 | 75c1 |
| BD34 / BD36 | last_cell / last_octab | u16 | cache key of the visible-cell list (FFFFh forces a rebuild) | 70cd, 031c, 03ab | 70cd |
| BD38 | place_shear | s8 | pitch shear shift (0 = none) | 70cd, vehicles | 7653 |
| BD39 | frame_counter | u16 | +1 per frame_update | 76ec | 323e (blink), input |
| BD3B / BD3C | water_crash / water_crash_anim | u8 | crash into water | 77d5 | race_run, 4bf3 |
| BD3D | mirror_base | u16 | `7500h + BA99·100h`; mirror x = (1600h − (x − BD3D)) >> 1 | 409c | mirror rasterizers, 28eb |
| BD3F | mirror_invalid | u8 | M clobbered (set by the LZ decompressor 0ab4:0047) → skip copy, 7c21 clears grey | 0ab4:0047, 7b9b(0), 7c21(0) | 7c21, 17cd |
| CC5C / CC5E | view_block | far ptr | malloc(7810h) for V | 0000:11d2 | 2537 |
| CC8E / CEB8 | key_ofs / order_ofs | u16 | offsets (in seg E5BA) of the face key array (E5B8+4B00h) and order array (E5B8+3E80h) | 0000:11d2 | 361c, 323e |
| CC92 | no_mirror_layout | u8 | replay/crash layout: no mirror, no hole, no mirror frame, no mirror 3D pass | race_run, 04ca(0), 059a(1), 01f4:144c | 13d8, 17cd, 7c21, 7b9b, 044e |
| E338 | video_mode | u16 | 13h = VGA (mode 13h), 0Dh EGA, 09h Tandy | config_load | 7487, 373c, present |
| E53C / E53E | sprite_cache_far | far ptr | allocated buffer (D010h bytes per world.md) | loader | 2789 |
| E55F | car_speed_disp | u8 | player speed (units Unknown; ≥ 0Ch triggers pursuit) | simulation | 5ffe |
| E5B0 / E5B2 | remap_ptr | far ptr | colour-code remap (E5B8+5780h) | 0000:11d2 | 261c, 7487 |
| E5B8 / E5BA | face_block | far ptr | malloc(57B0h): faces 640h×10, order, keys, remap | 0000:11d2 | 7487, 361c, 323e |
| E776 | adapter | u16 | 16cf:0006 result; 0x13 → no shake (MCGA?) | config_load | 0fa1 |
| E7DC / E7DE | mirror_block | far ptr | DOS block 300h paragraphs (also the LZ dictionary) | 0ab4:000f | 2537 |
| E7F4 + 1Bh·k | results_rec[k] | 27-byte records | +0 min, +1 sec, +2 frac, +8/+9 hi/lo of `95AB` (k=1) / `95AD` (k=2) | 61fd | 01f4:4227 (results) |

-----------------------------------------------------------------------------------------------

## 4. Pseudocode

### 4.1 Conventions used in the pseudocode

* `u8/s8/u16/s16/u32/s32` are exact widths. All arithmetic is 16-bit and wraps unless a line says otherwise.
  `NEG(v)` = `(s16)v < 0` (sign bit of a 16-bit result: the original's `cmp a,b / js` is `NEG(a-b)`, which is
  **not** a signed compare when the subtraction overflows — keep it). `>>` on `s16` is `sar`, on `u16` `shr`.
* Vertex arrays are indexed by the vertex index `v` (the original uses the byte offset `2v`): `vy[]` DS:2502,
  `vx[]` DS:3182, `vz[]` DS:3E02 (world space), `sx[]` DS:4A82, `sy[]` DS:5702, `depth[]` DS:6382,
  `dist[]` DS:7002, `my[]` DS:7C82. Each is 640h words (the arrays are 0C80h bytes apart).
* `F16(o)` = word at `face_seg:o` (far face buffer, `DS:E5BA`). A face record is 10 bytes at
  `E5B8 + 10·n`: `+0 w0` (count/flags + v0), `+2 v1`, `+4 v2`, `+6 w3` (type + v3), `+8` colour pair.
* Clip constants that `409c` patches into the code are written as variables: `W32 = BA95 + 20h` (view width·32),
  `W32M = BA95` ((width−1)·32). The front-view visibility bounds `2800h`/`5400h` are **not** patched.

### 4.2 View setup, buffers, colour remap

```c
// 0e12:409c view_setup (far) — called by stage load 0792:0d2e and the F1 handler 0e12:031c
void view_setup(void) {
    u16 w32m, cxhi, rows, base;
    if (attract_mode /*09C4*/)      { w32m = 0x1FE0; cxhi = 0x10; rows = 0x56; base = 0x38; } // 256 x 86
    else if (!half_window /*B6DC*/) { w32m = 0x27E0; cxhi = 0x14; rows = 0x60; base = 0x38; } // 320 x 96
    else                            { w32m = 0x1DE0; cxhi = 0x0F; rows = 0x40; base = 0x28; } // 240 x 64
    BA8F = base;                       // horizon row with zero pitch
    BA91 = rows;
    BA93 = rows * 0x140;               // (rows<<8) + (rows<<6)
    BA99 = (u8)cxhi;                   // view centre x in 256-unit steps (x = cxhi·100h / 32 px = 8·cxhi px)
    BD3D = 0x7500 + (cxhi << 8);       // mirror angle base
    BA95 = w32m;
    u16 W32 = w32m + 0x20;
    // self-modifying: 24 immediates (CS addresses listed at DS:BAE4[24]) are rewritten; the port keeps
    // them as variables (see 4.8): W32 at 4220 4234 4418 442c 435d 4371 3eee 3f1b; W32+8000h at 4226 441e
    // 4363; W32M at 422b 423d 4423 4435 4368 437a 452f 453a 4543 4546; W32M+8000h at 4535;
    // W32>>5 (width in px) at 2b94 2b9b (sprites).
    BA97 = W32 >> 1;                   // view centre x in 1/32 px (1400h / 0F00h / 1000h)
    u16 c = cxhi << 8;                 // octant base angles for 394c / 316c
    B7AF[0] = c;          B7AF[1] = c + 0x8000; B7AF[2] = c + 0x8000; B7AF[3] = c;
    B7AF[4] = c + 0x4000; B7AF[5] = c + 0x4000; B7AF[6] = c + 0xC000; B7AF[7] = c + 0xC000;
}
// F1 (031c): half_window ^= 1; BAD6 = 0; message 0Ah/0Bh; view_setup(); if BA91 != 60h paint the unused
// screen parts black with the library (page 0, colour 0, rects (0..27h,10h..6Fh) (118h..13Fh,10h..6Fh)
// (28h..117h,50h..6Fh)); restore page DS:009A; BD34 = FFFFh (forces a world rebuild).

// 0e12:2537 frame_buffers_init (far, main): the far blocks are normalised to offset 0
view_seg   = (CC5C >> 4) + 1 + CC5E;   // 7810h-byte malloc: 3D view buffer "V", 320 x 96 = 7800h bytes
mirror_seg = (E7DC >> 4) + 1 + E7DE;   // DOS block of 300h paragraphs (0ab4:000f): mirror "M", 88 x 19

// 0e12:25f4 view_row_tables_init
for (r = 0; r < 0x60; r++) row_ofs[r]  = r * 0x140;   // DS:B8D3
for (r = 0; r < 0x13; r++) mrow_ofs[r] = r * 0x58;    // DS:B993

// 0e12:261c build_colour_remap (far): remap[0..31] at face_seg:E5B0 (inside the face block, +5780h)
void build_colour_remap(void) {
    for (u8 c = 0; c < 32; c++) {
        u8 al = c, dl;
        if (colour_mode /*95C7*/ && (u8)rain /*BC75*/ && c == 8) { al = 0; goto store; }   // rain by day: 8 → 0
        dl = 0x0F;
        if (!colour_mode) {                  // dark legs / night
            dl = 7;
            if (c == 8) al = 0; else if (c == 7) al = 8;
        }
        if ((u8)snow /*BC77*/ && (c == 3 || c == 0x0A || c == 2)) al = dl;          // snow: white (grey at night)
    store:
        remap[c] = al;
    }
}
// Callers: race_run (surface 1Ch..1Eh weather ramps), keys T/S/N (debug), 2465 (restart).
```

Colour pair of a face (built in `7487`, VGA path `E338 = 13h`): `c1 = remap[w1>>11]`, `c2 = remap[w2>>11]`;
both < 16 → pair = `B4B9[(c2<<4)|c1]` (word); otherwise `lo = c1<16 ? c1 : B6B9[c1&15]`,
`hi = c2<16 ? c2 : B6B9[c2&15]` (a code < 16 left alone becomes the raw EGA index). Non-VGA: `pair & 0F0Fh`.
The rasterizer writes the **low byte to pixels with (x ^ y) odd and the high byte where (x ^ y) is even**
(fill_spans parity test) — a checkerboard of the two colour codes.

### 4.3 Camera (frame_update tail) and world coordinates

```c
// 0e12:76ec frame_update (far) — full order
void frame_update(void) {
    frame_counter++;                         // BD39
    BAA8 = 0; BAA9 = 0xFFFF;                 // crash request / crash face id
    screen_shake_step();                     // 0fa1
    auto_shift();                            // 095a   (simulation)
    engine_sound();                          // 23df   (simulation)
    if (!attract_mode) sim_0b1d();           // 0b1d   (simulation)
    B9484++;
    if (!freeze /*948C, always 0*/) {
        if (!ext_camera /*948B*/) car_simulation();       // 0977:0008
        replay_recorder();                                // 0ab4:1220
    }
    camera_pos_update();                 // 6e92: cam_x/z, cam_x4/z4, sun/moon sprite 8 (see 4.12)
    camera_select();                         // inline 7733..77b7 (see 4.12): cam_heading 9498, cam_row 9496,
                                             //   cam_roll 94A0, cam_y 949E; sprite drift signs 95B7/95B9
    set_page(DS[0x9A] = 1);                  // 1714:000e
    world_build_visible();                   // 70cd
    scene_prepare();                         // 34c3
}
```

Coordinate systems (all verified):

| Quantity | Unit |
|---|---|
| world X, Z of positions (`949A`, object X/Z) | position units, cell = 400h |
| vertex `vx`, `vz` and camera `cam_x4 = 94A1`, `cam_z4 = 94A3` | position·4 (+2 fraction bits for the camera from `9494`/`9495` bits 7–6); cell = 1000h |
| vertex `vy`, camera `cam_y = 949E` | height units = model units (car eye height `94BF` = 2Eh above the car Y `A1F3`) |
| heading (`9498`, `9491`) | u16, 10000h = 360°, 0 = +Z (north), 4000h = +X (east); objects/tiles use the high byte only |
| screen x `sx[]` | u16 angle in 1/32 px: 4000h = 90° = 512 px, so the 320-px view spans 56.25°; the view centre is `BA99<<8` (1400h) |
| screen y `sy[]` | s16 pixel row of V; 256 px = 45° (same angular scale as x) |
| roll `cam_roll = 94A0` | s8 shift count: row offset = `±(x − centre) >> |roll|`; 0 = level |

The projection is **angular (cylindrical)**, not perspective: x is linear in the bearing angle, y linear in the
elevation angle. There is no view matrix; the camera only offsets bearings by its heading and rows by its pitch.

### 4.4 World build (0e12:70cd → 72f8 → 75c1/7653/7487)

```c
// CS tables in 0e12 (image 0E120h + off):
//  7004: 25 x {s8 dX, s8 dZ, u16 dCell} — 5x5 grid of cells around the car, index = row*5+col,
//        row 0 = 2 cells north (dZ=+8), col 0 = 2 cells west (dX=-8); dX/dZ in X_hi/Z_hi units (4 = 1 cell);
//        dCell low 5 bits = column delta (mod 32), (dCell ^ (dCell & 1Fh)) = row delta·32 (mod 512).
//  7068: 8 x 10 grid indices, one row per heading octant, nearest/most relevant first:
//        o0: 0C 07 02 11 0B 0D 06 08 01 03   o1: 0C 08 10 07 0D 0B 11 03 09 04
//        o2: 0C 0D 0E 0B 07 11 08 12 09 13   o3: 0C 12 06 0D 11 07 0B 13 17 18
//        o4: 0C 11 16 07 0D 0B 12 10 17 15   o5: 0C 10 08 11 0B 0D 07 15 0F 14
//        o6: 0C 0B 0A 0D 11 07 10 06 0F 05   o7: 0C 06 12 0B 07 11 0D 05 01 00
//        (0Ch = the car's cell; the 4th entry is the cell behind, for the mirror).
void world_build_visible(void) {
    u16 tab = (((cam_heading + 0x1000) >> 8) & 0xE0) >> 2;   // octant·8
    tab += tab >> 2;                                          // octant·10
    u16 cell = (X_hi(cam_x) >> 2) + ((u8)(0x0F - (Z_hi(cam_z) >> 2)) << 5);   // 949B, 949D
    u16 t = leg_map[cell];                                    // 9671
    u8 type = (u8)t;
    if (!detail /*B6DD*/ && type < 0x77 && !(type >= 0x70 && type <= 0x73)) {
        u16 q = (((cam_heading + 0x2000) >> 8) & 0xC0) >> 2;  // quadrant·16
        tab = q + (q >> 2);                                   // quadrant·20 = the even octant rows
    }
    if (BD34 == 2*cell && BD36 == tab) {                      // nothing changed: only the route id
        if (type == route_tiles[0]) route_index = 0;          // 95C9..95CB → 0A74
        else if (type == route_tiles[1]) route_index = 1;
        else if (type == route_tiles[2]) route_index = 2;
        return;
    }
    BCD2 = BCD4 = BCD6 = 0xFFFF;                              // lighthouse / gate object slots
    BA55 = 1;                                                 // sprite list must be rebuilt
    BD34 = 2*cell; BD36 = tab;
    BD23 = 1;                                                 // face list must be fully re-sorted
    obj_ranges_clear();                                       // 70b8
    face_count /*BAD8*/ = 0; vert_count /*BADA*/ = 0; BD21 = 0;
    int n;
    if (attract_mode) n = 3;
    else if (detail > 1) n = 10;
    else if (detail == 1) n = 6;
    else n = (type >= 0x77 || (type >= 0x70 && type <= 0x73)) ? 6 : 3;
    u8 cxh = X_hi(cam_x) & 0x7C, czh = Z_hi(cam_z) & 0x3C;   // dh, dl
    for (int k = 0; k < n; k++) {
        const u8 *e = CS + 0x7004 + 4 * CS[0x7068 + tab + k];
        u8 ch = (cxh + (s8)e[0]) & 0x7C, cz = (czh + (s8)e[1]) & 0x3C;
        u16 d = *(u16 *)(e + 2);
        u16 c = cell;                                          // column wrap, then row wrap
        c = (c & 0x1E0) | ((c + (d & 0x1F)) & 0x1F);
        c = (c + (d ^ (d & 0x1F))) & 0x1FF;
        BD26 = 0;                                              // "no object"
        u16 m = leg_map[c];
        place_y /*BD2E*/ = m & 0x3F00;  place_rot /*946A*/ = (m >> 8) & 0xC0;  BD38 = 0;
        place_z /*BD2C*/ = (cz + 2) << 8; place_x /*BD2A*/ = (ch + 2) << 8;
        model_place((u8)m, 0);                                 // tile
        for (int i = obj_count - 1; i >= obj_first_static; i--) {      // static objects in that cell
            if ((X_hi(obj_x[i]) & 0x7C) != ch) continue;
            if ((Z_hi(obj_z[i]) & 0x3C) != cz) continue;
            place_x = obj_x[i]; place_z = obj_z[i]; place_y = obj_y8[i] >> 3;
            u16 f = obj_flags[i];
            if (f & 0x3000) continue;                          // moving / parked: drawn by 4d60
            if ((f & 0x3F) == 0) continue;
            place_rot = (obj_heading[i] >> 8) & 0xC0;
            BD38 = obj_pitch[i] >> 8;
            BD26 = 2*i + 2;
            model_place(f & 0x3F, 1);
        }
    }
    BADE = vert_count; BADC = face_count;                     // end of the static part
}
// Note: the static world (tiles + static objects + tile sprite children) is rebuilt only when the car's cell
// or the view octant changes; vehicles are appended every frame after BADE/BADC by 4d60 (objects section).

// 0e12:72f8 model_place(AL = model, AH = kind)
void model_place(u8 model, u8 is_obj) {
    BD24 = 0;                                                  // faces cut from an alias
    u8 far *set; u16 off;
    if (is_obj) { set = objects_set /*E54C*/; off = W(set + 2*model); }
    else {
        if (model >= 0x40) { set = tiles_scene /*E770*/; model -= 0x40; } else set = tiles_shared /*E7E0*/;
        off = W(set + 2*model);
        if (off <= 0x10) { BD24 = off; off = W(set + 2*model - 2); }   // alias of the previous model
    }
    u8 far *p = set + off; BD28 = p;
    u8 nf = p[0];
    if (nf) {
        u8 nv = p[1];
        BD22 = 0; B946B = nv;
        p = model_emit_vertices(p + 4, nv);                    // 75c1
        if (!BD22) model_emit_faces(p, nf);                    // 7487
    }
    // sprite children (tiles only have any): they follow faces
    p = BD28; u16 skip = p[0]*8 + p[1]*6; u8 nc = p[2]; p += 4 + skip;
    if (!nc) return;
    u8 base = BD21;
    if (base >= 0xF0) return;
    u8 end = base + nc;
    if (end >= 0xF0) { nc -= end - 0xF0; end = 0xF0; }
    BD21 = end; B946B = nc;
    u16 i = sprite_count /*9A71*/ + base;
    u16 seed = rol16(BA56, ((place_x + place_z) >> 8) >> 4);   // (X+Z)>>12 bits
    u8 r = place_rot;
    do {
        u16 id = W(p);
        if ((u8)id >= 0x12) {                                  // vegetation etc.: thinned per cell
            u16 carry = seed & 1; seed = ror16(seed, 1);
            if (!carry) { sprite_id[i] = 0; p += 8; goto next; }
        }
        sprite_id[i] = id;
        s16 x = W(p+2), z = W(p+4); p += 6;
        switch (r) {                                           // quarter turns only
            case 0x40: { s16 t = x; x = z; z = -t; } break;
            case 0x80: x = -x; z = -z; break;
            case 0xC0: { s16 t = x; x = -z; z = t; } break;    // r < 40h: none
        }
        sprite_x[i] = x + place_x; sprite_z[i] = z + place_z;
        sprite_y[i] = W(p) + place_y; p += 2;
    next:
        i++;
    } while (--B946B);
}
// (the seed rotation: the original rotates DI left by CL, then `ror di,1` per thinned child; the carry of each
//  ror decides. rol16/ror16 = 16-bit rotates.)

// 0e12:75c1 model_emit_vertices(ES:SI = &A[0], CX = nv) — returns SI after the C[] array
u8 far *model_emit_vertices(u8 far *p, u16 nv) {
    BD30 = place_x << 2; BD32 = place_z << 2;
    u16 base = vert_count; B945E = base;
    if (base >= 0x630 || base + nv >= 0x630) {                 // overflow: nothing emitted
        BD22 = 1;
        if (BD26 >= 2) { obj_vert_base[(BD26-2)/2] = 0; obj_vert_count[(BD26-2)/2] = 0; }   // AFB9/B0F9
        return p;                                              // (SI not advanced: callers don't use it then)
    }
    vert_count = base + nv;
    if (BD26 >= 2) { obj_vert_base[(BD26-2)/2] = base; obj_vert_count[(BD26-2)/2] = nv; }
    for (u16 v = base; B946B; v++, p += 2) {                   // loop count = 946B (== nv)
        s16 a, b, c; vertex_rotate(p, nv, &a, &b, &c);         // 7653: A[i], B[i] = p[2nv], C[i] = p[4nv]
        vy[v] = a + place_y; vx[v] = b + BD30; vz[v] = c + BD32;
        B946B--;
    }
    return p + 4*nv;                                           // skip B[] and C[]
}

// 0e12:7653 vertex_rotate
void vertex_rotate(const s16 far *A, u16 nv, s16 *oa, s16 *ob, s16 *oc) {
    s16 a = A[0], b = A[nv], c = A[2*nv];
    s8 sh = (s8)BD38;                                          // pitch shear (vehicles / static objects)
    if (sh) {
        s16 t = (s16)(b << 1) >> (sh < 0 ? -sh : sh);          // sar
        if (sh >= 0) t = -t;
        a += t;
    }
    u8 r = place_rot;                                          // 946A
    if (r & 0x3F) {                                            // fine rotation, Q15 tables
        u8 i = r & 0x3F;                                       // cos = DS:B851[i], sin = DS:B7CF[i]
        s16 nb = HI2(b, cosT[i]) + HI2(c, sinT[i]);            // HI2(p,q) = (s16)(((s32)p*q) >> 15)
        s16 nc = HI2(c, cosT[i]) - HI2(b, sinT[i]);            //   (imul; shl ax,1; rcl dx,1 → dx)
        b = nb; c = nc;
    }
    switch (r & 0xC0) {
        case 0x40: { s16 t = b; b = c; c = -t; } break;
        case 0x80: b = -b; c = -c; break;
        case 0xC0: { s16 t = b; b = -c; c = t; } break;
    }
    *oa = a; *ob = b; *oc = c;
}
// sinT (DS:B7CF) / cosT (DS:B851): 64 words (+1) = round(32767·sin/cos(i·90°/64)); both in the exe image.

// 0e12:7487 model_emit_faces(ES:SI = first face, CX = nf)
void model_emit_faces(const u16 far *f, u16 nf) {
    nf -= BD24;
    u16 vb = B945E;
    u16 n = face_count;
    if (n >= 0x640) { if (BD26 >= 2) obj_face_base[o] = obj_face_count[o] = 0; return; }   // B239/B379
    if (n + nf >= 0x640) nf -= (n + nf) - 0x640;
    face_count = n + nf;
    if (BD26 >= 2) {
        u16 o = (BD26 - 2) / 2;
        obj_face_base[o] = n; obj_face_count[o] = nf;
        if (obj_flags[o] == 0x10) { if (BCD4 == 0xFFFF) BCD4 = BD26 - 2; else BCD6 = BD26 - 2; }  // crossing gates
        if (obj_flags[o] == 0x0F) BCD2 = BD26 - 2;                                                // lighthouse
    }
    u16 d = E5B8 + 10*n;
    for (; nf; nf--, f += 4, d += 10) {
        u16 w0 = f[0] + vb, w1 = f[1] + vb, w2 = f[2] + vb, w3 = f[3] + vb;
        u8 c1 = remap[(w1 >> 11) & 0x1F], c2 = remap[(w2 >> 11) & 0x1F];
        F16(d+0) = w0; F16(d+2) = w1 & 0x07FF; F16(d+4) = w2 & 0x07FF; F16(d+6) = w3;
        u16 pair;
        if (E338 != 0x13) pair = ((c2 << 8) | c1) & 0x0F0F;
        else if (!(c2 & 0x10) && !(c1 & 0x10)) pair = B4B9[(c2 << 4) | c1];
        else { u8 lo = (c1 & 0x10) ? B6B9[c1 & 15] : c1, hi = (c2 & 0x10) ? B6B9[c2 & 15] : c2; pair = (hi << 8) | lo; }
        F16(d+8) = pair;
    }
}
```

### 4.5 Projection (0e12:394c, 39fd, 8b5f) and the CS trigonometry tables

CS tables of segment 0e12 (read them from `TDIII_unp.exe` at image offset `0E120h + off`; the port should
generate a C array from the exe once and embed it; three are exactly reproducible by formula, noted):

| CS off | Size | Content |
|---|---|---|
| `0FF2` | 130 × u16 | `sin(i·90°/128)·65535` rounded as in the exe (i = 0..129; not exactly `round()` — extract) |
| `10F6` | 256 × u16 | `cos(k·45°/256)·65535` (k = 0..255; extract) — `1/cos` for the distance |
| `12F6` | 2 × 513 × s16 | `A[i] = 32·min(255, floor(atan(i/512)·1024/π))`, i = 0..512 (1/32-px angle of the ratio i/512; 45° = 1FE0h); `A[513+i] = −A[i]` |
| `1AFA` | 2 × 513 × s16 | same in pixels: `B[i] = min(255, floor(atan(i/512)·1024/π))`, `B[513+i] = −B[i]` (verified: formula matches all 1026 entries) |
| `22FE..` | code | (0e12:22fe atan2 helper — objects section) |
| `267E` | 24 × 10 bytes | sprite scale masks (4.13) |
| `7004`, `7068` | 100 + 80 bytes | visible-cell tables (4.4) |
| `0F81` | 16 × 2 bytes | screen-shake offsets (overlays section) |

`B79F[o]` (DS) = byte offset `0` or `402h` (= 513 words) that selects the negated half; `B78F[o]` = `0, 0, 0, 0,
200h, −200h, 0, 0` (±90° in px for steep y); `B7AF[o]` = octant base angles (set by `409c`).

```c
// 0e12:394c vertices_project — all BADA vertices, every frame
void vertices_project(void) {
    for (u16 v = 0; v < vert_count; v++) {
        u16 o = 0;                                            // octant byte offset into B7AF/B79F
        u16 dx = vx[v] - cam_x4; if (NEG(dx)) { dx = -dx; o = 6; }
        u16 dz = vz[v] - cam_z4; if (NEG(dz)) { dz = -dz; o ^= 2; }
        u16 mn = dx, mx = dz, idx;
        if (dx < dz) { }                                      // ratio dx/dz, angle from the Z axis
        else if (dx == dz) { idx = 0x1FF; mn = dz; mx = dx; goto have; }   // (no swap, o unchanged)
        else { mn = dz; mx = dx; o ^= 8; }                    // ratio dz/dx, angle from the X axis
        idx = (mx == 0) ? 0 : ((u16)(((u32)mn << 16) / mx) >> 7) & 0x1FF;
    have:;
        u16 a = CS16(0x12F6 + 2*idx);
        sx[v] = B7AF_b[o] + CS16(0x12F6 + 2*idx + B79F_b[o]) - cam_heading;
        u16 cosv = CS16(0x10F6 + (a >> 4));                   // byte offset a>>4 → word a>>5
        dist[v] = (u16)(((u32)mx << 16) / cosv);              // mx / cos(angle); DIV overflows (INT 0) if mx >= cosv
        u16 dy = vy[v] - cam_y; if (NEG(dy)) dy = -dy;
        depth[v] = dy + dist[v];                              // painter key (|dy| + horizontal distance)
        sy[v] = 0x8000; my[v] = 0x8000;                       // rows computed on demand
    }
}
// Note on the equal case: when dx == dz the code keeps dx as "min" and dz as "max" (no swap, o not xored): the
// distance uses mx = dz (= dx). Range guard for the port: the division is 32/16 → 16 with trap on overflow; world
// distances inside the 5x5-cell window (< 5000h) are far below 46341, so it never traps; compute in 32 bits.

// 0e12:39fd vertex_project_y(v): front view row
void vertex_project_y(u16 v) {
    u16 o = 2, d = dist[v];
    u16 dy = vy[v] - cam_y;
    if (NEG(dy)) { dy = -dy; o = 0; }                         // o=2: vertex above the eye (row goes up)
    u16 num = dy, den = d;
    if (dy >= d) { num = d; den = dy; o ^= 8; }               // steep: 90° − atan(d/dy)
    u32 q = (u32)num << 9;
    u16 idx = ((q >> 16) >= den) ? 0x1FF : (u16)(q / den);    // (dy<<9)/d, clamped
    s16 y = CS16(0x1AFA + B79F_b[o] + 2*idx) + B78F_b[o] + cam_row;   // cam_row = 9496 = pitch + BA8F
    s8 r = cam_roll;
    if (r) {
        s16 t = (s16)(sx[v] - BA97) >> (r < 0 ? -r : r);
        if (r > 0) t = -t;
        y += t;
    }
    sy[v] = y;
    if (y < (s16)BAD4) BAD4 = y;                              // topmost row touched this frame
}

// 0e12:8b5f vertex_project_y_mirror(v): mirror row (19-row mirror, half vertical scale, centre row 0Ah)
void vertex_project_y_mirror(u16 v) {
    /* identical up to the table lookup: */ s16 y = CS16(0x1AFA + B79F_b[o] + 2*idx) + B78F_b[o];
    s8 r = cam_roll;
    if (r) y -= ((s16)(sx[v] - 0x9400) >> 1) >> (r < 0 ? -r : r);   // roll sign ignored; 9400h = fixed centre
    y -= car_pitch /*948F (not 9496!)*/;
    my[v] = (y >> 1) + 0x0A;                                   // sar
}
```

The mirror's horizontal coordinate is derived from the same `sx[]`: `mx = (s16)(0x1600 − (sx − BD3D)) >> 1`
(1/32 px, 0..0B00h = 88 px, left–right flipped, half scale). It is computed in each mirror rasterizer.

### 4.6 Face depth keys and sorting (0e12:361c, 7a35, 7b06)

The face list is `F16(order_ofs + 2i)` = far pointer offsets of the face records (`CEB8`, 640h words) with a
parallel key array `F16(key_ofs + 2i)` (`CC8E` = `CEB8 + C80h`). The order array persists between frames, so the
per-frame sort is incremental. Drawing goes from the **last** entry (largest key = farthest) to the first.

```c
void faces_sort_keys(void) {                                  // 0e12:361c
    if (BD23 || BA55) { face_order_reset(); BAD6 = 0; }       // 35f9: order[i] = E5B8 + 10*i, i < 640h
    for (u16 i = 0; i < face_count; i++) {
        u16 f = order[i], w0 = F16(f), k;
        u16 d0 = depth[w0 & 0x7FF];
        u8 n = w0 >> 14, farthest = (w0 >> 13) & 1;
        u16 d1 = depth[F16(f+2)];                             // w1/w2 are stored masked
        if (n == 0) k = d0;
        else if (farthest) {
            k = max(d0, d1);                                  // unsigned
            if (n >= 2) k = max(k, depth[F16(f+4)]);
            if (n == 3) k = max(k, depth[F16(f+6) & 0x7FF]);
        } else if (n == 1) k = (u16)(d0 + d1) >> 1;           // carry of the add is lost
        else if (n == 2) {
            u32 s = (u16)(d0 + d1) + (u32)depth[F16(f+4)];    // 17-bit (rcr takes the last carry)
            u16 h = s >> 1; u16 a = h >> 2; h -= a; a >>= 2; h -= a;   // ≈ s·11/32 ≈ average
            k = h;
        } else {
            u32 s = (u32)(u16)(d0 + d1) + (u16)(depth[F16(f+4)] + depth[F16(f+6) & 0x7FF]);
            k = (u16)(s >> 1) >> 1;                           // 17-bit sum / 4
        }
        key[i] = k;
    }
    if (!BD23 && !BA55) faces_bubble_sort(0, face_count - 1); // 7b06 (key/order swapped together)
    else { faces_quicksort(0, face_count - 1); BD23 = 0; }    // 7a18 → 7a35
}
// (361c runs with face_count 0 as well: 7b06/7a35 then get hi = −1 → bubble: end = 0 → returns at once.)

// 0e12:7a35 faces_quicksort(lo, hi) — ascending, pivot = key[lo]; swap() swaps key[] and order[] entries
void faces_quicksort(int lo, int hi) {
    u16 piv = key[lo]; int i = lo + 1, j = hi, p;
A:  for (;;) {                                               // 7a43
        if (piv < key[i]) goto B;
        if (++i > j) { p = j; if (lo != p) swap(lo, p); goto R; }   // 7a4e
    }
B:  for (;;) {                                               // 7aa9
        if (piv > key[j]) {                                   // 7ade
            swap(i, j); i++;
            if (j <= i) goto C;
            j--; goto A;
        }
        if (--j < i) goto C;
    }
C:  p = i - 1;                                                // 7ab4
    if (p == lo) goto RIGHT;
    swap(lo, p);
R:  if (p - 1 - lo > 0) { if (p - 1 - lo > 20) faces_quicksort(lo, p - 1); else faces_bubble_sort(lo, p - 1); }
RIGHT:
    if (hi - (p + 1) > 0) { if (hi - (p + 1) > 20) faces_quicksort(p + 1, hi); else faces_bubble_sort(p + 1, hi); }
}
// (byte arithmetic in the original: "(di−si) > 28h" ⇔ element difference > 20.)

// 0e12:7b06 faces_bubble_sort(lo, hi) — ascending
void faces_bubble_sort(int lo, int hi) {
    int end = hi + 1;                                         // exclusive
    for (;;) {
        u16 carry = key[lo]; int last = -1;
        for (int i = lo + 1; i < end; i++) {
            if (carry > key[i]) { swap(i - 1, i); last = i; } // carry keeps the larger value (now at i)
            else carry = key[i];
        }
        if (last < 0) return;
        end = last;
        if (!(end > lo + 2)) return;                          // QUIRK: a 2-element remainder is not re-checked
    }
}
```

### 4.7 Sky, ground and the horizon (0e12:373c, 3fe5, 3f6c, mirror 7b9b)

Sky and ground are drawn **during frame_update** (from `34c3`), before any face. Vertices 0 and 1 of the
projected arrays are borrowed as the two ends of the horizon line and restored afterwards.

```c
void sky_ground_draw(void) {                                  // 0e12:373c
    BA82 = 0;
    if (BAA6) {                                               // sky flash (lightning 2 / police 3 frames)
        u8 a = --BAA6;
        if (a == 0 && BCB0) { B9488 = BCB0; BCB0 = 0; }       // queued message after the flash
        BAD4 = 0; BAD6 = 0;                                   // full redraw + full copy
    }
    ftype /*BA9A*/ = 0x70;
    BA83 = 0; BA85 = 0; surface_under_car /*BA9B*/ = 0xFF;     // filled in by the face drawer (4.9)
    s16 save0 = sy[0], save1 = sy[1];
    s16 yr = cam_row, d = 0;                                  // 9496
    s8 r = cam_roll; u8 flat = 0;
    if (r) { d = (s16)BA97 >> (r < 0 ? -r : r); if (r > 0) d = -d; flat = 1; }
    yr += d;                                                  // row at the right edge (x = W32)
    s16 yl = yr - 2*d;                                        // row at the left edge (x = 0)
    s16 top = min((s16)BAD6, yr, yl);                         // BAD6 = previous frame's top row
    if (yr < (s16)BAD4) BAD4 = yr;  if (yl < (s16)BAD4) BAD4 = yl;
    span_top /*BA9C*/ = top; BA9E = top;
    sy[1] = yr - top; sy[0] = yl - top;                       // RELATIVE to top for the sky pass
    u8 sky_ega = sky_alt ? B95C1 : B95C0, sky_vga = sky_alt ? B95C3 : B95C2;
    if (BAA6) { BAD4 = 0; flat = 1; sky_ega = sky_vga = (u8)(BAA6 * 8 - 1); }   // flash colour (after the decrement)
    u16 pair;
    if (E338 == 0x13) { pair = sky_vga * 0x0101; BAA0 = pair; }
    else { flat = 1; BAA0 = sky_ega * 0x0101; pair = B4B9[(sky_ega << 4) | sky_ega]; }
    colour = pair; BAA2 = pair;                               // BAB1 / BAA2 (mirror sky)
    if (detail /*B6DD*/ == 0 || flat) {
        fill_above_edge(0, W32, /*v*/0, 1);                   // 3fe5, relative rows (see note)
    } else {
        // gradient sky (VGA, detail >= 1, no roll, no flash): flat colour, then 5 bands of 4 rows,
        // colour +0101h per band, ending at the horizon row
        s16 hrel = sy[0], t = top;                            // hrel = horizon − top (roll 0: both ends equal)
        s16 c = hrel - 0x14;
        if (c < 0) { hrel -= c; t += c; }                     // make room for the 20 gradient rows
        s16 h = hrel + t;                                     // absolute horizon row
        if (h >= 0) {
            if (t < 0) t = 0;
            s16 g = h - 0x14;                                 // first gradient row
            u16 ax = colour; u8 bands = 5, left = 4; u16 di;        // u8: bands can wrap (horizon at row 0)
            if (g < 0) {                                      // part of the gradient is above row 0
                BAD4 = 0; di = 0;
                do { if (--left == 0) { ax += 0x0101; bands--; left = 4; } } while (++g < 0);
                goto band_rows;                               // enters at "ax += 0101h"
            }
            if ((u16)g < BAD4) BAD4 = g;
            u16 nflat = g - t; di = t * 0x140;
            while (nflat--) { if (di >= 0x7800) goto ground; memsetw(V + di, ax, 0xA0); di += 0x140; }
            bands = 5;
        next_band:
            left = 4;
        band_rows:
            ax += 0x0101;
            do { if (di >= 0x7800) goto ground; memsetw(V + di, ax, 0xA0); di += 0x140; } while (--left);
            if (--bands) goto next_band;
        }
    }
ground:
    sy[1] += BA9E; sy[0] += BA9E;                             // back to absolute rows
    u16 g = snow /*BC77*/ ? 0x0F0F : W95C4;                   // ground colour codes (lo, hi)
    pair = B4B9[((g >> 8) << 4) | (g & 0xFF)];               // both bytes are codes 0..15 (VGA and EGA)
    colour = pair; BAA4 = pair;
    fill_below_edge(0, W32, 0, 1);                            // 3f6c: ground from the horizon down
    mirror_sky_ground();                                      // 7b9b (overlays section)
    sy[1] = save1; sy[0] = save0;
}
// The gradient/flat sky rows are full 320-px rows (160 words) regardless of the window width.
// NOTE (faithful quirk): in the flat path the two ends are relative to `top`; fill_above_edge's "top end at
// row ≤ 0" branch sets BA9C to that relative row (0), so when `top` equals one end of a tilted horizon (nothing
// was drawn above the horizon last frame) the sky triangle is drawn at rows 0.. instead of top.. — the rows
// between `top` and the tilted line keep old buffer content. Reproduce; check in DOSBox (open question 1).

// 0e12:3fe5 fill_above_edge(x0 = AX, x1 = CX, vertices a = BX, b = SI) — region above the edge a-b
void fill_above_edge(u16 x0, u16 x1, u16 a, u16 b) {
    if ((u16)(x0 - x1) == 0x8000) return;
    rows2 = rows3 = 0; reload2 = 0; dxl2 = dxr2 = 0; xl2 = x0; xr2 = x1; dxl = dxr = 0;
    u8 flip = 0;                                              // 946A
    s16 y0 = sy[a], y1 = sy[b];
    if (y0 > y1) { swap(y0, y1); flip = 1; }                  // y0 = higher end
    if (y0 > 0) {                                             // full-width rows from span_top down to y0
        rows1 = y0 + 1; reload2 = 3;
        rows2 = y1 - y0;
        if (rows2) {
            s16 s = (s16)(x1 - x0); if (!flip) s = -s; s /= (s16)rows2;        // idiv
            if (s > (s16)dxr2) { dxr2 = s; s = 0; }  dxl2 = s;                 // dxl2 = min(s,0), dxr2 = max(s,0)
        }
        fill_spans(x0, x1);                                   // span_top is the caller's
    } else {
        span_top = y0;                                        // (may be ≤ 0)
        rows1 = y1 - y0 + 1;
        if (y1 - y0) {
            s16 s = (s16)(x1 - x0); if (!flip) s = -s; s /= (s16)(y1 - y0);
            if (s > (s16)dxr) { dxr = s; s = 0; }  dxl = s;
        }
        fill_spans(x0, x1);
    }
}

// 0e12:3f6c fill_below_edge(x0, x1, a, b) — region below the edge a-b, then full columns [x0,x1] to the bottom
void fill_below_edge(u16 x0, u16 x1, u16 a, u16 b) {
    if ((u16)(x0 - x1) == 0x8000) return;
    rows3 = 0; reload2 = 3; dxl2 = dxr2 = 0; xl2 = x0; xr2 = x1; dxl = dxr = 0; rows2 = 0;
    s16 y0 = sy[a], y1 = sy[b]; u16 xt = x0, xb = x1;
    if (y0 > y1) { swap(y0, y1); swap(xt, xb); }             // y0/xt = top end
    span_top = y0;
    s16 below = BA91 - y1; if (below >= 0) rows2 = below;     // rows under the edge
    rows1 = y1 - y0 + 1;
    u16 l = xt, r = xb;
    if (y1 - y0) {
        s16 s = (s16)(xt - xb) / (s16)(y1 - y0);
        r = xt;                                               // span starts as the single point xt
        if (s > (s16)dxl) { dxl = s; s = 0; }  dxr = s;       // dxl = max(s,0), dxr = min(s,0)
    }
    fill_spans(l, r);
}
```

### 4.8 The span engine (0e12:41d4) — every polygon ends here

State (all DS): `span_top BA9C` (first row, may be < 0), segment row counts `rows1 BAC1`, `rows2 BAC3`,
`rows3 BAC5`; per-row x decrements (1/32 px, **subtracted** each row) `dxl BAB3`, `dxr BAB5`; segment-2 values
`xl2 BAC7`, `xr2 BAC9`, `dxl2 9460`, `dxr2 9462`; segment-3 values `xl3 BACB`, `xr3 BACD`, `dxl3 BABD`,
`dxr3 BABF`; which side(s) reload at the next boundary `reload2 BAAF`, `reload3 BAB0` (bit 0 left, bit 1 right);
`colour BAB1`; `ftype BA9A` (0 = OR mode).

```c
void fill_spans(u16 xl, u16 xr) {                             // 0e12:41d4 (V); 7e11 = mirror clone
    xl -= 0x20; xr += 0x20;                                   // spans are widened by 1 px on both sides
    s16 row = span_top;
top:
    if (row < 0) goto clip_top;
body:
    if ((u16)row >= BA91) return;
    s16 t = row + rows1 - BA91;
    if (t >= 0) { rows2 = 0; rows1 -= t; }
    if (ftype == 0) {                                         // OR mode (face type 0: headlight beams)
        if (colour == 0x0707 && colour_mode /*day*/) return;
        do { span_or(row, xl, xr); row++; xl -= dxl; xr -= dxr; } while (--rows1);
    } else
        do { span_put(row, xl, xr); row++; xl -= dxl; xr -= dxr; } while (--rows1);
seg_end:
    if (rows2 == 0) return;
    rows1 = rows2; rows2 = rows3; rows3 = 0;
    u8 f = reload2; reload2 = reload3;
    if (f & 1) { xl = xl2; xl2 = xl3; xl -= 0x20; dxl = dxl2; dxl2 = dxl3; xl -= dxl; }
    if (f & 2) { xr = xr2; xr2 = xr3; xr += 0x20; dxr = dxr2; dxr2 = dxr3; xr -= dxr; }
    goto top;
clip_top: {
        u16 n = -row; row = 0;
        rows1 -= n;
        if ((s16)rows1 > 0) { xl -= n*dxl; xr -= n*dxr; goto body; }   // (repeated subtraction, 16-bit)
        // (the unrolled skip loop in the <= 0 branch below does ONE step when its count is 0)
        n += rows1;                                           // = rows of this segment
        xl -= n*dxl; xr -= n*dxr;
        row = (s16)rows1;                                     // ≤ 0: continue clipping in the next segment
        goto seg_end;
    }
}

// one span (V: seg 90D0, row_ofs B8D3, W32/W32M from view_setup; M: seg 90D2, mrow_ofs B993, B00h/AE0h)
void span_put(s16 row, u16 xl, u16 xr) {
    u16 a = xl & 0xFFE0, c = xr & 0xFFE0;
    if (c > W32) {
        if (c > W32 + 0x8000) return;                         // right end "negative"
        if (a <= W32M) c = W32;
        else if (a <= c) return;                              // both beyond the right edge
        else c = W32;                                         // left end negative: whole row
    }
    u16 n = c - a; if (NEG(n)) n = 0;
    if (a > W32M) {
        if (a <= 0x8000) return;
        n += a; if (NEG(n)) return;
        a = 0;
    }
    u16 px = a >> 5, cnt = n >> 5;
    u8 far *d = V + row_ofs[row] + px;
    u16 pair = colour;
    if (((px ^ row) & 1) == 0) pair = swap_bytes(pair);       // checkerboard dither
    // store: rep stosw of cnt>>1 words; if cnt>>1 == 0 → ONE byte even when cnt == 0; else a final byte if cnt odd
    if ((cnt >> 1) == 0) d[0] = (u8)pair;
    else { for (u16 k = 0; k < (cnt & ~1); k++) d[k] = (k & 1) ? pair >> 8 : (u8)pair; if (cnt & 1) d[cnt-1] = (u8)pair; }
}
// span_or: identical clipping and parity; the bytes are OR-ed into V (`or es:[di],ax` / `or es:[di],al`) with
// the same count rule (cnt ≤ 1 → one byte).
```

### 4.9 Face drawing (0e12:323e face half, 3a7c, 3a9e)

The merge loop of `323e` (4.13) calls, for each face (farthest first):
`BAD0 = face; ftype = w3_hi & F8h; colour = F16(face+8)` (the pair `010Fh` is byte-swapped on odd frames
(`BD39 & 1`) — a blinking dither), then `draw_face_dispatch`.

```c
void draw_face(u16 f) {                                       // 3a7c + 3a9e (register juggling resolved)
    u16 w0 = F16(f), v0 = w0 & 0x7FF, v1 = F16(f+2);
    switch (w0 >> 14) {
    case 0: {                                                 // point (3bba)
        u16 x = sx[v0];
        if (!NEG(x) && !NEG(x + 0x5800) && !NEG(x + 0x2C00)) {       // 0 <= x < 2800h
            if (sy[v0] == 0x8000) vertex_project_y(v0);
            point_draw(v0, w0);                               // 66ad
        } else if (mirror_on) {
            u16 m = x - BD3D;
            if (NEG(m) || NEG(m + 0x6A00) || NEG(m + 0x3500)) return;  // 0 <= m < 1600h
            if (my[v0] == 0x8000) vertex_project_y_mirror(v0);
            point_draw_mirror(v0, w0);                        // 81c7
        }
        return; }
    case 1: {                                                 // line (3c0c)
        u16 a = sx[v0], b = sx[v1];
        if (front_visible(2, a, b, 0, 0)) {
            if (sy[v0] == 0x8000) vertex_project_y(v0);
            if (sy[v1] == 0x8000) vertex_project_y(v1);
            line_draw(/*bx*/v1, /*si*/v0, sx[v1], sx[v0], w0);   // 67af
        } else if (mirror_on && mirror_visible(2, a, b, 0, 0)) {
            if (my[v0] == 0x8000) vertex_project_y_mirror(v0);
            if (my[v1] == 0x8000) vertex_project_y_mirror(v1);
            line_draw_mirror(v1, v0, sx[v1], sx[v0], w0);        // 82d6
        }
        return; }
    case 2: draw_triangle(v0, v1, F16(f+4) & 0x7FF); return;   // (3a91: w2 masked again)
    case 3: draw_quad(f, v0, v1, F16(f+4) & 0x7FF, F16(f+6) & 0x7FF); return;
    }
}
// front test (all x values of the face):   (1) all NEG(x)            → not front
//                                          (2) all !NEG(x − 2800h)   → not front
//                                          (3) all !NEG(x − 5400h)   → not front      else front
// mirror test (only if not front and mirror_on), m = x − BD3D:
//                                          all NEG(m) / all !NEG(m − 1600h) / all !NEG(m − 4B00h) → skip
// A face is drawn in at most one of the two views. The mirror pass is only reached when B6D2 (mirror on);
// in the replay/crash layout the mirror image is never copied (CC92), but faces are still rasterized into M.

void draw_triangle(u16 v0, u16 v1, u16 v2) {                  // 3a9e
    if (front_visible(3, sx[v0], sx[v1], sx[v2], 0)) {
        for v in (v0, v1, v2): if (sy[v] == 0x8000) vertex_project_y(v);
        // registers are now bx = v2, si = v0, di = v1
        u16 a = sx[v2], b = sx[v0], c = sx[v1];  sort3_unsigned(&a, &b, &c);     // a <= b <= c
        if (NEG(c - b - 1) || NEG(b - a - 1) || NEG(a - c - 1)) tri_fill(v2, v0, v1);   // 682e
        else face_around_camera(v2, v0, v1);                  // all three gaps in 1..8000h: camera inside
    } else if (mirror_on && mirror_visible(3, ...)) {
        for v in (v0, v1, v2): if (my[v] == 0x8000) vertex_project_y_mirror(v);
        tri_fill_mirror(v2, v0, v1);                          // 836e
    }
}
void draw_quad(u16 f, u16 v0, u16 v1, u16 v2, u16 v3) {       // 3b0e
    if (front_visible(4, ...)) {
        for v in (v0, v1, v2, v3): if (sy[v] == 0x8000) vertex_project_y(v);
        // registers: bx = v3, si = v0, di = v1, bp = v2  (this rotation matters for BAD2 in 69da)
        sort4_unsigned(a <= b <= c <= d of the four sx);
        if (NEG(d - c - 1) || NEG(c - b - 1) || NEG(b - a - 1) || NEG(a - d - 1)) quad_fill(v3, v0, v1, v2);
        else { draw_triangle(v0, v1, v2); draw_triangle(v0, v2, v3); }   // split (3e64, full tests again)
    } else if (mirror_on && mirror_visible(4, ...)) {
        for v in (v0..v3): if (my[v] == 0x8000) vertex_project_y_mirror(v);
        quad_fill_mirror(v3, v0, v1, v2);                     // 85a9
    }
}

// 3d25..3da4 face_around_camera: the triangle surrounds the camera (the ground/ceiling the car is on)
void face_around_camera(u16 b, u16 s, u16 d) {
    s16 h = plane_height_at_camera(b, s, d);                  // 6316 → BCDF, BACF = (h > cam_y)
    u8 t = ftype >> 3;
    if (t == 0) goto fill;
    if (t < 0x0E) {
        if (t == 1) {                                         // wall/rail
            u16 v = car_speed; if (NEG(v)) v = -v;
            if (v < 0x14) bounce_back();                 // 0fd6
            else { BAA8 = 1; BAAE = 1; }                      // crash request
        }
        goto fill;
    }
    if (t >= 0x14 && t < 0x1C) {
        if (t >= 0x18) goto fill;
        if (NEG(cam_y - car_eye_height - h + bridge_clearance /*95D3*/)) { BA83 = h; goto fill; }   // roof over us
    }
    // ground types 0Eh..13h, 14h..17h within the clearance, >= 1Ch
    BACF = 0;
    if ((u16)h > BA85) {
        BA85 = h; BA89 = b; BA8B = s; BA8D = d;               // highest ground under the camera
        if ((s8)surface_under_car < 0x1C) surface_under_car = t;   // BA9B (types >= 1Ch stick)
    }
fill:
    fill_around_camera(b, s, d);                              // 3ebf
}

// 0e12:6316 plane_height_at_camera(b, s, d) → h (also BCDF); BACF = (h > cam_y)
s16 plane_height_at_camera(u16 b, u16 s, u16 d) {
    s16 h;
    if (vy[b] == vy[s] && vy[b] == vy[d]) { h = vy[b]; goto out; }
    u16 cx = cam_x4;                                          // arrange: b left of the camera (X <= cam), s,d right
    if (vx[b] > cx) { swap(s, b); if (vx[b] > cx) { swap(d, b); goto L6350; } }
    if (!(vx[d] > cx)) swap(d, s);                            // 6346 (reached from both paths above)
L6350:
    if (!(vx[s] > cx)) swap(d, b);
    s16 X0 = vx[b];
    s16 z1 = vz[b], z2 = vz[b], y1 = vy[b], y2 = vy[b];
    if (vx[s] != X0) { z1 += IMULDIV(vz[s] - vz[b], cam_x4 - X0, vx[s] - X0);
                       y1 += IMULDIV(cam_x4 - X0, vy[s] - vy[b], vx[s] - X0); }
    if (vx[d] != X0) { z2 += IMULDIV(vz[d] - vz[b], cam_x4 - X0, vx[d] - X0);
                       y2 += IMULDIV(cam_x4 - X0, vy[d] - vy[b], vx[d] - X0); }
    h = y1; if (z2 != z1) h += IMULDIV(cam_z4 - z1, y2 - y1, z2 - z1);
out:
    BCDF = h; BACF = (h > (s16)cam_y);
    return h;
}
// IMULDIV(a,b,c) = (s16)(((s32)a*b) / c) — 16x16→32 imul, 32/16 idiv (traps on overflow: guard in the port).
// The X comparisons are UNSIGNED (ja/jbe).

// 0e12:3ebf fill_around_camera(b, s, d): sort by sx (unsigned) → p <= q <= r; fill per edge
void fill_around_camera(u16 b, u16 s, u16 d) {
    sort the three by sx unsigned, carrying the vertex ids → (p, q, r);  BAB7 = p; BAB9 = q; BABB = r;
    edge(p, q) if sx[p] < W32 (unsigned);  edge(q, r) if sx[q] < W32;  edge(r, p) always;
    // edge(u, w): if (BACF) { BAD4 = 0; span_top = 0; fill_above_edge(sx[u], sx[w], u, w); }
    //             else fill_below_edge(sx[u], sx[w], u, w);
}
```

### 4.10 Rasterizer setups (triangles, quads, lines, points)

Slopes are "x decrement per row": `(x_start − x_end) / rows`, 16-bit `cwd; idiv` (truncation toward zero).
`LT(a,b)` below = `NEG(a − b)`.

```c
// 0e12:682e tri_fill(bx, si, di)
void tri_fill(u16 a, u16 b, u16 c) {
    if (sy[a] >= sy[b]) swap(a, b);                           // signed compares (jl)
    if (sy[b] >= sy[c]) swap(b, c);
    if (sy[a] >= sy[b]) swap(a, b);                           // sy[a] <= sy[b] <= sy[c]
    if (sy[c] == sy[a]) tri_row(a, b, c);                     // 685c
    else if (sy[c] == sy[b]) tri_flat_bottom(a, b, c);        // 68b0
    else if (sy[b] == sy[a]) tri_flat_top(a, b, c);           // 6909
    else tri_general(a, b, c);                                // 6956
}
void tri_row(u16 a, u16 b, u16 c) {                           // one row: min..max of the three x
    u16 x1 = sx[a], x2 = sx[b], x3 = sx[c];                   // cx, dx, ax
    u16 lo, hi;                                               // chosen with LT() (wrap compares)
    if (!LT(x3, x2)) { if (LT(x3, x1)) { lo = x2; hi = x1; } else if (LT(x1, x2)) { lo = x1; hi = x3; } else { lo = x2; hi = x3; } }
    else             { if (!LT(x3, x1)) { lo = x1; hi = x2; } else if (LT(x1, x2)) { lo = x3; hi = x2; } else { lo = x3; hi = x1; } }
    span_top = sy[a]; rows2 = rows3 = 0; rows1 = 1;
    fill_spans(lo, hi);
}
void tri_flat_bottom(u16 a, u16 b, u16 c) {                   // apex a on top
    u16 xa = sx[a], l = sx[c], r = sx[b];
    span_top = sy[a]; u16 h = sy[b] - sy[a]; rows2 = rows3 = 0; rows1 = h + 1;
    if (!LT(l, r)) swap(l, r);
    dxl = (s16)-(l - xa) / (s16)h;  dxr = (s16)-(r - xa) / (s16)h;
    fill_spans(xa, xa);
}
void tri_flat_top(u16 a, u16 b, u16 c) {                      // apex c at the bottom
    u16 xc = sx[c], l = sx[b], r = sx[a];
    span_top = sy[a]; u16 h = sy[c] - sy[a]; rows2 = rows3 = 0; rows1 = h + 1;
    if (!LT(l, r)) swap(l, r);
    dxl = (s16)(l - xc) / (s16)h;  dxr = (s16)(r - xc) / (s16)h;
    fill_spans(l, r);
}
void tri_general(u16 a, u16 b, u16 c) {                       // a top, b middle, c bottom
    u16 xa = sx[a], xb = sx[b], xc = sx[c];
    xl2 = xr2 = xb;
    span_top = sy[a];
    s16 h1 = sy[b] - sy[a], h = sy[c] - sy[a], h2 = h - h1;
    rows1 = h1 + 1; rows3 = 0; rows2 = h2;
    dxr2 = (s16)-(xc - xb) / h2; reload2 = 2;                 // lower short edge (b→c)
    dxl = (s16)-(xc - xa) / h;                                // long edge (a→c)
    s16 s = (s16)-(xb - xa) / h1;                             // upper short edge (a→b)
    if (s > (s16)dxl) { swap(dxl, s); dxl2 = dxr2; reload2 = 1; }   // short edge is the left one
    dxr = s;
    fill_spans(xa, xa);
}

// 0e12:69da quad_fill(bx = a, si = b, di = c, bp = d) — BAD0 = face record (vertex words at +0,+2,+4,+6)
// The face drawer calls it as (v3, v0, v1, v2) — i.e. `a` starts as the vertex of word 6, b word 0, c word 2,
// d word 4 — and the sort network below tracks the record word of the top vertex in BAD2.
void quad_fill(u16 a, u16 b, u16 c, u16 d) {
    u16 tw = 6;
    if (sy[a] > sy[b]) { swap(a, b); tw = 0; }                // signed compares (jle)
    if (sy[b] > sy[c]) swap(b, c);
    if (sy[c] > sy[d]) swap(c, d);
    if (sy[a] > sy[b]) { swap(a, b); tw = 2; }
    if (sy[b] > sy[c]) swap(b, c);
    if (sy[a] > sy[b]) { swap(a, b); tw = 4; }
    BAD2 = tw;                                                // = record word offset of the top vertex a
    // sy[a] <= sy[b] <= sy[c] <= sy[d]
    if (sy[a] == sy[d]) { drop_median(&a, &b, &c); tri_row(a, b, d); return; }          // 6a31: one row
    if (sy[a] == sy[c]) { drop_median(&a, &b, &c); tri_flat_top(a, b, d); return; }     // 6a69: 3 on top
    if (sy[b] == sy[d]) { drop_median3(&d, &b, &c); tri_flat_bottom(a, b, c); return; } // 6a9d: 3 at bottom
    // opp (DS:946A): 0 when c is an edge-neighbour of a in the face's vertex cycle, 1 when opposite
    //   opp = !((F16(BAD0 + ((BAD2+2)&7)) & 7FFh) == c || (F16(BAD0 + ((BAD2-2)&7)) & 7FFh) == c)
    if (sy[b] == sy[a]) {                                     // 6ade: flat top edge a-b; c, d below
        xl2 = sx[c]; xr2 = sx[d];
        s16 e = (s16)(sx[c] - sx[d]); if (sy[d] - sy[c]) e /= (s16)(sy[d] - sy[c]);
        dxl2 = dxr2 = e;                                      // bottom edge c-d
        u8 opp = opposite(c);
        span_top = sy[a];
        s16 hc = sy[c] - sy[a], hd = sy[d] - sy[a];
        rows3 = 0; reload2 = 1; rows1 = hc + 1; rows2 = hd - hc;
        u16 L = sx[b], R = sx[a];
        if (!LT(L, R)) { swap(L, R); opp ^= 1; }              // L = left end of the top edge
        if (!opp) { swap(xl2, xr2); swap(hc, hd); reload2 = 2; }   // left side goes to d, right to c
        dxl = (s16)(L - xl2) / hc;
        dxr = (s16)(R - xr2) / hd;
        fill_spans(L, R);
        return;
    }
    // 6bb9: general — a strictly above b, b strictly above d
    span_top = sy[a];
    s16 hb = sy[b] - sy[a], hc = sy[c] - sy[a], hd = sy[d] - sy[a];   // 9466, 9468, 945E
    rows1 = hb + 1; rows2 = hc - hb; rows3 = hd - hb - rows2;
    u8 opp = opposite(c);
    u16 xa = sx[a], xb = sx[b], xc = sx[c], xd = sx[d];      // 9464, dx, cx, bx
    if (sy[b] == sy[c]) { if (LT(xc, xb)) { swap(b, c); swap(xb, xc); } goto CHAINS_A; }
    if (!opp) goto CHAINS_A;
    if (xb == xd || LT(xb, xd)) goto CHAIN_B_LEFT; else goto CHAIN_B_RIGHT;

CHAINS_A:     // 6ca5 — a's neighbours are b and c, d opposite: chains a→b→d and a→c→d
    reload2 = 1; reload3 = 2; xl2 = xb; xr2 = xc;
    dxl = (s16)(xa - xb) / hb;
    { s16 s = (s16)(xa - xc) / hc;
      if (!LT(s, dxl)) { swap(dxl, s); swap(xl2, xr2); swap(hb, hc); reload2 = 2; reload3 = 1; }
      dxr = s; }
    { s16 q = (s16)(xl2 - xd); if (hd - hb) q /= (s16)(hd - hb); dxl2 = q; }   // hb/hc possibly swapped
    { s16 q = (s16)(xr2 - xd); if (hd - hc) q /= (s16)(hd - hc); dxr2 = q; }
    if (rows2 == 0) {                                         // b and c on the same row
        reload2 = 3; rows2 = rows3; rows3 = 0;
        if (NEG(dxl - dxl2))      { reload2 = 2; dxl = (s16)(xa - xd) / hd; }
        else if (NEG(dxr2 - dxr)) { reload2 = 1; dxr = (s16)(xa - xd) / hd; }
    }
    fill_spans(xa, xa); return;

CHAIN_B_LEFT: // 6d82 — c opposite a: chain a→b→c→d on one side, a→d on the other (b left of d)
    if (rows2 == 0) { rows2 = rows3; rows3 = 0; if (!LT(xb, xc)) swap(xb, xc); xc = xd; }   // unreachable
    xl2 = xr2 = xb; xl3 = xr3 = xc;
    dxl = (s16)(xa - xb) / hb;
    { s16 s = (s16)(xa - xd) / hd; u8 side = 1;
      if (!LT(s, dxl)) { swap(dxl, s); side = 2; }
      dxr = s; reload2 = reload3 = side; }
    dxl2 = dxr2 = (s16)(xl2 - xc) / (s16)rows2;
    { s16 q = (s16)(xl3 - xd); if (rows3) q /= (s16)rows3; dxl3 = dxr3 = q; }
    fill_spans(xa, xa); return;

CHAIN_B_RIGHT: // 6e0a — mirror image (b right of d)
    if (rows2 == 0) { rows2 = rows3; rows3 = 0; if (LT(xb, xc)) swap(xb, xc); xc = xd; }    // unreachable
    xr2 = xl2 = xb; xr3 = xl3 = xc;
    dxr = (s16)(xa - xb) / hb;
    { s16 s = (s16)(xa - xd) / hd; u8 side = 2;
      if (LT(s, dxr)) { swap(dxr, s); side = 1; }
      dxl = s; reload2 = reload3 = side; }
    dxl2 = dxr2 = (s16)(xr2 - xc) / (s16)rows2;
    { s16 q = (s16)(xr3 - xd); if (rows3) q /= (s16)rows3; dxl3 = dxr3 = q; }
    fill_spans(xa, xa); return;
}

// drop_median(&a,&b,&c): drops the vertex whose x is the median of (x_a, x_b, x_c) under LT(), using the same
// compare tree as tri_row (x1 = x_a, x2 = x_b, x3 = x_c): if !LT(x3,x2): LT(x3,x1) → drop c; else LT(x1,x2) →
// drop b; else drop a.  If LT(x3,x2): !LT(x3,x1) → drop c; else LT(x1,x2) → drop a; else drop b.  The dropped
// vertex's slot is refilled so that the call receives the two kept vertices in their original registers.
// drop_median3(&d,&b,&c) is the same tree on (x1 = x_d, x2 = x_b, x3 = x_c), keeping two of {d, b, c}.
```


(`CHAINS_A`: when a division's divisor is 0 the dividend is kept, exactly as the `je` around the `idiv`. The
`rows2 == 0` branches of 6d82/6e0a are unreachable because `sy[b] == sy[c]` was routed to `CHAINS_A`. All
`idiv`s are 16-bit.)

```c
// 0e12:67af line_draw(bx = p, si = q, ax = x_p, dx = x_q, ES:BP = &w0)
void line_draw(u16 p, u16 q, u16 xp, u16 xq, u16 w0) {
    s16 yt = sy[p], yb = sy[q];
    if (yt >= yb) { swap(yt, yb); swap(xp, xq); }             // top first
    u16 x = xp; s16 step = (s16)(xp - xq);
    span_top = yt; u16 h = yb - yt; rows1 = h + 1;
    if (h) step /= (s16)h;
    if ((w0 & 0x1800) == 0) { line_thin_spans(x, step); return; }     // 44c2
    dxl = dxr = step; rows2 = 0;
    u16 size = B95DD[(w0 >> 11) & 3];                         // line widths (leg file +4Bh)
    u16 half = CS16(0x12F6 + 2*ratio_idx(size, dist[p]));     // angular half width, 1/32 px
    fill_spans(x - half, x + half);                           // parallelogram, dithered
}
// ratio_idx(s, d) = (s < d) ? (((u32)s << 16) / d) >> 7 & 1FFh : 1FFh   (dist of the vertex passed in BX)

// 0e12:44c2 line_thin_spans(bp = x, si = step): one span per row from x(y) to x(y+1), no dither
void line_thin_spans(u16 x, s16 step) {
    BAC3 = 0; s16 row = span_top;
    if (row >= (s16)BA91) return;
    if (row < 0) {
        u16 n = -row; row = 0; rows1 -= n;
        if ((s16)rows1 < 1) return;
        if (rows1 == 1) { step = BAC3; goto draw; }           // QUIRK: x is not advanced by n rows here
        x -= n * step;
    }
    { s16 t = row + rows1 - BA91; if (t > 0) { BAC3 = step; rows1 -= t; } }
draw:
    for (;;) {
        u16 a = x, c = x - step;
        if (!NEG(a - c)) swap(a, c);                          // a = min, c = max (wrap compare)
        a &= 0xFFE0; c &= 0xFFE0;
        if (c > W32M) { if (c > W32M + 0x8000 /*A7E0h for 320*/) goto next;
                        if (a > W32M && a <= c) goto next; c = W32M; }
        if (a > W32M) { if (a <= 0x8000) goto next; a = 0; }
        u16 cnt = ((c - a) >> 5) + 1;
        put `cnt` bytes of `colour` (word stores, low byte first, no parity swap) at row_ofs[row] + (a >> 5);
    next:
        row++; x -= step;
        if (--rows1 > 1) continue;
        if (rows1 == 0) return;
        step = BAC3;                                          // last row: 1-px dot (or normal if bottom-clipped)
    }
}
// (patched constants: W32M in 452f/453a/4543/4546, W32M+8000h in 4535.)

// 0e12:66ad point_draw(bx = v, si = w0): lamps
void point_draw(u16 v, u16 w0) {
    u16 size = B95D9[(w0 >> 11) & 3];                         // {0Dh, 2, 9, 8} in the shipped legs
    u16 r = CS16(0x12F6 + 2*ratio_idx(size, dist[v])) >> 5;   // radius in px
    span_top = sy[v] - r;
    if (r == 0) return;
    rows1 = 2*r;
    int o = owner_object(v);                                  // last object i (from A473−1 down) with
    if (o < 0) return;                                        //   AFB9[i] <= v <= AFB9[i] + B0F9[i]
    u16 a = obj_heading[o] & 0xFF00;
    if (!(w0 & 0x2000)) a += 0x4000;                          // bit 13 clear: lamp faces sideways
    a = a - cam_heading - sx[v] + (BA99 << 8);                // (add ah, BA99)
    a &= 0x7FFF; if (a == 0x4000) a = 0x3FFF;
    if (a & 0x4000) a = 0x8000 - a;                           // fold to 0..4000h
    u16 s = CS16(0x0FF2 + ((a >> 6) & 0xFE));                 // 65535·sin: 0 edge-on, max face-on
    u16 q = (r < 12) ? 0x30 : 0x38;
    if (r > 1) { u8 quo = q / (r - 1), rem = q % (r - 1); q = quo + (((u8)(rem + 0x10) >= 0x20) ? 1 : 0); }
                                                              // r == 1: no DIV, q stays 30h/38h
    u16 inc = ((u32)(u8)q * s) >> 16;                         // BAB5
    dxr = inc; dxl = (u8)inc * (u8)r;                         // BAB3 = AL*CL (8-bit mul)
    u16 w = (r < 12 ? 8*r : 4*r);
    u16 half = ((u32)w * s) >> 16;
    blob_spans(sx[v] - half, sx[v] + half);                   // 43db
}
// (when r - 1 == 0 the code skips the DIV: AX = 30h/38h; AH = 0 + 10h < 20h → AL unchanged, so q = 30h/38h.)

// 0e12:43db blob_spans(si = xl, bp = xr): rows grow by `a` then shrink (a -= dxr each row), no dither
void blob_spans(u16 xl, u16 xr) {
    xl -= 0x20; xr += 0x20; s16 row = span_top;
    if (row < 0) {
        u16 n = -row; row = 0; rows1 -= n;
        if ((s16)rows1 <= 0) return;
        u16 a = dxl; while (n--) { a -= dxr; xl -= a; xr += a; } dxl = a;
    }
    if ((u16)row >= BA91) return;
    { s16 t = row + rows1 - BA91; if (t >= 0) rows1 -= t; }
    do {
        span with the same clip as span_put (patched 4418/441e/4423/442c/4435) but no parity swap;
        row++; dxl -= dxr; xl -= dxl; xr += dxl;
    } while (--rows1);
}
```

Mirror clones (all verified by instruction diff): `81c7` = `66ad` with `dist·2`, `my[]`, `14h` instead of `BA99`,
`x = (s16)(1600h − (sx − BD3D)) >> 1`, blob `8016` (rows 0..12h, width B00h/AE0h, `mrow_ofs`, seg `90D2`);
`82d6/80fb` = `67af/44c2` likewise; `836e/839c/8413/8490/8501` = `682e/685c/68b0/6909/6956`; `85a9` = `69da`;
`7e11` = `41d4` (height 13h instead of `BA91`, width B00h/AE0h/8B00h instead of the patched values).

### 4.11 Face types, day/night and weather in the renderer (summary; details in 4.14 and objects.md)

Face type `t = w3 >> 11` (`BA9A = t << 3`):

| t | Renderer behaviour |
|---|---|
| 0 | **OR mode**: `41d4` ORs the colour pair into V instead of storing it; the whole face is skipped when its pair is 0707h and `95C7 ≠ 0` (day). Car headlight beam quads (also the overlay beam `4b37`, colour 0808h). |
| 1 | wall: if it is the face around the camera → bump (`0fd6`) below speed 14h, else crash request `BAA8 = BAAE = 1`; also tested by `34f1` |
| 2, 3 | ordinary (2 also tested by `34f1`) |
| 4–13 | colour pair overwritten every frame by `4db6` (lamps by viewing side 0404h/0F0Fh, 0F0Fh/0, 0E0Eh/0; 7 random blink 0F0Fh/0; 8 strobe 1 frame in 8; 9 brake 0404h / 0C0Ch when stopped; 10 police beacon 09/0C/0F cycling; 11 traffic light 0A0Ah/0E0Eh/0C0Ch by phase; 12 crossing flasher 0808h / 0C0Ch alternating; 13 wiper vertex animation) — the pairs are raw palette pairs (EGA colours 0–15), bypassing `B4B9` |
| 14–19 (0Eh–13h) | ground types: the face around the camera sets `BA85` (height) and `BA9B` (surface for the simulation) |
| 20–23 (14h–17h) | bridge decks: ground only when within the clearance `95D3`, otherwise `BA83` (roof: no rain) |
| 24–27 (18h–1Bh) | knock-over / fold faces (`34f1` → `4590`, `4631`); drawn normally |
| 28–31 (1Ch–1Fh) | zone faces (weather ramps 1Ch–1Eh, leg finish 1Fh) via `BA9B`; drawn normally |

Other colour effects: colour pair `010Fh` is byte-swapped on odd frames (flashing dither, `323e`); points are
never dithered (`43db`), thin lines neither (`44c2`); polygons, thick lines, sky and ground are.

Day/night and weather:

| Global | Effect in the renderer |
|---|---|
| `95C7` colour_mode (leg file; debug key N) | 0 = dark: remap 8→0, 7→8 (`261c`), `B4B9` darkened by 0202h at load (`0792:0fce`), moon (sprite 4) instead of sun (5); 1 = day: OR-faces with 0707h hidden, headlight overlay only in weather |
| `95C8` sky_alt | alternate sky colours `95C1/95C3`; no sun/moon; set while it rains/snows (`46dc`) |
| `BC75` rain 0..8 | remap 8→0 by day; grey streaks + windscreen drops (`46dc`), wipers clear drops; `BAA6` lightning flashes (objects) when ≥ 6 |
| `BC77` snow 0..8 | codes 2, 3, 0Ah → 0Fh (day) / 7 (night); ground pair forced to `B4B9[FFh]` (0F0Fh codes); white flakes + drops |
| `BAA6` sky_flash | sky colour `BAA6·8−1`, full redraw, message `BCB0` afterwards |
| `B6DD` detail | cells 3/6/10, 5-band gradient sky (VGA, ≥ 1), vehicle/sprite ranges (`03d4`), vegetation density (`BA56`) |

### 4.12 Camera, overlays, mirror frame and presentation

Buffers: `V` = view buffer (seg `90D0`), `M` = mirror buffer (seg `90D2`), `S` = screen page 0 (A000h),
`P1` = library page 1. Overlay polygons use vertex slots 630h..63Fh of `scr_x/scr_y` and fake face records
in DS (`BAD0` = DS offset, ES = DS) for `69da`/`67af`.


```c
// ---------- 0e12:77d5 frame_draw ----------
void frame_draw(void) {
  set_page(DS[0x9A] = 1);
  draw_faces_and_sprites();                        // 323e
  if (!b948C) {
    ground_slope_update();                      // 6421
    if (surface_under_car == 0x0E && (i16)(A1F3 - car_eye_height) <= (i16)BA85) BAA8 = 1;  // in water
    if (b948B /* && !948C */) rcam_y = BA85 + rcam_height;                              // 94A7
    if (!b948B) {
      if (B6EA) BAAD = 2;
      if (BAAD) { u8 t = BAAD - 1; if (surface_under_car == 0x0E || B707) BAAD = t; BAA8 = 0; }
    }
    if (BAA8 && !b948B) { BD3B = 0; if (surface_under_car == 0x0E) BD3B = BD3C = 1; crash_start(); }
    cockpit_overlays();                            // 46c0
    if (B78D) replay_start();                      // 4c51 (returns if already 948B)
    mirror_frame_draw();                           // 7c21
  }
  set_page(DS[0x9A] = 0);
}

// ---------- 0e12:6421 ground_slope_update ---------- (Y=DS:2502[], X=DS:3182[], Z=DS:3E02[], byte offsets)
void ground_slope_update(void) {
  if (BA85 == 0) { BA85 = BA87; return; }
  BA87 = BA85;
  u16 b = BA89, s = BA8B, d = BA8D; i16 roll_in = 0 /*al*/, cx = 0;
  if (Y[b] == Y[s] && Y[b] == Y[d]) goto tail;               // al = 0, cx = 0
  // pass 1: sort by X (UNSIGNED compares), point on edge b->d at X[s]
  if (X[s] != X[b]) {
    if (!(X[s] > X[b])) swap(d, b);
    if (X[s] == X[d]) { swap(b, d); goto p1_eq; }
    if (X[s] < X[d]) ; else swap(d, s);
    if (X[s] == X[b]) goto p1_eq;
    if (!(X[s] > X[b])) swap(s, b);
    i16 bp = X[d] - X[b], t = X[s] - X[b];
    w945E = (i16)((i32)t * (Y[d] - Y[b]) / bp) + Y[b];
    w9460 = (i16)((i32)t * (Z[d] - Z[b]) / bp) + Z[b];
  } else { p1_eq: w945E = Y[b]; w9460 = Z[b]; }
  w9462 = slope(Y[s] - w945E, Z[s] - w9460);
  // pass 2: identical with X<->Z swapped: sort by Z, interpolate Y and X at Z[s]
  ...;                                                       // 6510..6583, same structure
  cx = slope(Y[s] - w945E, X[s] - w9460);
  i16 c = (i8)((w9462 >> 1) >> 8);                           // sar 1, take high byte, cbw
  i16 a = -(i16)(i8)((cx >> 1) >> 8);
  b946A = hi(car_heading 9491);
  rotate_by_heading(&a, &c);                                 // 6604
  roll_in = a; cx = c;
tail:
  BCDC = slope_to_roll_code((u8)roll_in);                    // 6666
  i16 d2 = cx - car_pitch;                                   // 948F
  BCDD = d2 == 0 ? 0 : d2 < 0 ? (d2 <= -0x18 ? -0x18 : d2) : ((u16)d2 >= 0x18 ? 0x18 : d2);
}
i16 slope(i16 dy, i16 dz) {       // 64cf / 6586
  if (dy == 0) return 0;
  if (dz == 0) return 0x7FFF;
  if (abs(dy) >= (u16)abs(dz) >> 1) return ((dy ^ dz) < 0) ? 0x8001 : 0x7FFF;
  return (i16)(((i32)dy << 16) / dz);
}
void rotate_by_heading(i16 *a, i16 *c) {   // 6604; S=DS:B7CF sin[0..64], C=DS:B851 cos[0..64], Q15
  u8 ang = b946A, q = ang & 0xC0, i = ang & 0x3F;
  i16 x = *a, y = *c;
  if (i) { i16 nx = hi16(2*x*C[i]) + hi16(2*y*S[i]);  i16 ny = hi16(2*y*C[i]) - hi16(2*x*S[i]); x = nx; y = ny; }
  switch (q) { case 0x00: break; case 0x40: { i16 t = x; x = y; y = -t; } break;
               case 0x80: x = -x; y = -y; break; case 0xC0: { i16 t = x; x = -y; y = t; } break; }
  *a = x; *c = y;
}
u8 slope_to_roll_code(u8 al) { u8 m = (i8)al < 0 ? -al : al; i8 v = BD01[(m & 0x7F) >> 2]; return (i8)al < 0 ? -v : v; }
// BD01[32] = 00 0C 0B 0A 09 09 08 08 08 07 07 07 07 06 06 06 06 06 05 05 05 05 05 05 04 04 04 04 04 04 04 04

// ---------- 0e12:0f31 crash_start ----------
void crash_start(void) {
  if (B78D) return;
  B78D = 1; music_stop(0); shake_count = 0x10; BAAB = 0;
  for (u16 bx = A475 * 2; bx != 0; bx -= 2) {
    u16 n = W(0xB379 + bx), lo = W(0xB239 + bx);
    if (n && BAA9 >= lo && BAA9 < lo + n) { BAAB = bx; return; }
  }
}
// ---------- 0e12:0fa1 screen_shake_step ----------  (per frame from 76ec; per step in crash loops)
void screen_shake_step(void) {
  if (!shake_count) return;
  u8 k = --shake_count;
  if ((u8)E776 == 0x13) return;
  u8 y = CS_0F81[2*k], x = CS_0F81[2*k+1];
  gfx_set_origin(x, y);          // 13h: display start = y*320 + (x & ~3)
}
// 0e12:0F81 pairs (y,x) for k=0..15: (0,0)(1,1)(1,2)(3,0)(0,4)(2,2)(4,1)(5,5)(1,4)(7,2)(3,6)(4,3)(0,7)(6,1)(2,5)(8,8)
// race end sets shake_count=1 then calls 0fa1 → origin (0,0).

// ---------- 0e12:46c0 cockpit_overlays ----------
void cockpit_overlays(void) { precipitation_draw(); if (!b948B) { headlight_beam_draw(); dashboard_edge_draw(); wipers_update_draw(); } }

// ---------- 0e12:46dc precipitation_draw ----------
void precipitation_draw(void) {
  sky_alt = 1;
  u16 lim = half_window ? 0x5000 : 0x7800;
  if (snow_level) {                                   // BC77, priority over rain
    if (BA83) goto fade;
    dirty_top = 0;
    snow_flakes(V, 0x0F, rngD2, snow_level - 1, lim);
    snow_flakes(V, 0x0F, rngD4, snow_level - 1, lim);
    windscreen_spawn(snow_level, 0x80); return;
  }
  if (rain_level) {                                   // BC75
    if (BA83) goto fade;
    dirty_top = 0;
    rain_streaks(V, 0x07, rngD2, rain_level - 1, lim);
    rain_streaks(V, 0x07, rngD4, rain_level - 1, lim);
    windscreen_spawn(rain_level, 0x40); return;
  }
  sky_alt = 0;
fade:                                                 // no weather / under a roof: retire drops
  if (!(rngD2 & 4)) { BB34 = (BB34 + 1) & 0x3F; drop_state[BB34] = 0; }
}
void snow_flakes(u8 *es, u8 c, u16 base, int m /*n-1*/, u16 lim) {   // 47aa
  for (int j = 2*m + 1; j >= 0; j--) {
    u16 p = (base + BB14[j]) & 0x7FFF;
    if (p < lim) es[p] = c;
    p ^= 0x4000; if (p < 0x7800) es[p] = c;           // hard 0x7800 even in half window
  }
}
void rain_streaks(u8 *es, u8 c, u16 base, int m, u16 lim) {         // 477c
  int len = ((m + 2) >> 1) + 3;
  for (int j = 2*m + 1; j >= 0; j--)
    for (u16 p = (base + BB14[j]) & 0x7FFF, k = len; k && p < lim; k--, p += 0x140) es[p] = c;
}
void windscreen_spawn(u16 n, u8 type) {                // 47d1
  if (((u8)rngD2 >> 2 & 7) < n) windscreen_splat(rngD4, type);
  if (((u8)rngD4 & 7) < n)      windscreen_splat(rngD2, type);
}
void windscreen_splat(u16 r, u8 type) {                // 47fc (also from 33f7: sprite hit, type 00 or C0 by rngD4&8)
  u8 i = BB34 = (BB34 + 1) & 0x3F;
  drop_state[i] = type;                                // age 0
  u16 off = r >> 1; drop_pos[i] = off;
  i16 row = off / 0x140, col = off % 0x140, key = (i16)0x8000;
  if (row - 0x60 < 0) {
    u16 dy = 0x60 - row; int neg = 0;                  // pivots at (47,96) and (174,96)
    i16 dx = col - 0x2F; if (dx >= 0x7F) dx -= 0x7F;
    if (dx < 0) { dx = -dx; neg = 1; }
    u16 dist = ((u16)dx > dy ? 3*dx + dy : dx + 3*dy) >> 2;   // octagonal |(dx,dy)|
    if (dist <= wiper_radius /*94E9=0x78*/) {
      u16 k = ((dx >> 8) >= dy) ? 0x7FFF : (u16)((((u32)dx << 8)) / dy) >> 1;
      key = neg ? -k : k;
    }
  }
  drop_key[i] = key;
}

// ---------- 0e12:4a5c wipers_update_draw ----------
void wipers_update_draw(void) {
  u8 s = wiper_phase;                                  // BC7C
  if (s == 0) { if (wipers_on) s = 1; }
  else if (!(s & 0x80)) { if (++s >= 5) s = 0x83; }    // 0x84 then dec
  else { if (--s == 0x80) s = 0; }
  if (s || wipers_on) wiper_phase = s;                 // (stored whenever the update branch ran)
  int p = s & 7, i0 = p - 1, i1 = p;  if (s & 0x80) { i0++; i1++; }
  i16 hi = wiper_keys[i0], lo = wiper_keys[i1];        // DS:9527 + 2*i  (i=-1 → DS:9525)
  for (int i = 0; i < 64; i++) if (drop_key[i] <= hi && drop_key[i] >= lo) drop_state[i] = 0;
  windscreen_drops_draw();                             // 487e
  for (int k = 0; k < 6; k++) {
    u16 w = wiper_verts[p][k]; i16 y = w >> 8, x = (w & 0xFF) << 5;
    if (half_window) { y -= 0x10; x -= 0x500; }
    if (y < dirty_top) dirty_top = y;
    Y5702[0xC68 + 2*k] = Y5702[0xC74 + 2*k] = y;
    X4A82[0xC68 + 2*k] = x;  X4A82[0xC74 + 2*k] = x + wiper2_dx /*0x1040*/;
  }
  fill_colour = 0x0000;
  overlay_quads4_draw(0x0707);                         // black blades, grey edge lines
}
// wiper_verts (x,y) phase 0: (210,90)(210,87)(54,91)(54,94)(50,96)(44,96)
//                        1: (178,43)(176,41)(49,89)(51,91)(50,96)(44,96)
//                        2: (132,19)(130,18)(46,88)(48,89)(50,96)(44,96)
//                        3: (54,1)(52,1)(43,88)(45,88)(50,96)(44,96)
//                        4: (4,6)(2,7)(42,89)(44,88)(49,96)(44,96)      right wiper = x+130
// Phase 0 (rest) wipes nothing (range [0x7FFF..0x602C] empty).

// ---------- 0e12:487e windscreen_drops_draw ----------
void windscreen_drops_draw(void) {
  for (int i = 0; i < 64; i++) {
    u8 d = drop_state[i]; if (!d) continue;
    d++;
    u8 age;
    while (!(age = d & 0x3F)) {                        // age wrapped
      if (!(rain_level | snow_level)) { drop_state[i] = 0; goto next; }
      d -= 0x38;                                       // → age 8, keep drop while weather lasts
    }
    drop_state[i] = d;
    u16 c = (d & 0xC0) == 0x00 ? 0x0606 : (d & 0xC0) == 0x80 ? 0x0F0F : (d & 0xC0) == 0x40 ? 0x0707 : 0x0000;
    u16 p = drop_pos[i], lim = 0x77FB;
    dirty_top = 0;
    if (half_window) { p -= 0x1428; lim = 0x4FFB; }
    if (age <= 1) drop_shape_fresh(p, c, lim); else drop_shape_old(p, c, lim);
  next:;
  }
}
// Every write: q &= 0x7FFF; if (q <= lim) write. Shapes as (row, col) relative to p:
// fresh 48f5: r-3:{-1,0} r-2:{-2..2} r-1:{-3..3} r0:{-2,-1,0,1,3} r+1:{-1,0,2}
// old 495c, colour byte <6 (black only): r-6:{1} r-4:{-1} r-2:{0,3} r-1:{-3,0} r0:{1} r1:{-1,0,1} r2:{0}
// old 495c, colour >=6:  r-4:{-1} r-3:{-4,-1,0,3} r-2:{-5} r-1:{-2,-1,0,4} r0:{-4,-1,0,1,3,4} r1:{-4,-3} r2:{0,4} r3:{-3,0,1}
// (words write lo byte at col, hi at col+1; colour pair has equal bytes so all pixels = low byte)

// ---------- 0e12:4b37 headlight_beam_draw ----------
void headlight_beam_draw(void) {
  if (!headlights) return;
  if (colour_mode /*day*/ && !rain_level && !snow_level) return;
  for (int q = 0; q < 2; q++) {                        // vertex words from beam_verts[4*q .. 4*q+3]  (q=1 starts at +8 bytes: overlap)
    for (int k = 0; k < 4; k++) {
      i16 y = beam_verts_word[2*(2*q + k)], x = beam_verts_word[2*(2*q + k) + 1];   // bx=0 / bx=8 bytes
      if (half_window) { y -= 0x10; x -= 0x500; }
      Y5702[0xC60 + 2*k] = y; X4A82[0xC60 + 2*k] = x;
    }
    fill_colour = 0x0808; BA9A = 0; BAD0 = 0xBC7D;
    quad_fill_69da(0xC60, 0xC62, 0xC64, 0xC66);        // BA9A=0 → OR 08 into V
  }
}
// beam (x,y): q0 (129,72)(191,72)(264,78)(56,78); q1 (264,78)(56,78)(64,95)(256,95)

// ---------- 0e12:4bf3 dashboard_edge_draw ----------
void dashboard_edge_draw(void) {
  const i16 *t = B78D ? dash_verts_crashed /*9531*/ : dash_verts /*9561*/;
  u16 edge = B78D ? 0x0707 : car_cockpit_edge /*94CD*/;
  if (BD3B) return;                                     // water crash
  for (int k = 0; k < 12; k++) {
    i16 x = t[2*k] << 5, y = t[2*k+1];
    if (half_window) { y -= 0x10; x -= 0x500; }
    Y5702[0xC68 + 2*k] = y; X4A82[0xC68 + 2*k] = x;
  }
  fill_colour = car_cockpit_colour;                     // 94CB
  overlay_quads4_draw(edge);
}
// dash_verts (x,y): (0,95)(24,91)(110,92)(110,95)(160,95)(160,93)(160,95)(160,93)(210,92)(210,95)(320,95)(320,91)
// crashed:          (0,95)(54,85)(134,88)(134,95)(160,95)(160,93)(160,95)(214,85)(294,88)(294,95)(320,95)(320,93)

// ---------- 0e12:4cb0 overlay_quads4_draw(edge) ----------
void overlay_quads4_draw(u16 edge) {
  BA9A = 0x70;
  BAD0 = 0xBC85; quad_fill_69da(0xC68, 0xC6A, 0xC6C, 0xC6E);
  BAD0 = 0xBC8D; quad_fill_69da(0xC6C, 0xC6E, 0xC70, 0xC72);
  BAD0 = 0xBC95; quad_fill_69da(0xC74, 0xC76, 0xC78, 0xC7A);
  BAD0 = 0xBC9D; quad_fill_69da(0xC78, 0xC7A, 0xC7C, 0xC7E);
  w945E = 0;
  if (edge) {
    fill_colour = edge;
    line_67af(/*bx*/0xC6A, /*si*/0xC6C, X4A82[0xC6A], X4A82[0xC6C], /*bp*/0xBC89);
    line_67af(0xC76, 0xC78, X4A82[0xC76], X4A82[0xC78], 0xBC99);
  }
}

// ---------- 0e12:4c51 replay_start ----------
void replay_start(void) {
  if (b948B) return;
  replay_state = 1;                                     // 94B2
  CS_0AB4[0x020F + rec_head] = 0xFFFF;                  // end marker in recorder ring
  rec_head = (rec_head + 2) & 0x7FF;                    // NB recorder itself masks 0xFFF
  b948B = 1; rcam_pitch = -0x10; rcam_roll = 0; BA55 = 1;
  rcam_height = car_eye_height + 0x26; rcam_dist = 0x30;
  rcam_angle = hi(W(0x124A)) + 0x10;
}

// ---------- 0e12:7c21 mirror_frame_draw ----------
void mirror_frame_draw(void) {
  if (CC92) return;
  u8 *m = MIRROR;
  if (BD3F || (!mirror_on && B6D1)) { BD3F = 0; memset(m, 0x07, 0x688); }   // grey glass
  else if (mirror_on) B6D1 = 1;
  for (int r = 3; r <= 5; r++) { m[r*88+0]=8; m[r*88+1]=0; m[r*88+2]=8; m[r*88+85]=8; m[r*88+86]=0; m[r*88+87]=8; }
  for (int r = 6; r <= 17; r++) { m[r*88+0]=8; m[r*88+1]=0; m[r*88+86]=0; m[r*88+87]=8; }
  { int r = 18; m[r*88+0]=8; m[r*88+1]=0; m[r*88+2]=8; m[r*88+85]=8; m[r*88+86]=0; m[r*88+87]=8; }
  u16 o = 0x1229 - (half_window ? 0x28 : 0);            // V row 14, col 169 (129)
  V[o] = 8; memset(V + o + 1, 0, 84); V[o + 85] = 8;    // row 14: black rim, grey ends
  memset(V + o + 85 + 0xEC, 0x08, 84);                  // row 15, cols 170..253 (130..213): grey shadow
}

// ---------- 0e12:7b9b mirror_sky_ground ---------- (from 373c; M rows use 7C82[] half-row units)
void mirror_sky_ground(void) {
  if (CC92 || !mirror_on) return;
  BD3F = 0;
  i16 s0 = W7C82[0], s1 = W7C82[1];
  BA9C = 0xFFFF;
  i16 a = -car_pitch, d = 0;
  if (car_roll) { u8 n = abs((i8)car_roll); d = 0x580 >> n; a -= d; }   // roll SIGN IGNORED (ch unused)
  a = (a >> 1) + 0x0F;
  W7C82[1] = 2*d + a; W7C82[0] = a;
  fill_colour = BAA2; fill_above_7cd9(ax=0, cx=0xB00);  // sky, x 0..88 px (x<<5)
  fill_colour = BAA4; fill_below_7d94(ax=0, cx=0xB00);  // ground
  W7C82[1] = s1; W7C82[0] = s0;
}

// ---------- 0c1c:13d8 view_present (E338 == 0x13 path) ----------
void view_present(void) {
  mirror_present();                                     // 17cd (includes retrace wait)
  u16 wwords = ((BA95 >> 5) + 1) >> 1;                  // 160 / 120 / 128
  u16 x0 = 0xA0 - wwords, gap = 2 * x0;                 // centred; gap = 320 - width
  u16 di = x0 + (lo(view_origin) << 8 | hi(view_origin));   // row*256 + x (converted below)
  u16 rows = BA91, voff = (0x60 - BA91) >> 1, si = 0;
  if (B9C4) di += voff << 8;
  i16 skip = 0x15;
  if (!BA82) { skip = min((i16)BAD4, (i16)BAD6); BAD6 = BAD4; }
  skip -= voff;
  if (skip >= 0) { rows -= skip; if ((i16)rows <= 0) return; di += skip << 8; si += skip * 0x140; }
  di += (di & 0xFF00) >> 2;                              // row*256 → row*320
  u8 *S = screen, *Vv = V;
  u8 dl = rows;
  if (!CC92) {
    u8 done = BA91 - dl;
    if (done < 0x0E) {
      u8 dh = 0x0E - done;                               // rows inside the mirror band
      do {                                               // leave S cols 168..255 (mirror) untouched
        if (half_window) { copy(S+di, Vv+si, 128); si += 128+88; di += 128+88; copy(S+di, Vv+si, 24); si += 24+0x50; di += 24+0x50; }
        else             { copy(S+di, Vv+si, 168); si += 168+88; di += 168+88; copy(S+di, Vv+si, 64); si += 64;      di += 64;      }
        dl--;
      } while (--dh);
    }
  }
  while (dl--) { copy(S+di, Vv+si, 2*wwords); si += 2*wwords + gap; di += 2*wwords + gap; }
}
// Race layout: view_origin=0x0010 → view at screen rows 16..16+BA91-1; full 320×96 = rows 16..111;
// half (F1) 240×64 at x 40..279, rows 16..79 (031c blacks the rest: x 0..39 & 280..319 rows 16..111, x 40..279 rows 80..111);
// 09C4 (0x56 rows) centred: +5 rows.

// ---------- 0c1c:17cd mirror_present (mode 13h) ----------
void mirror_present(void) {
  while (!(inp(0x3DA) & 8)) ;                           // wait until vertical retrace is active
  if (BD3F || CC92) return;
  if (!mirror_on) { if (!B6D1) return; B6D1 = 0; }
  const u8 *m = MIRROR;
  static const struct { u16 dst, src, n; } R[5] = {     // S offset, M offset, bytes
    {0x0E80, 0x018, 40},   // S row 11 x192..231 ← M row0 x24..63
    {0x0FB0, 0x060, 72},   // S row 12 x176..247 ← M row1 x8..79
    {0x10EE, 0x0B6, 76},   // S row 13 x174..249 ← M row2 x6..81
    {0x122D, 0x10D, 78},   // S row 14 x173..250 ← M row3 x5..82
    {0x136C, 0x164, 80}};  // S row 15 x172..251 ← M row4 x4..83
  for (int i = 0; i < 5; i++) memcpy(S + R[i].dst, m + R[i].src, R[i].n);
  for (int r = 0; r < 14; r++) memcpy(S + 0x14A8 + r*0x140, m + 0x1B8 + r*88, 88);   // S rows 16..29 x168..255 ← M rows 5..18
}
// Mirror on screen: rows 11..29, x 168..255, rounded top; fixed position (ignores view_origin / half window).

// ---------- 0c1c:1759 rle_draw_viewbuf(src, runs, row) ----------
void rle_draw_viewbuf(const u8 *src, u16 runs, u16 row) {
  u8 *es = V; u16 di = row * 0x140, x = 0;
  while (runs--) {
    u8 c = *src++, n = *src++; x += n;
    u16 a = n;
    if (x > 0x140) { x -= 0x140; a = n - x; if (c != 0x0F) memset(es+di, c, a); di += a; di -= 0x280; a = x; }
    if (c != 0x0F) memset(es+di, c, a);
    di += a;
  }
}
// 0792:1c00: decompress image (E55A/E55C) → DS:2500; sfx 2; page 0; rle_draw_viewbuf(DS:2500, 0x23C2, 0x5F)
//            ; mirror_frame_draw(); BAD4 = 0; view_present().   (crash cracks over the frozen frame)

// ---------- 0e12:76ec frame_update (camera part) ----------
void camera_select(void) {            // after camera_pos_update()
  u16 hd = car_heading; i16 pit = car_pitch; i8 rl = car_roll; i16 cy = A1F3;   // cockpit
  if (b948B) {
    u8 ang = rcam_angle;
    if (!rcam_manual) {                                  // 16D8 == 0 → chase the car heading
      i8 diff = hi(car_heading) - ang;
      i8 step = diff < 0 ? ((u8)diff >= 0xF0 ? 0 : -2) : (diff <= 0x10 ? 0 : 2);
      rcam_angle += step; ang += step;
    }
    hd = ang << 8; pit = rcam_pitch; cy = rcam_y; rl = rcam_roll;
  }
  cam_heading = hd;  cam_centre_row = pit + BA8F;  cam_roll = rl;  cam_y = cy;
  W95B9 = (i16)hd < 0 ? -abs(W95B9) : abs(W95B9);          // sky drift signs follow the heading quadrant
  W95B7 = (i16)(hd - 0x4000) < 0 ? -abs(W95B7) : abs(W95B7);
}

// ---------- 0e12:6e92 camera_pos_update ----------
void camera_pos_update(void) {
  if (replay_state == 0) {
    if (!b948B) { rcam_y = A1F3; rcam_pitch = car_pitch; }
    W94B4 = sprite_x[0]; W94B6 = sprite_z[0];
  } else if (replay_state == 2 && BAAB) {                  // crash object into sprite slot 7
    W9D01 = W(0xA5B9 + BAAB); W9F81 = W(0xA6F9 + BAAB); WA201 = (W(0xA839 + BAAB) >> 3) + 0x30;
  }
  i16 ox = 0, oz = 0;
  if (b948B) { polar_small(rcam_angle, rcam_dist, &ox, &oz); ox <<= 1; oz <<= 1; }
  u16 cx = (ox + sprite_x[0]) & 0x7FFF, dz = (oz + sprite_z[0]) & 0x3FFF;   // 9CF3 / 9F73
  cam_x = cx; cam_z = dz;
  sprite_type[8] = sky_alt ? 0 : (colour_mode ? 5 : 4);   // sun / moon / none
  sprite_x[8] = cx; sprite_z[8] = dz;
  u8 o = B95BD & 7;
  if (o > 4) sprite_x[8] -= 0x900; else if (o && o < 4) sprite_x[8] += 0x900;
  o = (o + 1) & 7;
  if (o < 3) sprite_z[8] += 0x900; else if (o > 3 && o < 7) sprite_z[8] -= 0x900;
  sprite_y[8] = rcam_y + 0x3C0;
  cam_x4 = (cx << 2) | (B9494 >> 6);  cam_z4 = (dz << 2) | (B9495 >> 6);
}
void polar_small(u8 ang, u8 dist, i16 *cx, i16 *dz) {    // 6f9c; T = CS 0e12:0FF2 u16 sin(i·90°/128), i=0..128
  u8 q = ang >> 6, i = (ang << 2) & 0xFC;                 // B947D = q
  u8 s = hi(T[i/2]), c = hi(T[(0x100 - i)/2]);
  i16 x = -2 * ((dist * s) >> 8), z = -2 * ((dist * c) >> 8);
  switch (q) { case 0: break; case 1: { i16 t = x; x = z; z = -t; } break;
               case 2: x = -x; z = -z; break; case 3: { i16 t = x; x = -z; z = t; } break; }
  *cx = x; *dz = z;
}

// ---------- 0e12:0751 replay camera input (948B != 0), cl = direction bits & 0x0F ----------
//  16D8 = 1; rcam_dist &= 0xF8;
//  cl==1: if (rcam_dist > 0x10) rcam_dist -= 8;   cl==2: if ((u8)(rcam_dist+8)) rcam_dist += 8;
//  cl==4: rcam_angle += 4;   cl==8: rcam_angle -= 4;
//  cl==9: if (rcam_pitch < 0x30) rcam_pitch += 8;   cl==0xA: if (rcam_pitch > -0x18) rcam_pitch -= 8;
//  cl==5: if ((u16)rcam_height < 0x140) rcam_height += 4;   cl==6: if ((u16)rcam_height > 0x2E) rcam_height -= 4;
//  (if 948C: cl 5/6 move rcam_y ±4 directly)
```

Views that exist: **cockpit** (camera = car X/Z/A1F3, car heading/pitch/roll) and **external
orbit camera** (948B=1) used by F5 (scene frozen: physics 0977:0008 and the recorder are skipped, 026f saves /
restores BA85, BCDD, BCDC, BA9B), F10 / crash (4c51: replay of the recorder ring, 94B2=1), with the camera
placed at car − 4·dist·(sin θ, cos θ) looking along θ, auto-chasing the car heading (±2/frame when >16/256
off) until the stick is touched. No look-left/right/back views; the only rearward view is the mirror.

Mirror: yes, a real second 3D pass (4.9: 3a9e with B6D2, mirror rasterizers into M).
My side: 373c→7b9b paints M's sky/ground with a horizon from car_pitch/car_roll; 323e also admits sprites whose
relative angle byte+8 lands in 0x8C..0xAB when B6D2 (32e9) for the mirror; 7c21 frames it; 17cd copies it.
The pass is skipped in the replay/crash layout (CC92=1) and M is marked invalid (BD3F) whenever the LZ
decompressor runs.

Frame order (race_run): frame_update (camera, 34c3 3D build incl. 373c/3a9e mirror pass) → race_input(0) →
frame_draw (faces/sprites into V, overlays into V, mirror frame into M+V) → race_input(1) (keys; message boxes
drawn on S set BA82) → HUD (dash gauges on S) → view_present (VR wait, M→S, V→S) → spin until ≥5 ticks
(145.6 Hz) calling the RNG 0000:0f58 each spin.

Layout transitions (race_run, when 948B changes): 0→1 `059a`: copy S rows 0..15 → P1, fill S rows 0..15
black, `0602` draws the replay panel over S rows 112..199 (colour 8 etc.), CC92=1. 1→0 `04ca`: B6D1=1, copy
P1 rows 0..15 → S, decompress dashboard (E5BC/E5BE) to P1, copy P1 rows 16..103 → S rows 112..199, CC92=0.


### 4.13 Sprites (pre-scaling, projection, sorting, merge with faces, drawing)

#### Overview and call graph

```
load:  0792:0d2e / 01f4:53c4 ─► 2789 sprites_prescale
                                 ├─ 276e sprite_pixels_remap      (non-VGA palettes only)
                                 └─ 2810 sprite_prescale_one  (per sprite id 1..n-1)
                                     └─ 289a sprite_scale_setup  (per scale step)
                                         ├─ 2eb6 sprite_scale_patterns   (CS:267E → masks)
                                         ├─ 2d54 sprite_scale_rows  ─► 2f84 sprite_scale_row      (shrink, ≤ 1:1)
                                         └─ 2dc6 sprite_scale_rows_up ─► 3004 sprite_scale_row_up (grow, > 1:1)
frame: 77d5 frame_draw ─► 323e draw_faces_and_sprites
          ├─ 3085 sprite_list_build    (only when BA55 = 1: world cells were re-collected)
          ├─ 316c sprite_project       (angle, distance, depth key per listed sprite)
          ├─ 312e sprite_sort ─┬─ 78a7 ─► 78cc sprite_qsort ─► 794b sprite_rec_swap / 797b   (BA55 = 1)
          │                    └─ 3147 ─► 797b sprite_bubble_sort                          (BA55 = 0)
          ├─ 3352 sprite_animate       (drift, animation chains, crash debris)
          ├─ 33f7 sprite_collisions
          └─ merge loop: faces (3a7c) and sprites painter-sorted together, back to front
                ├─ 2e49 sprite_pick_scale
                └─ 28eb sprite_draw ─┬─ 2b74 sprite_blit_main   ─► 2c7c sprite_rows_main
                                     └─ 2999 sprite_blit_mirror ─► 2a9e sprite_rows_mirror
```

Key facts:
* Sprites are **not** transformed like polygons. Each listed instance gets a horizontal angle (screen x, 1/32 px),
  a Euclidean horizontal distance, and a depth key `|dy| + dist`. They are depth-sorted (descending key) and
  **merged with the sorted face list** in `323e`: before each face, every sprite whose key is greater than the
  face's key is drawn. So sprites and faces are depth-sorted together (painter's algorithm, one list each).
* Scaling is quantised: at load time every sprite is pre-scaled into `95BF` sizes (0Eh..11h steps) with
  bit-pattern tables (`CS:267E`). At draw time the apparent angular size selects a step. The blit is then an
  unscaled transparent copy (colour 0 is transparent). There is no per-pixel scaling at draw time.
* The same routine draws into the main 3D view (`seg DS:90D0`, 320-byte rows) or into the rear-view mirror
  (`seg DS:90D2`, 88-byte rows, 19 rows, half scale), selected by the sprite's angle.
* The sprite's projected point is its **bottom** row. The image is drawn upwards from it, centred on x.

#### Data: pre-scaled sprite cache (far buffer `E53C`, segment `B9EB`)

```
+0000  u16 dir[32]            dir[id] = offset of that sprite's step table (dir[0] = 40h, unused)
+0040  per sprite id 1..n-1, in id order:
        mode 0: u16 step[95BF]    offset of the copy for each step (equal-sized steps share a copy)
                copy …            one per distinct (W, H)
        mode 1: u16 step[95BF]    all → the same copy
                copy              built at scale 17h (1:1)
        mode ≥2: nothing (dir[id] points at the next sprite's data and is never used)
copy:  u8 h                     source rows = size of rep[]
       u8 W                     output row width (of the last stored row; all rows equal for shipped data)
       u8 H                     output rows on screen (sum of repeats)
       u8 rep[h]                repeat count per stored row (shrink: first H entries = 1, rest unused;
                                grow: h entries of B9D7 or B9D7+1)
       u8 pix[stored][W]        stored rows (shrink: H rows; grow: h rows), colour 0 = transparent
```
The build stops early (truncates rows) when the next row would not fit below D000h; `2789` raises fatal
message 1Ah if the write offset wrapped or passed D000h. The directory has room for 32 ids only.

#### Data: `CS:267E` scale-pattern table (image offset `0x0E120 + 0x267E = 0x1079E`, 24 × 10 bytes, ends at 0e12:276E)

Entry `e = 17h − r` for scale remainder `r` (0..17h); r = 17h is 1:1. Bytes 0-6 = 56-bit horizontal mask
(MSB of byte 0 first, applied from the left edge of the w-wide padded row; symmetric around the middle);
bytes 7-9 = 24-bit vertical mask, consumed as a 24-bit rotate-left of `(b9:b8:b7)` whose outgoing bit 23 is
tested (so the first row uses bit 7 of b9). Keep ratio ≈ (r+1)/24 in both directions.

| e | r | bytes | H ones /56 | V ones /24 |
|---|---|---|---|---|
| 0 | 17 | ff ff ff ff ff ff ff ff ff ff | 56 | 24 |
| 1 | 16 | ff ff 7f ff fe ff ff f7 ff ff | 54 | 23 |
| 2 | 15 | bf ef 7f ff fe fd fd f7 ff df | 50 | 22 |
| 3 | 14 | bd ef 7f f7 fe fd bd 77 ff df | 47 | 21 |
| 4 | 13 | bd ef 7b b7 de fd bd 77 ff dd | 44 | 20 |
| 5 | 12 | bd eb 7b b7 de bd bd 77 ff dd | 42 | 20 |
| 6 | 11 | ad eb 7a b7 5e bd b5 77 7f dd | 38 | 19 |
| 7 | 10 | ad ab 7a b5 5e b5 b5 75 7d dd | 35 | 17 |
| 8 | 0f | a5 ab 5a b5 5a b5 a5 75 7d 5d | 31 | 16 |
| 9 | 0e | a5 aa 5a a5 5a a5 a5 55 7d 5d | 28 | 15 |
| 10 | 0d | 25 aa 52 a5 4a a5 a4 55 7d 55 | 24 | 14 |
| 11 | 0c | 25 8a 52 a5 4a a1 a4 55 5d 55 | 22 | 13 |
| 12 | 0b | 25 8a 52 a4 4a a1 a4 55 55 55 | 21 | 12 |
| 13 | 0a | 25 8a 12 a4 42 a1 a4 15 55 55 | 19 | 11 |
| 14 | 09 | 21 8a 12 a4 42 a1 84 15 55 45 | 17 | 10 |
| 15 | 08 | 21 82 12 a4 42 21 84 11 55 45 | 15 | 9 |
| 16 | 07 | 21 82 12 24 42 21 84 11 55 44 | 14 | 8 |
| 17 | 06 | 01 82 12 24 42 21 80 11 15 44 | 12 | 7 |
| 18 | 05 | 01 80 12 24 42 01 80 11 14 44 | 10 | 6 |
| 19 | 04 | 01 80 02 24 40 01 80 10 14 44 | 8 | 5 |
| 20 | 03 | 01 80 02 20 40 01 80 10 14 04 | 7 | 4 |
| 21 | 02 | 00 80 02 20 40 01 00 00 14 04 | 5 | 3 |
| 22 | 01 | 00 80 00 20 00 01 00 00 14 00 | 3 | 2 |
| 23 | 00 | 00 00 00 20 00 00 00 00 04 00 | 1 | 1 |

Extraction: `img = exe[hdr_paragraphs*16:]; tab = img[0x1079E:0x1079E+240]` (`scratchpad/r3d/scaletab.py` decodes it).
The port should embed the 240 bytes verbatim (the rows are hand-tuned, e.g. e=4 and e=5 both keep 20 rows).

`DS:BA3F` step → scale value (17 bytes, image 0x2787F): `00 01 02 03 04 05 06 07 08 0a 0c 0e 10 12 14 17 1a`.
Values < 18h shrink with entry `17h − v`; 1Ah = grow (B9D7 = 1, r = 2, entry 21: each pixel/row once, plus once
more where the mask is 1 → ≈ 1.09×). Step 15 (17h) is 1:1.

`DS:BA0E` apparent size → step (49 bytes, image 0x2784E):
`00 01 02 03 04 05 06 07 08 08 09 09 0a 0a 0b 0b 0c 0c 0d 0d 0e 0e 0f 0f 0f 10 10 10 11 11 11 12 12 12 12 13 13 13 13 14 14 14 14 15 15 15 15 15 15`
(indices > BA0D are never used because of the clamp).

#### Pseudocode — load time

```c
// 0e12:2789
void sprites_prescale(void) {
    sprite_pixels_remap(/*di*/0x2500 + DS16[0x2502], /*dx*/0x2500 + DS16[0x2504], /*si*/0x2500 + DS16[0x2504]);
    B9EB = (DS16[0xE53C] >> 4) + 1 + DS16[0xE53E];
    BA0D = BA3F[(u8)(DS[0x95BF] - 1)];
    B9E5 = 0xD000;
    u16 di = 0x40;
    for (u16 id = 0;; ) {
        C16[2*id] = di;                       // C = cache segment B9EB
        if (id != 0) {
            u16 old = di;
            di = sprite_prescale_one(id, di);
            if (di < old || di > B9E5) { message_fatal(0x1A); return; }   // 0000:179c
        }
        if (++id >= DS16[0x2506]) break;
    }
}

// 0e12:276e  (ES = DS)
void sprite_pixels_remap(u16 di, u16 dx, u16 si) {
    if ((u8)DS16[0xE338] == 0x13) return;
    do { u8 b = DS[di]; if (b >= 0x10) b = DS[si + b - 0x10]; DS[di++] = b; } while (di < dx);
}

// 0e12:2810
u16 sprite_prescale_one(u8 id, u16 di) {
    B9D8 = id;
    u8 n = DS[0x95BF];
    u8 mode = DS[0x95E1 + id] & 7;
    if (mode == 0) {
        u16 dir = di; di += 2*n; B9E9 = 0xFFFF;
        u8 k = 0;
        do {
            C16[dir] = di;
            B9D6 = BA3F[k]; B9D7 = 0;
            u16 save = di;
            di = sprite_scale_setup(di);
            u16 wh = C16[B9E3];               // W | H<<8
            if (wh == B9E9) { di = save; C16[dir] = C16[dir - 2]; }
            else B9E9 = wh;
            k++; dir += 2;
        } while (k < n);
    } else if (mode == 1) {
        B9D7 = 0; B9D6 = 0x17;
        u16 dir = di; di += 2*n;
        for (u8 i = 0; i < n; i++) C16[dir + 2*i] = di;   // loop with CX = n
        di = sprite_scale_setup(di);
    }
    return di;                                // mode >= 2: unchanged
}

// 0e12:289a
u16 sprite_scale_setup(u16 di) {
    sprite_scale_patterns();
    u16 rec = 0x2500 + 8*B9D8;
    u8 w = DS[rec], h = DS[rec+1];
    DS[0x9480] = w; DS[0x9481] = h;
    C[di++] = h;
    B9E3 = di; di += 2;
    B9E1 = di; di += h;
    u16 lens = 0x2500 + DS16[rec+4];
    u16 ptrs = 0x2500 + DS16[rec+2];
    return (B9D7 >= 1) ? sprite_scale_rows_up(ptrs, lens, di) : sprite_scale_rows(ptrs, lens, di);
}

// 0e12:2eb6
void sprite_scale_patterns(void) {
    u8 v = B9D6;
    if (v >= 0x18) { v -= 0x18; B9D7++; if (v >= 0x18) { B9D7++; v -= 0x18; } }
    const u8 *E = CS + 0x267E + (v < 0x18 ? (0x17 - v) * 10 : 0);
    memcpy(&DS[0xB9EF], E, 7); memcpy(&DS[0xB9F6], E, 7);
    memcpy(&DS[0xB9FD], E, 7); memcpy(&DS[0xBA04], E, 5);     // BA09 not written
    if (v >= 0x18) {                                          // only if B9D6 >= 48h: unreachable
        v -= 0x18; B9D7 = 3;
        E = CS + 0x267E + (v < 0x18 ? (0x17 - v) * 10 : 0);
    }
    DS[0xBA0A] = E[7]; DS[0xBA0B] = E[8]; DS[0xBA0C] = E[9];
}

static int vbit(void) {                 // 24-bit rotate left of BA0C:BA0B:BA0A, returns old bit 23
    u8 c = DS[0xBA0C] >> 7, c0 = DS[0xBA0A] >> 7, c1 = DS[0xBA0B] >> 7;
    DS[0xBA0A] = (DS[0xBA0A] << 1) | c;
    DS[0xBA0B] = (DS[0xBA0B] << 1) | c0;
    DS[0xBA0C] = (DS[0xBA0C] << 1) | c1;
    return c;
}

// 0e12:2d54   (the initial "mov al,[lens]" is dead)
u16 sprite_scale_rows(u16 ptrs, u16 lens, u16 di) {
    u16 wlast = 0; B9DA = 0;
    for (;;) {
        if (vbit()) {
            B9DA++;
            C[B9E1] = 1;
            u16 start = di;
            di = sprite_scale_row(0x2500 + DS16[ptrs], DS[lens], di);
            B9E1++;
            wlast = di - start;
        }
        lens += 2; ptrs += 2;
        if (--DS[0x9481] == 0) break;
        if ((u16)(di + wlast) >= B9E5) break;      // no room for one more row
    }
    C[B9E3] = (u8)wlast; C[B9E3 + 1] = B9DA;
    return di;
}

// 0e12:2dc6
u16 sprite_scale_rows_up(u16 ptrs, u16 lens, u16 di) {
    u16 wlast = 0; B9DA = 0;
    for (;;) {
        u16 start = di;
        B9DA += B9D7;
        di = sprite_scale_row_up(0x2500 + DS16[ptrs], DS[lens], di);
        wlast = di - start;
        C[B9E1] = B9D7;
        if (vbit()) { B9DA++; C[B9E1]++; }
        lens += 2; B9E1++; ptrs += 2;
        if (--DS[0x9481] == 0) break;
        if ((u16)(di + wlast) >= B9E5) break;
    }
    C[B9E3] = (u8)wlast; C[B9E3 + 1] = B9DA;
    return di;
}

// 0e12:2f84 (shrink) and 0e12:3004 (grow, UP = 1)
u16 sprite_scale_row(u16 src, u8 len, u16 di /*, UP */) {
    u8 m = len & 0x3F; DS[0x9482] = m;               // bits 6-7 of the length word are ignored
    u8 pad = (u8)(DS[0x9480] - m) >> 1; B9D9 = pad;
    u16 bi = 0; u8 bits = 8;
    #define HBIT() ({ u8 *b = &DS[0xB9EF + bi]; int c = *b >> 7; *b = (*b << 1) | c; \
                      if (--bits == 0) { bi++; bits = 8; } c; })
    #define EMIT(p) do { int c = HBIT(); if (UP) C[di++] = (p); if (c) C[di++] = (p); } while (0)
    for (u8 i = 0; i < pad; i++) EMIT(0);
    for (u8 i = 0; i < m;   i++) EMIT(DS[src + i]);
    for (u8 i = 0; i < pad; i++) EMIT(0);
    for (u8 i = 0; i < bits; i++) rol8(&DS[0xB9EF + bi]);   // completes 8 rotations: mask restored
    return di;
}
```

Output width of every row is `popcount(first 2·pad+m mask bits)` (shrink) or `2·pad+m + popcount` (grow). The code
stores only the last row's width as W and uses it as the row stride when drawing; all shipped sprites have even
`w − m` on every row (checked with `scratchpad/r3d/sprchk.py`), so all rows have the same width.

#### Pseudocode — per frame

```c
// 0e12:3085
void sprite_list_build(void) {
    for (u16 k = 0; k <= 8; k++) DS16[0x8B62 + 2*k] = 2*k;       // instances 0..8 always listed
    u16 n = 9;
    u8 cx0 = (hi(X[0]) & 0xFC) - 4, cz0 = (hi(Z[0]) & 0xFC) - 4;  // car cell − 1 (X[0] = 9CF3[0])
    for (u16 i = 9; i < DS16[0x9A71]; i++) {
        if ((u8)ID[i] == 0) continue;
        if ((u8)((hi(X[i]) & 0xFC) - cx0) > 8) continue;           // column car-1..car+1
        if ((u8)((hi(Z[i]) & 0xFC) - cz0) > 8) continue;           // row    car-1..car+1
        DS16[0x8B62 + 2*n] = 2*i; if (++n >= 0x98) goto done;
    }
    for (u16 i = DS16[0x9A71], c = DS[0xBD21]; c != 0; c--, i++) { // tile children of this rebuild
        if ((u8)ID[i] == 0) continue;
        DS16[0x8B62 + 2*n] = 2*i; if (++n >= 0x98) goto done;
    }
done:
    DS16[0x94AE] = n;
}

// 0e12:316c
void sprite_project(void) {
    for (u16 k = 0; k < DS16[0x94AE]; k++) {
        u16 i = SPR_INST[k] / 2, id = ID[i];
        if ((u8)id == 0) { SPR_KEY[k] = 0xFFFF; continue; }        // 8A32/8C92 left stale
        B9D8 = (u8)id;
        u16 o = 0;
        u16 ax_ = (u16)(X[i] << 2) - DS16[0x94A1]; if ((s16)ax_ < 0) { ax_ = -ax_; o = 6; }
        u16 az  = (u16)(Z[i] << 2) - DS16[0x94A3]; if ((s16)az  < 0) { az  = -az;  o ^= 2; }
        u16 mx, mn;
        if (ax_ >= az) { o ^= 8; mx = ax_; mn = az; } else { mx = az; mn = ax_; }
        u16 idx = (mn == mx) ? 0x1FF : (mx == 0 ? 0 : (u16)(((u32)mn << 16) / mx) >> 7);  // 0..1FFh
        u16 t = 0x12F6 + 2*idx;
        SPR_ANGLE[k] = B7AF_b[o] + CS16[t + B79F_b[o]] - DS16[0x9498];
        u16 dist = (u16)(((u32)mx << 16) / CS16[0x10F6 + (CS16[t] >> 4)]);   // mx / cos(atan)
        u16 dy = Y[i] - DS16[0x949E]; if ((s16)dy < 0) dy = -dy;
        SPR_KEY[k] = dy + dist;
        if ((u8)id > 8 && dist < DS16[0x95CD]) dist = DS16[0x95CD];
        SPR_DIST[k] = dist;
    }
}

// 0e12:312e / 3147 / 78a7
void sprite_sort(void) {
    if (DS16[0x94AE] == 0) return;
    u16 lo = (DS[0x948B] == 0) ? 1 : 0, hi = DS16[0x94AE] - 1;     // entry 0 (car) excluded in normal view
    if (DS[0xBA55]) sprite_qsort(lo, hi); else sprite_bubble_sort(lo, hi);
}
// records = {SPR_KEY 8902, SPR_DIST 8A32, SPR_INST 8B62, SPR_ANGLE 8C92}, stride 130h; swap = 794b
void sprite_bubble_sort(int a, int b) {              // 797b, descending
    for (;;) {
        int last = -1;
        for (int j = a; j < b; j++)
            if (SPR_KEY[j] < SPR_KEY[j+1]) { swap(j, j+1); last = j; }
        if (last < 0) return;
        b = last;
        if (b <= a) return;
    }
}
static void sub(int a, int b) { if (b - a <= 0) return; if (b - a > 20) sprite_qsort(a, b); else sprite_bubble_sort(a, b); }
void sprite_qsort(int lo, int hi) {                  // 78cc, descending; pivot = first element
    u16 piv = SPR_KEY[lo]; int i = lo + 1, j = hi, p;
scan_i:
    for (;;) {
        if (piv > SPR_KEY[i]) goto scan_j;
        if (++i > j) { p = j; if (lo != p) swap(lo, p); goto rec; }
    }
scan_j:
    for (;;) {
        if (piv < SPR_KEY[j]) {
            swap(i, j); i++;
            if (j <= i) goto meet;
            j--; goto scan_i;
        }
        if (--j < i) goto meet;
    }
meet:
    p = i - 1;
    if (p == lo) { sub(lo + 1, hi); return; }
    swap(lo, p);
rec:
    sub(lo, p - 1);
    sub(p + 1, hi);
}

// 0e12:3352  (runs after projection: new positions show next frame, new ids this frame)
void sprite_animate(void) {
    for (int k = DS16[0x94AE] - 1; k >= 0; k--) {
        u16 i = SPR_INST[k] / 2, id = ID[i];
        if (id & 0x40) Z[i] += DS16[0x95B9];
        if (id & 0x80) X[i] += DS16[0x95B7];
        u8 cd = (id >> 8) & 0x1F;
        if (cd != 0) {
            hi_byte(ID[i])--;                                  // countdown − 1
            if (--cd == 0)
                ID[i] = DS16[0x9611 + 2*(id & 0x3F)] | (id & 0xE0C0);   // keep class + drift bits
            continue;
        }
        u8 lo = (u8)id;                                        // countdown 0: crash debris ids 6..8
        if (lo < 6 || lo > 8) continue;
        u16 j = lo - 6;
        s8 f = (s8)(DS[0x94B3] - 9); if (f < 0) continue;
        u16 t = (u8)(0x0B - f);                                // = 14h − 94B3 = frame 0..11
        X[i] += (s8)DS[0xBA58 + j];
        Z[i] += (s8)DS[0xBA5B + j];
        Y[i]  = DS[0xBA5E + j + 3*t] + Y[0] - DS16[0x94BF];
    }
}

// 0e12:33f7  (walks nearest first; stops at the first sprite outside the radius)
void sprite_collisions(void) {
    if (DS[0x948B]) return;
    for (int k = DS16[0x94AE] - 1; k >= 0; k--) {
        if (SPR_KEY[k] >= DS16[0x95CF]) return;
        u16 i = SPR_INST[k] / 2, id = ID[i]; u8 cls = (id >> 8) & 0xE0;
        if (cls == 0) continue;
        if (cls == 0x60) {                                     // solid
            u16 s = DS16[0x1296]; if ((s16)s < 0) s = -s;
            if (s < 0x14) { bounce_back(); return; }              // 0e12:0fd6
            if ((u8)id == 0x10 && DS[0xBCD8] < 0x30) return;
            DS[0xBAA8] = 1; return;                            // crash
        }
        if (cls < 0x60) {                                      // 20h / 40h: knocked over
            DS[0xBAA7] = 1; sfx_play(3); DS[0x947C] = 5; ID[i] |= 1;
            if (cls == 0x20) ID[i] = 0x001F; else random_damage();   // far 0e12:0edb
            continue;
        }
        ID[i] = 0;                                             // 80h..E0h: removed
        if (cls == 0x80) windscreen_splat(/*ax*/DS16[0xD2], /*dl*/(DS[0xD4] & 8) ? 0xC0 : 0);
        else if (cls == 0xA0) DS16[0x9488] = 0x0D;             // "Go back to the main road!"
        else if (cls == 0xC0) DS16[0x9488] = 0x18;             // "WRONG WAY!"
        else sfx_play(0x0D);                                   // E0h
    }
}
```

The collision test is only `depth key < 95CF` (radius on `|dy| + horizontal distance` from the *camera*); there is
no lateral test. Class 40h keeps its class after `|= 1`, so it re-triggers every frame while in range (see open questions).

```c
// 0e12:323e
void draw_faces_and_sprites(void) {
    if (DS[0xBA55]) sprite_list_build();
    sprite_project(); sprite_sort(); sprite_animate();
    DS[0xBA55] = 0;
    sprite_collisions();
    u16 f = DS16[0xBAD8], k = DS16[0x94B0];                    // 94B0 = 0
    while (k < DS16[0x94AE] || f != 0) {
        ES = DS16[0xE5BA];
        u8 cl = (u8)ID[SPR_INST[k] / 2]; u16 key = SPR_KEY[k];  // read even when k >= 94AE (unused)
        if (k >= DS16[0x94AE]) goto face;
        if (cl == 0) { k++; continue; }
        if (f != 0 && key <= ES16[DS16[0xCC8E] + 2*(f-1)]) goto face;   // face is farther or equal
        u8 v = hi(SPR_ANGLE[k]) + 8; B9D5 = v;
        if (v >= 0x40 && ((u8)(v + 0x74) >= 0x20 || DS[0xB6D2] == 0)) { k++; continue; }
        //  main view: angle in [−800h, 37FFh]; mirror: hi(angle) in 84h..A3h (behind)
        if (key <= 0x10) { k++; continue; }
        if (cl > 5)                  { if (key >= DS16[0xB6E2]) { k++; continue; } }
        else if (cl <= 3 && (cl & 1)) { if (key >= 0x980)       { k++; continue; } }
        sprite_pick_scale(k, key);
        sprite_draw(k);
        k++; continue;
    face:
        f--;
        u16 bp = ES16[DS16[0xCEB8] + 2*f]; DS16[0xBAD0] = bp;
        u16 dx = ES16[bp], ax = ES16[bp+8], cx = ES16[bp+6];
        DS[0xBA9A] = hi(cx) & 0xF8;
        u16 si = ES16[bp+2] * 2, bx = (dx & 0x7FF) * 2; dx &= 0xC0FF;   // dh &= C0h
        if (ax == 0x10F && (DS16[0xBD39] & 1)) ax = swap_bytes(ax);     // blinking colour pair
        DS16[0xBAB1] = ax;
        draw_face_dispatch(/*bx, si, dx, cx*/);                         // 0e12:3a7c, faces section
    }
}
```
Note the `cl` used for the distance limits is the whole low byte of the id (drift bits included): ids 1, 3 are
limited to key < 980h, ids 2, 4, 5 (plane, moon, sun) are unlimited, everything else (including drifting clouds
and birds, low byte ≥ 40h) uses `B6E2`.

```c
// 0e12:2e49
void sprite_pick_scale(u16 k, u16 depth) {
    if (B9D5 & 0x80) depth <<= 1;                          // mirror: half size
    u8 s = (u8)ID[SPR_INST[k] / 2] & 0x3F; B9D8 = s;
    u16 size = (u16)(DS[0x95E1 + s] & 0xF8) << 3;
    u16 idx; DS[0x947D] = 0;
    if (size == depth) idx = 0x1FF;
    else if (size > depth) { DS[0x947D] = 4; idx = (u16)(((u32)depth << 16) / size) >> 7; }
    else                   {                  idx = (u16)(((u32)size  << 16) / depth) >> 7; }
    u16 a = CS16[0x12F6 + 2*idx];                          // atan, 2000h = 45°
    if (DS[0x947D]) a = 0x4000 - a;                        // atan(size/depth) when inverted
    B9D6 = a >> 8;                                         // angular size in 8-px units (0..40h)
}
// (a zero divisor sets B9D6 = 0; unreachable)

// 0e12:28eb
void sprite_draw(u16 k) {
    ES = B9ED = B9EB;
    u8 id = B9D8, mode = DS[0x95E1 + id] & 7;
    u16 sid = (mode < 2) ? id : (mode == 2 ? id - 1 : id - 2);
    u8 s = B9D6; if (s > BA0D) s = BA0D;
    u16 copy = C16[C16[2*sid] + 2*BA0E[s]];
    B9E7 = C[copy]; copy++;
    u8 W = C[copy], H = C[copy+1];
    if (W == 0 || H == 0) return;
    DS[0x9480] = W; DS[0x9481] = H; DS[0xB9DB] = H;        // B9DC stays 0
    u16 d = SPR_ANGLE[k];
    B9E1 = copy + 2;
    u16 si = copy + 2 + B9E7;                              // first stored row
    if (B9D5 & 0x80) {
        DS16[0x947E] = 0x58 - ((s16)(d - DS16[0xBD3D]) >> 6);
        sprite_blit_mirror(k, si);
    } else {
        u16 t = rol16((u16)(d << 2), 1);
        DS16[0x947E] = swap_bytes(t);                      // == (d >> 5) & 1FFh since d % 20h == 0
        sprite_blit_main(k, si);
    }
}
```
The x conversion only works because the angle is always a multiple of 20h (atan tables, B7AF and the heading
are); the port can use `(d >> 5) & 0x1FF`. The result is a 9-bit pixel column (negative values wrap to 100h..1FFh).

```c
// 0e12:2b74 + 2c7c  (mirror variant noted in [..])
void sprite_blit_main(u16 k, u16 si) {
    DS16[0xB9DF] = 0;
    u8 W = DS[0x9480];
    u16 x = DS16[0x947E] - (W >> 1); DS16[0x947E] = x;
    u8 vis = W;
    if (x < 0x140 /*[0x58]*/) { if ((u16)(0x140 /*[0x58]*/ - x) < W) vis = (u8)(0x140 - x); }
    else {
        s16 r = (s16)(x | 0xFE00) + W;                    // assumes x in −256..−1
        if (r <= 0) return;
        vis = (u8)r; DS16[0xB9DF] = (u8)(W - vis);
    }
    DS[0xB9D4] = vis;

    u16 dist = SPR_DIST[k], dy = Y[SPR_INST[k] / 2] - DS16[0x949E], o = 2;
    if ((s16)dy < 0) { dy = -dy; o = 0; }
    u16 num = dy, den = dist;
    if (dy >= dist) { num = dist; den = dy; o ^= 8; }
    u16 idx = den ? (u16)(((u32)num << 9) / den) : 0;     // 0..200h
    s16 y = CS16[0x1AFA + B79F_b[o] + 2*idx] + B78F_b[o]; // pixel angle below the horizon
    s16 r = 0; s8 roll = DS[0x94A0];
    if (roll) {
        r = (s16)(SPR_ANGLE[k] - DS16[0xBA97]) >> abs(roll);
        /* mirror: r = ((s16)(SPR_ANGLE[k] - 0x9400) >> 1) >> abs(roll); */
        if (roll > 0) r = -r;
    }
    y = y + 1 + r - (s16)DS16[0xB9DB] + (s16)DS16[0x9496];   // top row = bottom − H + 1
    /* mirror: y = ((s16)(y + 1 - r - H - DS16[0x948F]) >> 1) + 7;  no BAD4 update */
    if (y < (s16)DS16[0xBAD4]) DS16[0xBAD4] = y;
    if (y >= (s16)DS16[0xBA91] /*[0x13]*/) return;

    u16 rp = B9E1; u8 rep = C[rp];
    while (y < 0) {                                        // top clip
        if (--rep == 0) { rp++; rep = C[rp]; si += W; }
        if (--DS[0x9481] == 0) return;
        y++;
    }
    B9DE = rep; B9E1 = rp;

    // 2c7c [2a9e]
    u16 end = DS16[0xBA93] /*[0x688]*/;
    u16 di = DS16[0xB8D3 + 2*y] /*[B993]*/;
    if (DS16[0xB9DF]) si += DS16[0xB9DF]; else di += DS16[0x947E];
    if (di >= end) return;
    u8 far *fb = MK_FP(DS16[0x90D0] /*[90D2]*/, 0);
    for (;;) {
        for (u8 c = 0; c < vis; c++) { u8 p = C[si + c]; if (p) fb[di + c] = p; }
        di += 0x140 /*[0x58]*/;
        if (--rep == 0) { si += W; rp = ++B9E1; rep = C[rp]; }
        if (di >= end) return;
        if (--DS[0x9481] == 0) return;
    }
}
```
The row copy is unrolled (word pairs with per-byte zero tests) but is exactly "copy `vis` bytes, skip zeros".
Clipping summary: left/right against 0..319 (mirror 0..87) regardless of the view width `BA95`; bottom by the
buffer end offset `BA93` (= `BA91` rows); top by skipping output rows; nothing is drawn when the top row is
≥ `BA91`, when W or H is 0, or when fully off the side.


### 4.14 Objects and vehicles, runtime face colours, near-face hits, per-leg state


Addresses `SSSS:OOOO` (file segments), `DS:xxxx` = DGROUP 1BE4. Object arrays are u16[160] indexed by
the **slot** `bx = 2*i` (i = object index); written below as `flags[i]` etc. All arithmetic 16-bit
unless noted; `>>` on u16 = logical (`shr`), on s16 = arithmetic (`sar`). Hex constants.

Per-frame order (from `frame_update` 76ec → `70cd` → `34c3`):
`70cd` (world tiles + static objects, only rebuilt when the car's cell/octant changes) →
`34c3`: **`4d60`** (`4fc7` parked vehicles, `509b` moving objects, `5ad4` collisions, `5df9` lighthouse) →
**`5e70`** (crossing gates) → `394c` project → `361c` sort → **`34f1`** (only cockpit view) → `373c` →
**`4d6d`** (runtime face colours) → **`5ffe`** (police).

Buffer layout (vertex count `BADA`, face count `BAD8`) after `4d60`:
`[0, BADE/BADC)` world tiles + statics (persist across frames until `70cd` rebuilds) →
`[BADE/BADC, BAE2/BAE0)` parked vehicles (`4fc7`) → `[.., BADA/BAD8)` moving objects (`509b`).
Every object that gets emitted records its ranges in `AFB9/B0F9` (vertices) and `B239/B379` (faces);
`75c1`/`7487` use `BD26` = slot+2 (0 = no object) for that. `70cd` clears `B0F9[]`/`B379[]` of all
160 objects when it rebuilds (`70b8`).


#### Common: model lookup (inline in 52a3, 4db6, 5ad4, 5cf2)
```c
far u8 *model_ptr(int i) {           // m = flags[i] & 3F
  m = flags[i] & 0x3F;
  if (m > 3) return O_seg:(O_off + O_table_word[m]);   // DS:E54C/E54E; offsets relative to table start
  if (m == 3) return DS:D7A4;  if (m == 2) return DS:CEBC;
  return far[CEA4];                  // player POB (models 0,1)
}
// 539d does NOT do this: it always uses the O table (ES preloaded by 4fc7).
```
Vehicle header (objects.md): `nf=b0 nv=b1 nc=b2 nk=b3 nf'=b4 nv'=b5 off=w6`; near vertices at +8.

#### 4fc7 parked_vehicles_emit
```c
es = [E54E];
BADA = BADE;  old = BAD8;  BAD8 = BADC;
if (attract_mode == 0)
  for (i = A477-1; i >= 0; i--) {
    if (i == 0 && outside_view == 0) continue;           // (cmp [948B],bl with bl=0)
    f = flags[i];
    if (!(f & 0x1000) || (f & 0x2000)) continue;
    if (!window(i, &dist)) { AFB9[i]=B0F9[i]=B239[i]=B379[i]=0; continue; }
    if ((f >> 8) == 0x1F) {                              // lightning object
      if ((u8)rain < 6) { clear 4 ranges; continue; }
      cx = ((u16)((u8)(4*i)) << 8) + rand_lo;            // ch = (bl<<1)&FF, bl = low byte of 2i
      if (cx & 0x05D9) { clear 4 ranges; continue; }
      sky_flash = 2;  sfx(9);
    }
    obj_emit_vehicle(i, dist);                           // 539d
  }
BAE0 = BAD8;  BAD8 = old;  BAE2 = BADA;
```
`window` (shared with 509b, inline):
```c
bool window(i, u16 *dist) {
  ax = (s16)((X[i] - car_x) << 1) >> 1;   // 949A; sign-extend 15-bit difference
  ax += view_half;  if (ax > view_x2 /*B6E0, unsigned*/) return false;
  dx = abs((s16)(ax - view_half));
  ax = (s16)((Z[i] - car_z) << 2) >> 2;   // 949C; 14-bit
  ax += view_half;  if (ax > view_x2) return false;
  *dist = dx + abs((s16)(ax - view_half)); // Manhattan, position units → LOD test DX > 200h
  return true;
}
```
Note: LOD distance is from the **car** (949A/949C), not the camera.

#### 539d obj_emit_vehicle (BX = slot, DX = dist)
```c
p = O_off + O_table[flags[i] & 0x3F];         // es:
nf = p[0]; nv = p[1]; v = p + 8;
if (dist > 0x200) { nf = p[4]; nv = p[5]; v = p + 8 + *(u16*)(p+6); }   // far LOD
if (AFB9[i] == BADA && B239[i] == BAD8 && B0F9[i] == nv) {   // same slots as last frame: reuse
  BADA += B0F9[i];  BAD8 += B379[i];  return;
}
vert_overflow = 0;
place_x = X[i]; place_z = Z[i]; place_y = A839[i] >> 3;
place_rot = A979[i] >> 8;  place_pitch = AAB9[i] >> 8;  BD26 = 2*i + 2;
place_nverts = nv;  obj_transform_verts(v);          // 75c1: SI advances to the face array
if (vert_overflow) { B239[i] = B379[i] = 0; return; }
BD24 = 0;  obj_emit_faces(nf);                        // 7487
```

#### 509b moving_objects_update
```c
for (b in {BCAB, BCAC, BCAD, BCAE, BCAF}) if (b) b--;          // u8 saturating
BADA = BAE2;  old = BAD8;  BAD8 = BAE0;
n = attract_mode ? 1 : A475;
for (i = n-1; i >= 0; i--) {
  obj_full_transform = 1;
  if (outside_view == (u8)(2*i)) { obj_full_transform = 0; if (i == 0) continue; }  // true only for i=0 (and i=128) in cockpit view
  if (!(flags[i] & 0x2000)) continue;
  BCC4 = BCC6 = BCC8 = 0;
  bool move = !crash_freeze && (attract_mode || (outside_view == 0 && race_clock_running));
  if (move) {
    traffic_follow_path(i);                    // 5466 (simulation spec): steers, may set BCC3=1, may clear 2000h
    if (!(flags[i] & 0x2000)) continue;        // NB: ranges NOT cleared
    if (!attract_mode) {
      s16 vx = (s8)ABF9[i], vz = (s8)(ABF9[i] >> 8), vy = (s16)(s8)AAB9[i] << 2;
      bool integrate = true;
      if (flags[i] & 0x1000) {
        integrate = false;
        if (i <= 2 && i != 0 && race_computer_cars /*B09*/) {
          if (BCAA[i]) integrate = true;         // opponent allowed to move
          else if (++BCA8[i] >= 0x1E) { BCAA[i] = (u8)(i*0x10 + 0x1E); BCA8[i] = 0; }
        }
      }
      if (integrate) {
        if (flags[i] & 0x8000) { vx >>= 1; vz >>= 1; vy >>= 1; }   // sar
        BCC4 += vx;  X[i] = (X[i] + vx) & 0x7FFF;
        BCC6 += vz;  Z[i] = (Z[i] + vz) & 0x3FFF;
        A839[i] += vy;  BCC8 += vy + (A839[i] & 7);
      }
    }
  }
  if (!window(i, &dist)) { AFB9[i]=B0F9[i]=B239[i]=B379[i]=0; continue; }
  BCCA = AFB9[i];  BCCC = B0F9[i];
  nf = obj_emit_vehicle_moving(i, dist);        // 52a3
  if (vert_overflow) { B239[i] = B379[i] = 0; continue; }
  if (B0F9[i] == BCCC && AFB9[i] == BCCA && BAD8 == B239[i]) BAD8 += B379[i];   // reuse face records
  else { BD24 = 0; obj_emit_faces(nf); }        // 7487
}
if (old != BAD8) facelist_changed = 1;
```

#### 52a3 obj_emit_vehicle_moving (BX = slot, DX = dist) → CX
```c
place_x = X[i]; place_z = Z[i]; place_y = A839[i] >> 3;
place_rot = A979[i] >> 8;  place_pitch = AAB9[i] >> 8;  BD26 = 2*i + 2;
p = model_ptr(i);  nf = p[0]; nv = p[1]; v = p + 8;
if (dist > 0x200) { nf = p[4]; nv = p[5]; v = p + 8 + *(u16*)(p+6); }
vert_overflow = 0;
if (AFB9[i] == BADA && nv == B0F9[i] && obj_full_transform == 0) {
  // translated cache (effectively dead: BCC3 is 1 except slot 0/128 in cockpit view)
  place_nverts = 2;  obj_transform_verts(v);           // reserves nv verts, transforms only 0,1
  for (k = AFB9[i]+2; k < AFB9[i]+B0F9[i]; k++) {      // count B0F9-2
    Xw[k] += BCC4 << 2;  Zw[k] += BCC6 << 2;  Yw[k] += (u16)BCC8 >> 3;
  }
  return (nf << 8) | nv;                               // BUG: CX not converted; SI not at faces
}
place_nverts = nv;  obj_transform_verts(v);
return nf;
```

#### 4d6d obj_runtime_colours_all
```c
strobe_counter++;  tlight_lamp = 0;                   // CH = 0 (CL left from 373c)
if (outside_view == 0 && --tlight_timer == 0) {
  tlight_phase = (tlight_phase + 1) & 3;
  tlight_timer = (tlight_phase & 1) ? 9 : 0x5A;
}
wiper_frame++;
for (i = A473-1; i >= 0; i--) if (B379[i]) obj_face_runtime_colours(i);   // CX carried over
```

#### 4db6 obj_face_runtime_colours (BX = slot; CX = limit, shared across calls)
Record `r` = far `E5B8 + 10*B239[i]`: `+0 w0, +2 v1, +4 v2, +6 w3, +8 colour word`. Colour words are
final pixel pairs (lo = even, hi = odd pixel; palette 0–15 = EGA colours), bypassing `B4B9`.
```c
for (r = first face of i; ; r += 10) {
  t = (r.w3 >> 8) & 0xF8;                      // type<<3
  switch (t) {
  case 0x20: case 0x28: case 0x30: {           // types 4,5,6: lamp by viewing side
      a = (t==0x20) ? 0x0404 : (t==0x28) ? 0x0F0F : 0x0E0E;   // shown when s < 0
      b = (t==0x20) ? 0x0F0F : 0;                             // shown when s >= 0
      v = r.w0 & 0x7FF;
      d = (A979[i] & 0xFF00) + 0x4000 - cam_heading /*9498*/ - vert_bearing[v];
      s = (s8)((d >> 8) + half_fov /*BA99*/);
      r.col = (s < 0) ? a : b; break; }
  case 0x38: r.col = (rand_lo & 4) ? 0x0F0F : 0; break;               // 7 random blink
  case 0x40: r.col = (strobe_counter & 7) == 0 ? 0x0F0F : 0; break;   // 8 strobe
  case 0x48: r.col = (flags[i] & 0x8000) ? 0x0C0C : 0x0404; break;    // 9 brake light
  case 0x50: if (flags[i] & 0x4000) {                                 // 10 beacon (police)
        beacon_colour += 3; if (beacon_colour > 0xF) beacon_colour = 9;
        r.col = beacon_colour * 0x0101; }                             // else leave emitted colour
      break;
  case 0x58: {                                                         // 11 traffic light lamp
      k = tlight_lamp; ph = tlight_phase;
      // lamp k: 0 green A, 1 yellow A, 2 red A, 3 green B, 4 yellow B, 5 red B
      static const u16 col[6] = {0x0A0A,0x0E0E,0x0C0C,0x0A0A,0x0E0E,0x0C0C};
      bool on = k==0 ? ph==0 : k==1 ? ph==1 : k==2 ? ph>=2 : k==3 ? ph==2 : k==4 ? ph==3 : ph<=1;
      r.col = on ? col[k] : 0;
      tlight_lamp = (k+1 > 5) ? 0 : k+1; break; }
  case 0x60:                                                           // 12 crossing flasher
      r.col = (gate_angle && ((frame_counter ^ r_offset) & 2)) ? 0x0C0C : 0x0808; break;
  case 0x68: wipers(i); return;                                        // 13
  default: return;                             // type < 4 or > 13 ends the scan
  }
  if (--CX == 0) return;                       // `loop`
}
```
`r_offset` = the record's offset in the face segment (bit 1 alternates between consecutive records).
Phases: 0 = A green / B red, 1 = A yellow / B red, 2 = A red / B green, 3 = A red / B yellow; green 90
frames, yellow 9. The lamp index runs over all type-11 faces in object order (high index first).

`wipers(i)` (writes into the **model data**, shared by all instances; takes effect when the model is
next transformed):
```c
p = model_ptr(i);  nf=p[0]; nv=p[1]; nc=p[2]; nk=p[3];
anim = p + 8 + 6*nv + 8*(u8)(nf + nc + nk);      // 8-bit sum
f = (rain || snow) ? (wiper_frame & 7) : 0;
src = anim + 12*f;                               // A0 A1 B0 B1 C0 C1
A[0..1] = src[0..1]; B[0..1] = src[2..3]; C[0..1] = src[4..5];   // arrays at p+8, +2nv, +4nv
CX = 2*nv;                                       // leaks into the next object's loop limit
```

#### 5ad4 vehicle_collisions
```c
for (i = A475-1; i >= 1; i--) {
  f = flags[i];
  if (!(f & 0x2000) || ABF9[i] == 0) continue;
  if ((f & 0x9000) == 0x9000) flags[i] = (f & ~0x1000) | 0x0800;
  else flags[i] = f & 0x6FFF;
}
for (i = A475-1; i >= 1; i--) {
  if (!(flags[i] & 0x2000)) continue;
  if ((flags[i] & 0x3F) - 0x12 <= 2) continue;          // trains 12h–14h excluded
  for (j = i-1; j >= 0; j--) {
    if (j != 0 && B379[j] == 0) continue;               // j must be drawn (player always)
    if (abs(X[j]-X[i]) >= coll_prefilter) continue;     // unsigned compares after abs
    if (abs(Z[j]-Z[i]) >= coll_prefilter) continue;
    if (abs((A839[j]>>3) - (A839[i]>>3)) >= coll_prefilter) continue;
    coll_bits = 0;
    p = model_ptr(i); nk = p[3];
    if (nk) {
      box = p + 8 + 6*p[1] + 8*(u8)(p[2] + p[0]);
      for (k = 0; k < nk; k++, box += 8) {
        place_pitch = AAB9[i]>>8; place_rot = A979[i]>>8;
        vert_rotate(box, stride 2) -> (dy, dx, dz);      // 7653
        polar_b = *(u16*)(box+6);                         // r | h<<8
        BCC4 = dx + X[i];  BCC6 = dz + Z[i];  BCC8 = dy + (A839[i]>>3);   // position units
        vehicle_collision_boxes(j);                       // 5cf2
      }
    }
    b = coll_bits;  if (!b) continue;
    if (b & 1) { flags[i] |= 0x9000; if (j) flags[j] |= 0x9000; else crash |= 1; continue; }
    fl = (b & 2) ? 0x9000 : 0x8000;
    a = atan2_xz(X[j]-X[i], Z[j]-Z[i]);
    hi_i = (u8)((atan2_xz((s8)ABF9[i], (s8)(ABF9[i]>>8)) - a) >> 8);   // velocity direction of i
    if ((u8)(hi_i + 8) < 0x10) flags[i] |= fl;
    else {
      hi_j = (u8)((atan2_xz((s8)ABF9[j], (s8)(ABF9[j]>>8)) - a) >> 8);
      if ((u8)(hi_j + 0x88) < 0x10 && j) flags[j] |= fl;
    }
  }
}
```
(i heading at j within ±800h → i stops; else j heading at i within ±800h → j stops.)

#### 5cf2 vehicle_collision_boxes (BX = slot j)
```c
place_pitch = AAB9[j]>>8; place_rot = A979[j]>>8;
p = model_ptr(j); nk = p[3]; if (!nk) return;
box = p + 8 + 6*p[1] + 8*(u8)(p[2] + p[0]);
for (; nk; nk--, box += 8) {
  vert_rotate(box, stride 2) -> (dy, dx, dz);
  ax = abs(dx + X[j] - BCC4);  cx = abs(dz + Z[j] - BCC6);   // s16 abs
  mn = min(ax,cx); mx = max(ax,cx);
  d17 = (u16)(mn + mx) + (u16)(2*mx);            // 17-bit (carry of the 2nd add kept)
  d = d17 >> 2;  if (d > 0xFF) continue;
  dyv = (u16)abs(dy + (A839[j]>>3) - BCC8) >> 1;  if (dyv > 0xFF) continue;
  w = *(u16*)(box+6) + polar_b;                  // r_j+r_i (lo, carry into hi), h_j+h_i (hi)
  r = w & 0xFF; h = w >> 8;
  if (d <= r && dyv < h)                   { coll_bits |= 7; continue; }
  w8 = w << 3; r = w8 & 0xFF; h = (w8 >> 8) & 0xFF;
  if (d < r && dyv < h)                    { coll_bits |= 6; continue; }
  h = (2*h) & 0xFF; r2 = 2*r;
  if ((r2 > 0xFF || d < r2) && dyv < h)    { coll_bits |= 4; }
}
```

#### 5df9 lighthouse_rotate
```c
if (lighthouse_slot == 0xFFFF) return;          // BCD2 = slot of the flags==000Fh object
ang = (frame_counter & 0x7F) << 9;              // one turn per 128 frames
r = lighthouse_radius * 16;  if (r == 0) return;
polar(ang, r);                               // 9460 = r·sin, 9462 = r·cos
v = face_records[B239[i]].w0 & 0x7FF;           // first face of the object
Xw[v] = Xw[v+1] = (X[i] << 2) + polar_a;
Zw[v] = Zw[v+1] = (Z[i] << 2) + polar_b;        // heights untouched
```

#### 5e70 crossing_gate_update / 5f57
```c
if (gate_slot0 == 0xFFFF) { train_near = 0; gate_angle = 0; train_min_dist = 0xFFFF; return; }
g = gate_slot0;  train_near = 0;
for (i = A475-1; i >= 1; i--) {
  if (!(flags[i] & 0x2000) || !B379[i] || (flags[i] & 0x3F) - 0x12 > 2) continue;
  d = abs(((X[g]+0x100) & 0xFE00) - X[i]) + abs(((Z[g]+0x100) & 0xFE00) - Z[i]);
  if (d >= 0x240) continue;
  if (train_min_dist >= d) train_min_dist = d;
  train_near = 1; break;
}
a = gate_angle;
if (a) sfx(7);                                   // bell
if (train_near) {
  if (a < 0x40) {
    if (a < 0x3C && train_min_dist <= 0x60) a += 3;
    a++;  if (a == 0x3E) sfx(8);
  }
} else if (a) a = (a - 1) & 0x3E;
gate_angle = a;
gate_arm(gate_slot0);  if (gate_slot1 != 0xFFFF) gate_arm(gate_slot1);

gate_arm(g):                                     // 5f57
  ang = 0x1000;  if (gate_angle >= 0x10) ang += (gate_angle - 0x10) << 8;   // 22.5°..90°
  if (gate_arm_len == 0) return;
  polar(ang, gate_arm_len);                   // a = r·sin (horizontal), b = r·cos (up)
  v = face_records[B239[g] + 2].w0 & 0x7FF;      // 3rd face: arm tip vertices v, v+1; pivot height v+3
  Yw[v] = Yw[v+3] + polar_b;  Yw[v+1] = Yw[v] + 8;
  c = polar_a; e = 8; hd = A979[g] >> 8;
  if (hd & 0x80) { c = -c; e = -e; }
  if (hd & 0x40) { c = -c; e = -e; Zw[v] = (Z[g]<<2) - c; Zw[v+1] = Zw[v] + e; }
  else           {                  Xw[v] = (X[g]<<2) - c; Xw[v+1] = Xw[v] + e; }
```

#### 5ffe police_update (cockpit view only)
```c
if (outside_view) return;
if (police_cooldown) police_cooldown--;
radar_level = 0;  place_rot = 0 /*pullover count*/;  place_nverts = 0 /*siren count*/;
for (i = A475-1; i >= 1; i--) {
  f = flags[i]; m = f & 0x3F;
  if (!(f & 0x2000) || (m != 0x11 && m != 0x30)) continue;
  if (B379[i] == 0) {                            // not drawn: distance to player (object 0)
    d = abs(X[i]-X[0]) + abs(Z[i]-Z[0]);
    if (d < 0x800) c = 0x21; else if (d < 0xC00) c = 0x11; else continue;
  } else {
    if (race_computer_cars && (f & 0x4000))
      for (s = 1; s <= 2; s++)
        if (abs(X[i]-X[s]) + abs(Z[i]-Z[s]) < 0xD8 &&
            (s8)((A979[i]>>8) - (A979[s]>>8) + 0x40) >= 0) {       // same heading ±40h
          tickets[s]++; sky_flash = 3; pending_msg = 0x2C; police_cooldown = 0x96;
          police_pursuit_clear(); return;         // NB: no siren sfx this frame
        }
    c = 0x6F - (vert_depth[AFB9[i]] >> 7);  if ((s16)c < 0) continue;
  }
  if (c >= 0x20) {
    if (f & 0x4000) place_nverts++;
    if (c >= 0x50 && police_cooldown == 0) {
      bool close = true;
      if (!(f & 0x4000)) {
        if (car_speed_disp < 0x0C) close = false;
        else {
          flags[i] = f | 0x40C0;  ticket_speed = car_speed_disp;
          if ((AE79[i] & 0xFFC0) == 0) AE79[i] += 0x40;
          else if ((s8)(((A979[i] - A979[0]) >> 8) - 0x40) >= 0) { police_turn_slot = 2*i; close = false; }
        }
      }
      if (close && c >= 0x65) {
        place_rot++;
        if (++pullover_ctr >= 0x1E) {            // caught
          sky_flash = 3; pending_msg = 0x21; police_cooldown = 0x96; police_pursuit_clear();
          tickets[0]++;  clock_sec += 20; if (clock_sec >= 60) { clock_min++; clock_sec -= 60; }
          return;
        }
      }
    }
  }
  if (radar_level < (u8)c) radar_level = c;
}
if (place_rot == 0) pullover_ctr = 0;
sfx(place_nverts ? 0x14 : 0x94);                 // siren on / off
```

#### 34f1 near_face_hits (cockpit view only)
```c
key = sort_keys; ptr = key - 0xC80;              // seg E5BA, u16 arrays, BAD8 entries
for (k = 0; k < face_count && key[k] <= near_depth; k++) {
  r = ptr[k];  t = r.w3 >> 11;  if (t == 0) continue;
  if (t >= 0x18) { if (t > 0x1B) continue;
    if (t == 0x1A) plate_fold_down(r); else knockover_hit(t, k);
    continue; }
  if (t > 2) continue;                           // only types 1, 2
  n = r.w0 >> 14;  lo = 0xFFFF; hi = 0;
  for (m = 0; m <= n; m++) { y = Yw[r.word[m] & 0x7FF]; hi = max(hi,y); lo = min(lo,y); }  // unsigned
  y0 = car_y /*A1F3[0]*/;  bot = (s16)(y0 - near_dy) < 0 ? 0 : y0 - near_dy;  top = y0 + near_dy;
  if (lo > top || hi < bot) continue;
  if (t == 1 && abs(car_speed /*1296*/) < 0x14) { bounce_back(); continue; }
  crash = t; crash_type = t; crash_face = (r - E5B8) / 10;
}
```

#### 4590 plate_fold_down (type 1Ah; BP = &r.w3)
```c
sfx(4); effect_ctr = 5; bump_ctr = 3;
R = ((r.col >> 8) == 6) ? r : r - 10;            // pair: first record has colour hi byte 6
R.w0 &= 0xE7FF;  y = Yw[R.w0 & 0x7FF] + 6;
R.w3 = (R.w3 & 0x7FF) | 0x1800;                  // type → 3
S = R + 10;  S.w0 &= 0xE7FF;
v = S.w0 & 0x7FF; dy = Yw[v] - y;     Yw[v] = y;   Zw[v] += dy;
v = S.v1 & 0x7FF; y++; dy = Yw[v] - y; Yw[v] = y;  Zw[v] += dy;
S.w3 = (S.w3 & 0x7FF) | 0x1800;
```
Changes persist in the world buffers until `70cd` rebuilds the cell set (then the plate stands again).

#### 4631 knockover_hit (CH = type 18h/19h/1Bh)
```c
f = (ptr[k] - E5B8) / 10;                        // face index
for (i = A473-1; i >= 1; i--) {
  if (!B379[i] || f < B239[i] || f >= B239[i] + B379[i]) continue;
  lo = (u8)flags[i] | 1;
  if (t == 0x18) lo = 9; else if (t == 0x19) lo = 0x0B;   // knocked-down sign models O 9 / O 11
  *(u8*)&flags[i] = lo;  BD34 = 0xFFFF;          // force world rebuild next frame
  if (t == 0x1B && (lo & 0x3F) == 0x39) {        // seagull (38h/39h) takes off
    flags[i] = 0x3039; ABF9[i] = 0; AE79[i] = 0; sfx(0x12);
  } else sfx(5);
  effect_ctr = 5; bump_ctr = 5; random_damage(); return;
}
```

#### 0edb random_damage / 0fd6 bounce_back
```c
ax = rand_lo >> 2;  hb = ax >> 8;  b = ax & 1;   // b = bit 2 of rand_lo
if ((hb & 0x65) == 0) { sfx(0x0C); damage_bits |= 4 << b; }
else if ((hb & 0x1F) == 0) { show_message(0x2E); damage_bits |= 1 << b; }
// 0e12:0f16.. belongs to the second entry 0ec5 (not reached from 0edb)

bounce_back: effect_ctr = 4; memcpy(DS:125E, DS:1282, 12);
```

#### 22fe atan2_xz / 234b polar
```c
u16 atan2_xz(s16 x /*CX*/, s16 z /*DX*/) {
  o = 0; if (x < 0) { x = -x; o = 6; } if (z < 0) { z = -z; o ^= 2; }
  if (x >= z) { swap(x, z); o ^= 8; }            // now x <= z (unsigned)
  if (z == x) idx = 0x1FF; else if (z == 0) idx = 0;
  else idx = (u16)(((u32)x << 16) / z) >> 7;     // 0..1FF
  return T[idx*2 + OFS[o/2]] + BASE[o/2];        // T = cs:12F6 words; OFS = DS:B79F, BASE = DS:B7BF
}
// OFS {0,402h,0,402h,402h,0,402h,0} selects T+ (atan) or T− (cs:16F8 = −T+); BASE {C000,4000,4000,C000,0,0,8000,8000}
// Result = atan2(−z, x) in 10000h/turn (0 = +X, 4000h = −Z, 8000h = −X, C000h = +Z).
// T+[k] = atan(k/512)·10000h/2π truncated to multiples of 20h (max err 20h): copy the table.

void polar(u16 ang, u16 r) {                  // far, cdecl
  a = (ang >> 7) & 0x1FF;  k = a & 0x7F;  if (a & 0x80) k = 0x80 - k;
  s = (u32)SIN[k] * r >> 16;  if (a & 0x100) s = -s;       // SIN = cs:0FF2, 129 words
  c = (u32)SIN[0x80 - k] * r >> 16;  q = (a >> 7) & 3;  if (q == 1 || q == 2) c = -c;
  polar_a /*9460*/ = s;  polar_b /*9462*/ = c;
}
// SIN[k] = round(65536·sin(k·π/256)), SIN[128] = FFFFh.
```

#### 261c build_colour_remap (far E5B0[0..31])
```c
for (c = 0; c < 32; c++) {
  v = c;  hi = 0x0F;
  if (colour_mode && rain && c == 8) { v = 0; goto store; }  // (dl = 0)
  if (!colour_mode) { hi = 7; if (c == 8) v = 0; else if (c == 7) v = 8; }
  if (snow && (c == 2 || c == 3 || c == 0x0A)) v = hi;
store: remap[c] = v;
}
```
Called by race_run (each weather ramp step), 2465, key handlers 05e3/0606/0667.

#### 255e leg_state_reset / 2465 life_reset / 03d4 detail_apply / 25f4
```c
leg_state_reset:
  do { s = rand(); } while (popcount16(s) < 10 || popcount16(s) > 12);
  sprite_seed = seed_B6ED = s;
  view_row_tables_init();  life_reset();
  route_index = 0; tickets[0..2] = 0; BCA8..BCAF = 0; clock_min = clock_sec = 0;
  B6D8 = BC7B = BB34 = police_cooldown = 0; BCB1 = 0; BCB9 = BCBA = 0;
  opp_time[0..5] = 0;  rain = snow = 0 (words BC75, BC77, BC79 = 0);
  car_x = sprite_x[0]; car_z = sprite_z[0];

life_reset:
  if (B9D3 || B78D) {
    X[0] = sprite_x[0] = B9CB;  Z[0] = sprite_z[0] = B9CD;  sprite_y[0] = B9CF;
    A839[0] = (B9CF - car_eye_height) << 3;  9491 = B9D1;
    h = (B9D1 >> 8) - 0x40;  A979[0] = (h << 8) | h;
  }
  build_colour_remap();  detail_apply();
  9486 = 948D = BCDD = 94AE = 0;  B78E = B9D3 = B6D4 = B6D5 = B704 = sky_flash = gate_angle
    = train_near = 9493 = BCDC = B708 = B705 = B706 = throttle = crash_freeze = crash = B78D
    = outside_view = effect_ctr = race_clock_running = BC7C = 0;
  B709 = B70F = 0xFF;  tlight_timer = 1;  gear = 1;  BAAD = 0x32;
  BB35[0..3F] = 0;  sprite_id[0..8] (9A73) = 0;  police_pursuit_clear();

detail_apply:
  if (detail > 1) { v = 0x700; seed = B6ED; } else if (detail == 1) { v = 0x440; seed = B6ED & 0x5555; }
  else { v = 0x300; seed = 0; }
  view_half = v; sprite_seed = seed; view_x2 = 2*v; view_x4 = 4*v;
  keep = (skill < 3) ? 2 : (skill < 6) ? 3 : return;
  // traffic thinning: in each group of 4 objects (i = A475-1 down to 3), keep the first `keep`
  ctr = 5; left = keep;
  for (i = A475; ; ) {                  // first step only decrements ctr
    if (--ctr == 0) { ctr = 4; left = keep; }
    if (--i <= 2) break;
    if (--left < 0 && (flags[i] & 0x3F) - 0x12 > 2) flags[i] &= 0x3F;   // removed (not trains)
  }

view_row_tables_init: for k<96: B8D3[k] = k*0x140;  for k<19: B993[k] = k*0x58;
```

#### 61fd opponent_times_finalize / 624a opponent_time_estimate
```c
61fd: est(slot 1, t=&BCBB); est(slot 2, t=&BCBE);
      E80F = BCBD; E810 = BCBC; E811 = BCBB; E817 = hi(95AB); E818 = lo(95AB);
      E82A = BCC0; E82B = BCBF; E82C = BCBE; E832 = hi(95AD); E833 = lo(95AD);

est(i, t):                                       // t = {frac, sec, min}
  fr = t[0]; sec = t[1]; mn = t[2];
  if (!(fr | sec | mn)) {                        // opponent not finished
    d = abs(X[0]-X[i]) + abs(Z[0]-Z[i]);
    v = (u16)(((skill >> 1) + 4) * speed_class[3] /*95B5*/) >> 4;
    n = ((d > v) ? d / v : 0) + 1;
    if (n < 0x700) { sec = n / 7; fr = n % 7; } else sec = 0xFF;   // u8 quotient/remainder
    mn = 0;  if (sec & 0x80) mn += 4;  sec = (u8)(sec << 1);
    while (sec >= 60) { mn++; sec -= 60; }
    sec += clock_sec; if (sec >= 60) { mn++; sec -= 60; }
    mn += clock_min;  fr &= 7;
    for (k = tickets[0]; k; k--) { sec -= 20; if ((s8)sec < 0) { sec += 60; mn--; } }
  }
  for (k = tickets[i]; k; k--) { sec += 20; if (sec >= 60) { sec -= 60; mn++; } }
  t[0] = fr; t[1] = sec; t[2] = mn;
```


-----------------------------------------------------------------------------------------------

## 5. Data used (file formats are in FORMATS.md / port/formats — cited, not repeated)

| Data | Source | Used by | Reference |
|---|---|---|---|
| road tiles `SCENETTT.BIN` (types 00–3F), `<scene>T.BIN` (40–7F) | far `E7E0`, `E770` | `72f8` (4-byte header, alias entries ≤ 10h cut `BD24` faces) | objects.md "Object table", "Header, static object" |
| objects `SCENETTO.BIN` / `<scene>O.BIN` | far `E54C` | `72f8` (AH=1), `539d`, `52a3` (8-byte vehicle header, far LOD) | objects.md |
| car `.POB` | far `CEA4`, near `CEBC`, `D7A4` | `52a3` | objects.md |
| leg file `<scene>A..E.DAT` → `DS:9592..B6C9` | map `9671`, objects `A473..`, sprite instances `9A71..`, colour tables `B4B9`/`B6B9`, sky/ground colours `95C0..95C4`, point/line sizes `95D9`/`95DD`, sprite params `95BF`, `95CD`, `95CF`, `95E1`, `9611`, drift `95B7/B9`, sun octant `95BD`, colour mode `95C7` | world.md "Leg map file" |
| sprite set `<scene><d>.DAT` → `DS:2500` | prescaled into far `E53C` | 4.13 sprites | world.md "Sprites" |
| executable tables | CS `0e12:0FF2`, `10F6`, `12F6`, `1AFA`, `267E`, `7004`, `7068`, `0F81`; DS `B78F`, `B79F`, `B7CF`, `B851`, `BA0E`, `BA3F`, `BD01`, `BAE4`, `94CF..9561` (overlay vertex lists), `BB14` | see 4.5, 4.4, 4.13, 4.12 | this spec |

Face record (runtime, far `E5B8 + 10·n`): `+0 w0` (bits 14–15 count−1, 13 farthest-vertex sort / one-sided
lamp, 11–12 size index, 0–10 vertex), `+2 v1`, `+4 v2` (colour bits removed), `+6 w3` (bits 11–15 type,
0–10 vertex), `+8` colour pair. Extraction of all CS/DS tables: image offset = `seg·16 + off` in
`work/TDIII_unp.exe` (DS = 1BE4h → image `1BE40h + off`); `B7CF/B851/B78F/B79F/BA0E/BA3F/BD01` are initialised
data, `B7AF` is rewritten by `409c`.

## 6. Hardware / DOS dependencies → SDL3

| Original | Where | SDL3 port |
|---|---|---|
| RAM view buffer V (7800h) and mirror M (688h) addressed through segments `90D0`/`90D2`, row tables `B8D3`/`B993` | all rasterizers | two plain byte arrays (320×96, 88×19) with the same row tables; keep the 1-byte-minimum span quirk and the 320-byte stride |
| Self-modifying clip immediates (`409c` via `DS:BAE4`) | `41d4`, `43db`, `44c2`, `3ebf`, sprites `2b74` | variables `W32`, `W32M` set by `view_setup()` |
| Code-segment lookup tables | `0e12:0FF2..1AFA`, `267E`, `7004`, `7068`, `0F81` | generated `const` arrays extracted from the exe (a small tool script, like the TD2 tables); `12F6`/`1AFA` can also be computed (formula in 4.5) |
| Mode 13h page 0 (A000h) persistent between frames | present `13d8`/`17cd` | a persistent `screen[64000]`; implement both copies byte-exactly (partial row copy from `min(BAD4,BAD6)`, mirror hole) and convert through the palette into an SDL streaming texture once per frame |
| Vertical-retrace busy wait (`in 3DAh`) | `17cd` | drop; present once per game frame (vsync optional) |
| CRTC start address shake (`1776:0008`) | `0fa1` | offset the texture source by `y·320 + (x & ~3)` bytes when presenting |
| `div`/`idiv` traps on overflow (INT 0) | `394c`, `39fd`, `6316`, rasterizer slopes | compute in 32 bits and truncate to 16; guard divisions by 0 and results that would overflow (the original would crash — should never happen with shipped data) |
| 145.6 Hz timer, ≥ 5 ticks per frame | race loop | frame pacing 34.34 ms (platform spec) |
| E776==0x13 disables shake | overlays / present | treat as VGA: always shake |
| CS-resident replay ring (0AB4:020F) and CS tables (0F81, 0FF2) | overlays / present | plain arrays |
| LZ decompressor clobbering M (BD3F) | overlays / present | keep the BD3F semantics (one grey mirror frame after any decompression) or simply set BD3F where 0ab4:0047 is called |

## 7. Timing

* Everything in this spec runs **once per frame** (frame_update + frame_draw), paced to ≥ 5 ticks of the
  145.6 Hz timer (≤ 29.1 fps). Nothing runs in an interrupt.
* Frame-count driven animation: `BD39` (blink of colour 010Fh, odd frames), strobe counter (1 in 8), traffic
  light timers (5Ah / 9 frames), wiper phases, windscreen drop ageing, sprite animation countdowns (`9611`),
  sprite drift (`95B7/95B9` per frame), sky flash (`BAA6` frames), screen shake (10h frames), police beacon.
  All are per rendered frame, so a faster port must keep the 34 ms frame step for identical behaviour.
* Incremental work: the static world list is rebuilt only on a cell/octant change (`70cd`), then fully
  quicksorted; on other frames the face order from the previous frame is repaired by bubble passes (`7b06`),
  which can leave a few faces out of order for a frame (see the 2-element quirk). Sprites likewise (`312e`).
* `BAD6` (previous frame's top row) makes the V → screen copy depend on the previous frame.

## 8. Differences from Test Drive (1987) / Test Drive II

Nothing of this engine carries over from TD1/TD2: those draw a 2D road from per-row tables; TD3 has a
polygon world with an angular (cylindrical) projection, painter sorting, a second 3D pass for the mirror,
and a single dithered span filler. The TD2 port's **structure** is reusable (frame loop, RAM view buffer
copied to the screen once per frame, snapshot testing), and so is its approach to CS tables (extract to C
arrays). The two-colour checkerboard (EGA-style dither expressed in VGA palette pairs) has no TD1/TD2
counterpart.

## 9. Open questions

1. **Tilted-horizon sky (373c)**: with a nonzero roll and nothing drawn above the horizon in the previous frame,
   the sky triangle lands at buffer rows 0.. instead of at `BA9E` (relative rows passed to `3fe5` whose
   "top ≤ 0" branch sets `BA9C` to the relative value). The rows between the higher end and the tilted line then
   keep stale content. Faithful as written; check in DOSBox with a banked car (roll comes from `6421` via `BCDC`
   → `9493`).
2. **Face bubble sort quirk (7b06)**: the pass loop stops when the unsorted range is ≤ 2 elements, so a pair can
   stay swapped until the next frame. The sprite bubble sort (`797b`) does not have this quirk. Port both as is.
3. **Mirror roll** (`8b5f`, `7b9b`, sprites `2999`): the roll term ignores the roll sign and uses the fixed
   centre 9400h (correct only for the full-width view). Faithful quirk.
4. **Front test constants 2800h/5400h are not patched**: in the half (240 px) and attract (256 px) views faces are
   accepted up to 320 px and then clipped by the patched span clip; sprites are clipped at 320 px only (they can
   write V columns beyond the narrow view, which are never copied to the screen). Harmless, keep.
5. `948C` is only ever cleared (always 0): the "freeze" branches in `77d5`/`76ec` are dead in the shipped game.
6. `13d8` subtracts the vertical centring offset `(60h − BA91)/2` from the skip count even when `09C4 = 0`
   (half window: −16), i.e. in the half view it also copies up to 16 rows above the rows drawn this frame; they
   hold whatever V had there from earlier frames (normally sky). Reproduce; verify in DOSBox.
7. Division overflow: `394c` (distance) and the slope `idiv`s could trap on extreme geometry; not observed with
   shipped data. The port must not crash (clamp) — confirm no visible difference.
8. `95D9`/`95DD` size units: point radius/line half-width is `atan(size/dist)` in the same angular units as x, so
   sizes are world units (position·4). Line widths `95DD` = leg file +4Bh: values not yet listed in world.md.
9. Dither phase: the span writer puts the **low** byte of the pair (the colour of code `w1`) on pixels with
   (x ^ y) odd; objects.md calls `w1` the "even pixels" code. Same picture up to a one-pixel shift of the
   checkerboard — use the code's rule.
10. The `BA85` ground height and `BA9B` surface come from the **renderer** (the face around the camera): the
   physics depends on drawing. With `detail`/view changes (cells not emitted) the ground face could be missing
   (then `BA85 = 0`, `6421` reuses `BA87`). Keep the coupling.

Sprites (4.13):


* `94B0` has no writer (static 0): confirm with a Ghidra xref that no indirect write exists.
* Class 40h sprites keep class 40h after `id |= 1`, so while within `95CF` they re-trigger sound, `947C` and
  `0e12:0edb` every frame. Real behaviour or are such sprites never within range twice? Class 20h becomes id 1Fh
  (class 0) and stops.
* The horizontal clip is fixed at 320 px (mirror 88 px) and the main-view cull at angle < 3800h. In the narrow view
  modes (`9C4 ≠ 0`: 256 px, `B6DC ≠ 0`: 240 px, set by `409c`) sprites can therefore be drawn into buffer columns
  outside the view. Harmless only if those columns are covered later (HUD / dashboard): check in the frame-buffer spec.
* The mirror roll term uses the fixed centre 9400h, not `BD3D`-derived: only correct for the 320-px view.
* ~~`E338` in VGA~~ answered: the VGA build runs mode 13h and `E338 = 13h`, so `276e` never remaps on VGA.
* `BCD8` is the crossing-gate arm angle (4.14, `5e70`): sprite 10h of class 60h does not crash the car while the arm is less than 30h down.
* Answers to world.md questions: the renderer ignores bits 6-7 of the row-length words and the record's `+6` word;
  `95CD` is a *minimum* projection distance (not a limit); `95E1` bits 3-7 are the world size (`<< 3`).
* The special instance slots 1..8 are identified from their writers (`0ab4`, `6e92`); their exact on-screen
  behaviour (debris arcs, sun/moon placement from `95BD`) belongs to the crash/sky sections.

Objects (4.14):


* `509b`: `BCC3` is forced to 1 unless `outside_view == (u8)(2i)`, so the translated-vertex cache of
  `52a3` is only reachable for slot 128 in cockpit view, and there it returns CX = nf:nv and SI not at
  the faces (would break 7487 if the face cache misses). Treat as dead: always full transform.
* Face/vertex caches (509b, 539d) compare only slot positions and vertex count: a LOD switch where near
  and far `nv` are equal but `nf`/faces differ would keep stale faces. Port: always re-emit.
* `4db6` loop limit CX is uninitialised (CL left by 373c/7b9b, CH=0; 0 → 65536) and after a wiper
  object becomes 2·nv. The scan is not bounded by `B379[i]`: runtime faces must be the object's
  first faces, and the scan can run into the next object's records. Port: scan while type ∈ 4..13
  within `B379[i]`, confirm no model relies on the overrun.
* Stale ranges: objects dropped by 509b after `5466` clears 2000h keep `B239/B379`; `4d6d` and `4631`
  may then touch records now owned by other objects. Port: keep explicit per-frame ranges.
* `95D5`/`95D6` are the lighthouse beam radius/16 and gate arm length (world.md calls them speeds);
  `95AB`/`95AD` go into results records +8/+9 (world.md says sky/horizon) — meaning Unknown.
* Lamp side test uses `BA99` (set by `409c`: 10h/14h/0Fh) with `vert_bearing` from `394c`; exact
  geometric meaning (answered by 4.5): `BA99<<8` is the view-centre angle, so `sx[v] − BA99<<8 + cam_heading` is the bearing of the
  vertex; the test compares the lamp's facing (object heading + 4000h) with that bearing, i.e. which side of the lamp faces the eye.
* `E55F` (speed units), `BCB9/BCBA`, `BB35[64]`, `B9D3/B78D` restart semantics: simulation/game_flow.
* Flag `0800h` (set by 5ad4 when 8000h+1000h) and `1000h` hold semantics: simulation spec.

Overlays / presentation (4.12):


1. 13d8 subtracts the vertical centring offset `(0x60−BA91)/2` from the skip count even when 09C4=0 (half
   window: skip −16) — intended? The port should reproduce it or always copy all rows (safe only if V rows
   above BAD4 are valid sky — check with the 373c spec).
2. 4c51 masks the recorder index with 0x7FF while 0ab4:1220 uses 0xFFF.
3. Wiper wipe pivots in 47fc are x=47 and x=174 (0x2F, +0x7F) but the drawn right blade is shifted +130.
4. Direction-bit meaning of `cl` in 0751 (built from DS:915C | DS:947B) — likely bit0 up, bit1 down, bit2
   left, bit3 right; confirm in the input spec.
5. 7b9b ignores the sign of the roll (mirror horizon always tilts the same way) — faithful bug?
6. Snow flakes' ^0x4000 copies clip at 0x7800 even in the half window (can write V rows 64..95, which the
   half-window copy never shows — harmless).
7. Page 1 being current during the 3D pass: no 3D routine (faces, sprites, overlays) draws through the library;
   all write V/M directly. Page 1 only matters for library calls made between frame_update and frame_draw
   (key handlers in race_input(0)); message boxes (0000:179c) select page 0 themselves.
8. DS:95BD & 7 = sun/moon octant around the camera (camera_pos_update pseudocode); sprite slots 7/8 are the smoke puff at the
   crash object and the sun/moon (4.13 special instances).
