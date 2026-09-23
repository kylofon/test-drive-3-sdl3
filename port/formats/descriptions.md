# Description and configuration files

Car `.LST`, scene `.LST`, `SCENExx.HI`, `TD3.CFG`, `PLAYDISK.DAT`, `MASTERQ.BIN`.
Addresses as in `port/RE_GUIDE.md`. Status tags: **Verified** (decoded and checked against data,
pictures or the code path end to end), **Likely**, **Unknown**. Symbols: `port/spec/descriptions_symbols.csv`.
Dump tool: `python tools/td3car.py [GAME_DIR [OUT_DIR]]` → `work/desc/*.txt|json`, `cars.txt`
(all cars side by side), `summary.txt`.

All files are read with plain `fopen/fread` (MS C RTL: `1940:0308` fopen, `1940:0334` fread,
`1940:0526` fwrite, `1940:0240` fclose) straight into fixed DGROUP globals, one `fread` per block.
Multi-byte values are little-endian. A file is laid out exactly as the DS blocks it fills, so the
port can keep the same structs.

## Car `.LST` (675 bytes) — loader `01f4:29f0` car_lst_load (Verified layout)

`car_lst_load(slot, "A:CCERV.LST", name_dst, pics_dst)` builds the name from the PLAYDISK car slot
(`DS:09E0 + 6*slot`, must start with `C`) and reads 10 blocks. Callers: `01f4:28ae` (startup, selected
car `DS:0AFE`), `01f4:2f0e` (PLAYDISK screen: loads **every** car in turn to list its name, then
reloads the selected one), `01f4:3332/3396` (car select next/prev), `01f4:0f4e` (top-score car
names). `0792:0d2e` reads only the first 13h bytes of the two opponent cars' `.LST` (names to
`DS:2286` / `DS:229B`).

| File | Size | DS | Content |
|---|---|---|---|
| 000 | 13h | `2264` (name_dst) | name, NUL-padded: "Chevrolet CERV III" |
| 013 | 13 × u16 | `0B50` (pics_dst) | picture pair counts |
| 02D | 42h | `E564` | gearbox and shift lever |
| 06F | 7 | `E541` | unused colours |
| 076 | 5 × u16 | `E77E` | steering-wheel marker |
| 080 | 10 | `CEAE` | dashboard colours |
| 08A | 20 × u16 | `CC64` | gauges |
| 0B2 | D9h | `94B8` | units, odometer, cockpit view |
| 18B | 35 × i16 | `1200` | physics |
| 1D1 | 15 × 14 | `074C` | archive directory `d` part (FORMATS.md) |

### 013 picture pair counts → `DS:0B50` (Verified)

(colour,count) pair count passed to the RLE draw `01f4:59ac` for each car picture. All 5 cars × 12
pictures match the decompressed sizes exactly (`td3res.lzw_decode` length / 2).

| DS | Picture | Drawn by |
|---|---|---|
| 0B50 | `.ICN` 208 wide | `01f4:33f8`, `01f4:0a96` |
| 0B52 | `.SIC` icon 72 wide | `01f4:144c` (menu) |
| 0B54 | — 0450h in every car, no reader | Unknown (leftover) |
| 0B56 | `.BIC` 112 wide | `01f4:33f8`, `01f4:0a96` |
| 0B58 | `.SID` 112 wide | `01f4:33f8` |
| 0B5A | `.TOP` 320 wide | `0792:1c66` |
| 0B5C | `1.BOT` | `0792:1c66` |
| 0B5E | `2.BOT` | `0792:1c66` |
| 0B60 | `L.BOT` | `0792:0922` |
| 0B62 | `R.BOT` | `0792:0922` |
| 0B64 | `.ETC` gear-shift picture 56 wide | `0792:0922` |
| 0B66 | `FL1.LZ` spec card page 1, 208 wide | `01f4:33f8` |
| 0B68 | `FL2.LZ` spec card page 2 | `01f4:33f8` |

### 02D gearbox → `DS:E564` (Verified for manuals, Likely for the CERV)

| Off | DS | Type | Meaning | Readers |
|---|---|---|---|---|
| +00 | E564 | u8 | highest gear index; gear `DS:B6D7`: 0 = R, 1 = N, 2.. = 1st.. (7 CERV, 6 others) | `0e12:0156`, `0188` |
| +01 | E565 | u8[8] | shift-lever animation step for each gear (R,N,1..6) → `DS:CC57` | `0e12:01c5`, `0792:0922` |
| +09 | E56D | (u8 dx,u8 dy)[28] | knob offset per animation step, knob drawn at (dx+208, dy+175) on the `.ETC` picture | `0792:1156` |
| +41 | E5A5 | u8 | gears per lever range: 1 = plain sequential manual; 3 = CERV | `0e12:0188`, `0229`, `095a` |

`0792:10c8` walks `DS:CE94` one step per frame toward `DS:CC57`, redrawing the knob, so the lever
slides through the gate. Manual cars have H-pattern paths (e.g. NSX steps R0 N3 1:7 2:11 3:16 4:20
5:25). The CERV has a straight slider (18,0)…(18,24) with steps R0 N4, gears 1–3 at 8, 4–6 at 12,
and `E5A5 = 3`: the gear-up/down keys jump between the two ranges and the auto-shift logic stays
inside a range, matching the spec card "3-speed Turbo-Hydramatic, 2-speed gearbox" (**Likely**).

### 06F unused → `DS:E541` (Verified unused)

`0F 07 08 00 0C 04 04` in every car; nothing reads DS:E541–E547. The shift-knob colours hard-coded in
`0792:0922` / `0792:1156` are the same values (0Fh, 7, 8, 0, 0Ch, 4).

### 076 steering-wheel marker → `DS:E77E` (Likely)

Drawn by `0792:155e`, erased/redrawn by `0792:1310` whenever the wheel position `DS:E33A` (0–32,
16 = centre, set by the steering keys/joystick in `0e12:09d6`) changes.

| DS | Meaning | CERV |
|---|---|---|
| E77E | colour | 12 |
| E780 | radius (px, polar length for `0e12:234b`) | 92 |
| E782 | angle per step, 1/65536 turn: angle = E782 × (2·E33A − 32) | 256 |
| E784 | centre x | 116 |
| E786 | centre y on the cockpit page | 205 |

`0792:1310` also switches the dashboard between three rim positions (E33A ≤ 7, ≤ 24, > 24) by
copying from the second page (`0792:1488`).

### 080 colours → `DS:CEAE` (Verified readers, names Likely)

| DS | Reader | Meaning |
|---|---|---|
| CEAE, CEAF | `0e12:0db4` | radar-detector bar colours (bar count `DS:B710 >> 4`, beeps) |
| CEB0, CEB1 | — | no reader |
| CEB2, CEB3 | `0792:16c4` | odometer digits / background |
| CEB4 | `0792:15d4` | tachometer needle |
| CEB5 | `0792:18da` | speedometer needle |
| CEB6 | `0e12:0cbe` | top bar 2-digit readout of `DS:B70E` (a countdown) |
| CEB7 | `0e12:0b85` | top bar race clock `DS:B70C:B70B` |

### 08A gauges → `DS:CC64` (Verified against the dashboard pictures)

Angles are 16-bit binary angles (10000h = 360°) fed to `0e12:234b` (polar → `DS:9460/9462`) and
drawn as a line from the centre (`16d9:0002`). Needle centres match the dials in `<car>1/2.BOT`
(CERV: speedo at x 92, tach at x 154; NSX the other way round).

| DS | Meaning | CERV | NSX |
|---|---|---|---|
| CC64 | tach needle length | 14 | 15 |
| CC66 | tach angle per step, step `DS:E5AE` = rpm >> 5 | 0600h | 05B0h |
| CC68, CC6A | tach centre x, y | 154,159 | 92,154 |
| CC6C | speedo needle length | 14 | 15 |
| CC6E | speedo angle per step, step `DS:E55F` = speed >> 2 (max 31) | 0648h | 0700h |
| CC70, CC72 | speedo centre x, y | 92,159 | 138,154 |
| CC74 | 152 in every car, no reader | | |
| CC76 | tach step clamp (needle stop), read by `0977:0008` | 28 | 34 |
| CC78, CC7A | odometer x, y (`0792:16c4` skips when y = 0) | 286,196 | 276,192 |
| CC7C, CC7E | tach backing rectangle x, bottom y | 128,168 | 72,168 |
| CC80, CC82 | speedo backing rectangle x, bottom y | 64,168 | 120,168 |
| CC84, CC86 | tach / speedo rectangle widths | 48,48 | 40,40 |
| CC88 | speedo zero angle = −CC88 × 500h | 19 | 20 |
| CC8A | tach zero angle = −CC8A × 500h | 16 | 16 |

The backing rectangles are saved to the spare page in `0792:0922` and restored by `0792:15d4 /
16c4 / 1488` before the needles are redrawn (the wheel rim can cover them).

### 0B2 units / view block → `DS:94B8` (partly Verified)

Identical in every car except the first 5 bytes and `94CB`.

| DS | Type | Meaning | Status |
|---|---|---|---|
| 94B8 | u8 | 1 = imperial (CERV, NSX, Stealth), 0 = metric (Diablo, Mythos). Selects `DETAIL1.LZ` vs `DETAIL2.LZ` (`01f4:470e`); leg distance/speed are converted km → miles (×5/8) in `main`, and back ×8/5 for display (`01f4:4d7e`) | Verified |
| 94B9 | u32 | odometer step: velocity units accumulated per 1/60 odometer unit (`0792:1952`): 50000h (mile) vs 31999h (km), ratio 1.6093 | Verified |
| 94BD | i16 | 1C00h: impact threshold (`0977:0008`, \|v\|>>4 × 5 → crash sound 13h / `0e12:0ec5`) | Likely |
| 94BF | i16 | 46: eye height above the car (camera y = car y + 94BF) | Likely |
| 94C1, 94C3 | u16 | 60h, 28h: depth / vertical band limits in `0e12:34f1` | Unknown |
| 94CB | u16 | colour pair of the cockpit polygon drawn into the 3D view (`0e12:4bf3`; 0909h CERV/Stealth, 0C0Ch others) | Likely |
| 94CD.. | | 4-point polygons (`94CF`, `0e12:4b37`), 12-point cockpit outlines (`9531`, `9561`, `0e12:4bf3`), `94E7/94E9/94EB/9527` (`0e12:4a5c`, `47fc`) | Unknown (render3d) |

### 18B physics → `DS:1200` (Likely unless noted)

Read almost only by the car simulation `0977:0008`. Engine speed `DS:129C` and speed `DS:1296`
units were calibrated against the tach/speedo faces (needle zero and step vs printed scale):
**rpm ≈ 129C × 10**, **mph ≈ 1296 × 1.64**. Checks: `1244 × 1.64` = 223/161/210/184/157 mph vs
the spec cards' 225/167.6/202/180/160; `1204/100` = 1.11/0.95/0.91/0.89/0.93 g vs 1.1/.90/.87/.87/.89.

| DS | File | Name | Meaning | CERV NSX DIAB MYTH STEL |
|---|---|---|---|---|
| 1200 | 18B | grip_scale | multiplier in the max yaw-rate formula `0e12:23b1` = g·1200/4·E7E8/250·1238/speed/2 | 600 ×5 |
| 1202 | 18D | engine_drag_lin | engine drag `DS:E548` = (rpm² + 1202·rpm) / 1208 | 48 ×5 |
| 1204 | 18F | grip_slide | lateral grip limit: sliding starts above it (**≈ published lateral g × 100**) | 111 95 91 89 93 |
| 1206 | 191 | grip_recover | grip regained below it | 91 77 72 70 75 |
| 1208 | 193 | engine_drag_div | see 1202 | 300 ×5 |
| 120A | 195 | auto_upshift | auto-shift up at rpm ≥ this (`0e12:095a`) ≈ 7500 rpm | 750 ×5 |
| 120C | 197 | auto_downshift | down when rpm < this and throttle 0 ≈ 3000 rpm | 300 ×5 |
| 120E | 199 | drag_speed_div | speed-proportional resistance = speed / 120E | 10 12 12 14 12 |
| 1210 | 19B | slope_speed_mul | speed factor in the divisor of the height-change term | 256 220 256 200 270 |
| 1212–1220 | 19D | gear ratios ×100 | index = gear (R, N=0, 1st..6th); rpm = ratio·speed/122A (**Verified**: NSX 3.07/1.73/1.23/0.97/0.77, Diablo 2.31/1.52/1.13/0.89/0.68, Mythos = Testarossa 3.14/2.01/1.53/1.17/0.88; 5-speed cars repeat 5th as 6th; CERV 2.48/1.48/1.00 and ×0.56 high range 1.39/0.83/0.56) | |
| 1222 | 1AF | steer_num | steering rate ∝ (B70E+17)·1222·7·input·80 / (1224·696), × (3·steering_response+16)/36 | 2 1 1 5 9 |
| 1224 | 1B1 | steer_den | | 3 2 2 8 16 |
| 1226 | 1B3 | brake_force | resistance += 1226·brake (×1, ×2, ×4 by the brake flags `DS:9486 & 18h`) | 236 205 206 176 210 |
| 1228 | 1B5 | accel_mul | velocity += net_force·1228/4 | 8 ×5 |
| 122A | 1B7 | rpm_div | rpm = ratio·speed / 122A (final drive / tyre) | 10 11 11 11 11 |
| 122C | 1B9 | rev_limit | rpm above it (counter `DS:1246`) → `0e12:0e74` (skill > 2) damages the engine (`DS:9486` bit 20h) or the current gear (bit 100h<<gear); either divides the drive force by 4 | 840 984 960 984 984 |
| 122E | 1BB | torque_base | drive force = (1230·throttle + 122E)·(rpm+512)/1024 − rpm²/30 + turbo term `DS:12A4` | 32 ×5 |
| 1230 | 1BD | torque_throttle | per throttle step (`DS:B6D6`, 0–29) | 173 146 160 140 153 |
| 1232 | 1BF | throttle_upshift | throttle set on up-shift (×2 into 1st) | 3 4 4 4 4 |
| 1234 | 1C1 | throttle_downshift | throttle set on down-shift (×2 into R) | 4 5 5 5 5 |
| 1236 | 1C3 | free_rev_div | rpm += (drive − drag)/1236 in neutral, /(2·1236) when not on the road | 10 ×5 |
| 1238 | 1C5 | grip_mul | see 1200 | 44 ×5 |
| 123A, 123C | 1C7 | steer_speed | steering += s·B6D9·123A/123C | 1, 3 |
| 123E | 1CB | grade_div | slope forces: resistance += 9462·12/123E, lateral 9460/123E | 32 32 27 42 32 |
| 1240, 1242 | 1CD | turn | `DS:948D` = 1240·BCDD/1242 | 3, 4 |
| 1244 | 1CF | top_speed | speed governor: drive force 0 at speed ≥ 1244 | 136 98 128 112 96 |

Not a field of the file but useful: throttle `DS:B6D6`, gear `DS:B6D7`, wheel `DS:E33A`, speed
`DS:1296` (= max(|vx|,|vy|)-ish /180h), rpm `DS:129C`, odometer `DS:E55E` + `DS:CEA8`/60. The
simulation spec owns the full model; this table only gives the per-car constants.

## Scene `.LST` (1638 bytes) — loader `01f4:2bc6` scene_lst_load (Verified layout)

Name from the PLAYDISK scene slot (`DS:0A34 + 8*slot`, must start with `S`). Callers: `01f4:28ae`,
`01f4:2f0e` (all scenes), `01f4:36de/373e`. After the `.LST` the same function loads the `.HI`.

| File | Size | DS | Content |
|---|---|---|---|
| 000 | 13h | `0AEA` | name "Pacific - Yosemite" (default in the exe "Tom's Test Track") |
| 013 | 1 | `0B0B` | number of legs (5) |
| 014 | 29 × u16 | `0B16` | picture pair counts |
| 04E | 10 | `0B0C` | leg data digits + object flag |
| 058 | 9 × 48 | `E5C0` | leg and route names |
| 208 | 2C8h | `8E04` | copy-protection code sheet (= `MASTERQ.BIN` layout) |
| 4D0 | 29 × 14 | `081E` | archive directory `e` part (FORMATS.md) |

### 014 picture pair counts → `DS:0B16` (Verified: all match the decompressed sizes)

| DS | Meaning | Reader |
|---|---|---|
| 0B16 | `.ICN` banner (320×33) | `01f4:379c` |
| 0B18 | `.SIC` icon (72×40) | `01f4:144c` |
| 0B1A + 6n | leg n (0–8): `.ALZ` pairs (320×50) | `01f4:3f8e`, `379c` |
| 0B1C + 6n | `.BLZ` pairs (320×19) | same |
| 0B1E + 6n | u16, low byte = ASCII digit of the picture set: `<scene><digit>.COL/.ALZ/.BLZ` | same |

Legs share picture sets: SCENE01 uses sets 1,3,3,3,5 and SCENE02 2,2,4,4,5; the unused sets are the
7-byte placeholders in the archive. The scene select screen (`01f4:379c`) shows the first leg's and
the last leg's (`DS:0B0B`) pictures.

### 04E leg data digits → `DS:0B0C` (Verified)

* `0B0C[leg]` (legs 0–8): ASCII digit patched into `SCENE01?.DAT` (`0792:0d2e`) — all `1`, so every
  leg loads the same `<scene>1.DAT`; the per-leg file is `<scene><'A'+leg>.DAT` (`0792:0fce`).
* `0B15` (byte 9): nonzero → do **not** load `<scene>O.BIN` / `P.BIN`, keep the shared `SCENETTO/P`
  sets from `DATAB` (`0792:0d2e`). SCENE01 = 1 (its O/P are 7-byte placeholders), SCENE02 = 0 (own
  O.BIN 32205 bytes, P.BIN 7791 bytes). FORMATS.md's "placeholders" note holds for SCENE01 only. (The
  objects spec names it `scene_has_own_op`; the sense is inverted: nonzero = shared.)

### 058 leg and route names → `DS:E5C0` (Verified)

9 records × 48 bytes, space-padded, no terminators: leg name [12] + 3 route names [12] (each route
name starts with a space). Unused legs are "Section 6" … "Subroute 3". `01f4:4954` (leg results)
copies the current leg name and route `DS:0A74` name, and the next leg's name and 3 routes, into
its print strings; `01f4:40ce` uses them in the route choice. Example: `Scenic Coast| Highway 101|
Highway 1   | Shortcut   `.

### 208 copy-protection code sheet → `DS:8E04` (Verified decode, dead code)

Same 712-byte layout as `MASTERQ.BIN` (DATAB, 713 bytes = this block + `FF`; it equals
SCENE01's block byte for byte). Used only by the patched-out check (dead code `01f4:1ed7`–`262a`):

| Off | Size | Content |
|---|---|---|
| 000 | 12 | `column_of[12]`: values 0–3 |
| 00C | 12 × 32 | answer grid: 12 rows × 8 answers × 4 bytes; each byte = `swap_nibbles(0xFF − ch)`, `ch` = `0`–`9` or `.`; the entry `HOLE` (7B 0B 3B AB) is a marker |
| 18C | 12 | picture table A (identity in SCENE01; `0 1 2 3 6 7 12…` in SCENE02) |
| 198 | 12 | picture table B |
| 1A4 | 12 × u16 | offsets (from 8E04) of word list B: car names (SCENE01 "Mythos", "CERV III", "Diablo" …; SCENE02 "Lotus", "NSX" …) |
| 1BC | 12 × u16 | word list A: "RPM", "Turbo", "Wheelbase" … (SCENE02 "Brakes", "Wipers" …) |
| 1D4 | | the words, each ended by `AA`, then zeros |

The check: `DS:0089 = 0` → pick the set at random (`0000:0f58 & 1`): 0 = load `MASTERQ.BIN` into
8E04 and title "Pacific - Yosemite" (the master manual), 1 = keep the scene's own sheet (its
scenery-disk manual) with the scene name as title; a retry after a wrong answer reuses the set
(`DS:0089` = set + 1). A random word r (`0c1c:0702`) gives nibbles a=r>>12, b, c, d (retried until
all ≤ 11). Shown: picture A[d], picture B[c] (`01f4:262a`), word B[b], word A[a]. With
δ = (a − b) mod 12: column p = `column_of[(b+δ) mod 12]`, row = (c+δ) mod 12, answer at
`0C + 32·row + 4p`; if that entry is `HOLE`, row = (d+δ) mod 12 and the answer is at
`1C + 32·row + 4p`. The player types up to 4 characters (digits or `.`); wrong → "Wrong. You may
play for 2 minutes." (`DS:0088` stays 0). The two sheets differ by +5 on every answer. The port
bypasses the check (PLAN.md decision 3); `tools/td3car.py` dumps the decoded grids.

## `SCENExx.HI` (450 bytes) — read `01f4:2bc6`, written `01f4:2e66` (Verified)

Loaded right after the scene `.LST` into a temp buffer (`DS:2500`), accepted only if exactly 1C2h
bytes and the checksum matches, then copied to `DS:1FBB`; otherwise the table is cleared (scores 0,
names blank, records 0; the `col,row,80h` bytes keep their exe defaults). Written with `wb+` by
`01f4:2e66` after a new top score (`01f4:0a96`) or an improved leg record (`01f4:4954`); a short
write is fatal: `0000:0874(3)` "High score file update failed. Is your disk full?".

| File | DS | Size | Content |
|---|---|---|---|
| 000 | 1FBB | 7 | car slot (PLAYDISK index) of each top-score entry |
| 007 | 1FC2 | 7 × u32 | top scores, best first (`01f4:0a96` inserts, `0000:0000` compares with entry 7 at `DS:1FDA`) |
| 023 | 1FDE | 7 × 18 | print string for `01f4:1a82`: per entry `1Ah` (column), row (4, 6..11), name[15], `80h` (next line); the last `80h` is `AAh` (end) |
| 0A1 | 205C | 9 × 3 × 8 | best record per leg and route: index (leg·3 + route)·8 |
| 179 | 2134 | 9 × 8 | best cumulative record through leg n (the last used leg = whole race) |
| 1C1 | 217C | 1 | checksum = XOR of bytes 000–1C0 ^ 5Bh |

Record (8 bytes, also `DS:09C6` current leg and `DS:09D0` cumulative): `min, sec, hundredths,
average mph, u32 score`. `01f4:51e2` merges field-wise: lower time (min·6000+sec·100+hh, 0 = empty),
higher average speed, higher score, each independently; `01f4:4d7e` prints `m:ss.hh`, speed (×8/5
for metric cars), score. The average speed is distance in 1/60 miles × 6000 / time in hundredths;
the leg score is computed in `main` from skill (`DS:0B04`+4), average speed and distance (capped at
1 000 000) and added to `DS:09DA`. SCENE02.HI in this copy holds real results (95211 best), which
check out (route bests, cumulative totals, checksum).

## `TD3.CFG` (6 bytes) — `0000:092a` config_load (Verified)

Three u16. Read at startup; if missing the game asks on the text screen and, on `Y`, writes it
itself (`wb+`). `SETUP.EXE` (EXEPACK'd MSC; unpacked with `tools/unexepack.py`, DGROUP 00CDh, main
`0000:0010`, writes with DOS create/write at `0000:02aa`) writes the same three words from its menus
(letters A–E) — no joystick setting exists in either.

| Off | DS | Values |
|---|---|---|
| 0 | `E5B4` → `E338` = `DS:00EA[v]` | video: 0 VGA/MCGA (library mode 13h), 1 EGA (0Dh), 2 Tandy (09h); the prompt default comes from hardware detection `DS:E776+EEh` |
| 2 | `0096` | audio: 0 PC single-voice, 1 Tandy 3-voice, 2 CMS (Game Blaster), 3 MIDI MT-32/LAPC-1, 4 AdLib/Sound Blaster. 3 → `DS:008F` = 1 and `DS:0096` = 81h |
| 4 | `0098` | 1 when MIDI was chosen (the setup shows "Turning MIDI sounds on may slow down the game"); no other reader |

This copy: `00 00 04 00 00 00` = VGA, AdLib/SB. (The index's `TD3.CFG` strings for `0c1c:15c7` and
`0c1c:170a` are false hits: `0202h` there is a VGA register value.)

## `PLAYDISK.DAT` (179 bytes) — read `01f4:28ae`, written `01f4:2736` (Verified)

Read at startup (called from `0000:0000`); rewritten when the car (`01f4:310c`), scene
(`01f4:3636`) or options (`01f4:3b2e`) change. `01f4:2812` first re-reads the label on the disk in
the drive and loops "Insert your PLAY DISK" until it matches `DS:0A78` (so the settings are saved to
the right disk).

| File | DS | Size | Content |
|---|---|---|---|
| 000 | 0A78 | 12h | label "MASTER PLAY DISK " (exe default "No Play Disk"); shown on the PLAY DISK screen |
| 012 | 09E0 | 14 × 6 | car base names ("CCERV", "CDIAB", "CMYTH", "CCNSX", "CSTEL"), `?????` = empty; loadable only if it starts with `C` |
| 066 | 0A34 | 8 × 8 | scene base names ("SCENE01", "SCENE02"), `???????` = empty; must start with `S` |
| 0A6 | 0AFE | u16 | selected car slot |
| 0A8 | 0B00 | u16 | no reader found (0) |
| 0AA | 0B02 | u16 | selected scene slot |
| 0AC | 0B04 | u16 | skill level 0–8 (shown 1–9; < 3 = "(auto-shift)", `DS:0095`; > 2 enables gear damage) |
| 0AE | 0B06 | u8 | number of car slots to cycle through (5) |
| 0AF | 0B07 | u8 | number of scene slots (2) |
| 0B0 | 0B08 | u8 | number of human players 1–4, taking turns per leg (`DS:09C5`); shown as "Clock" (1), "1 Other Person" (2), "n Other People" (3–4) |
| 0B1 | 0B09 | u8 | 1 = "Computer Cars" (with 0B08 = 1); enables opponents (`0792:0d2e` picks the two opponent cars from the other slots) |
| 0B2 | 09C2 | u8 | steering response 1–9 (default 4), changed in-game (`0000:179c`) |

This copy: car slot 3 (NSX), scene 0, skill 3 (auto), 5 cars, 2 scenes, clock, computer cars on,
steering 4.

## Port notes

* Keep the DS-block structs; load files with the same block sizes so short files fail the same way
  (`car_lst_load` returns 1 → the slot is skipped).
* `01f4:2f0e` loads every car/scene description to list the names, overwriting the live car and
  scene globals, then reloads the selected ones: the port can read names only.
* Units for HUD/port code: rpm ≈ `129C`×10, mph ≈ `1296`×1.64, gauge angle unit 1/65536 turn.

## Open questions

* `DS:0B54` (car pair count 0450h in every car) and `CC74`, `CEB0/CEB1`, `DS:0B00`: no reader found;
  maybe read through a pointer the index missed.
* `94B8` block beyond `94BF` (cockpit/hood polygons, `94C1/94C3` limits) — for the render3d spec.
* Exact physical units of `1210`, `123E`, `1222/1224`, the turbo term `DS:12A4` (from rpm changes
  `129E−129C`) and whether the speed unit is exactly 1.64 mph (dial faces only give ±3 %).
* `DS:B70E` (in the steering rate and shown by `0e12:0cbe`) is set in `0792:000c` from the timer:
  confirm it is a countdown and why it scales steering.
* Picture tables A/B of the code sheet: what `01f4:262a` draws for an index (icons from `KEYS.LZ` /
  `COP*.LZ`?).
