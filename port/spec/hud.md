# hud — Test Drive III: The Passion, TDIII.EXE (VGA path)

Porting spec for everything drawn over and around the 3D view during a race: the cockpit set-up
(palettes, `.TOP` / `1.BOT` / `2.BOT` / `L.BOT` / `R.BOT` / `.ETC` pictures, the spare-page layout),
the per-frame instruments (tachometer and speedometer needles, rolling odometer, steering-wheel rim
and marker, animated gear lever), the top bar (compass, race clock, radar detector), the crash
overlays (broken windscreen `BROKE.LZ`, water `WATER.LZ`), the chase-car / instant-replay panel
(`CHASE.LZ`), the window-size switch, the in-race message box and pause, and `race_input`.
Conventions follow `port/RE_GUIDE.md` (`SSSS:OOOO` code, `DS:xxxx` = DGROUP 1BE4). Car-file fields
are cited from `port/formats/descriptions.md` (DS:CC64 gauges, DS:E564 gearbox, DS:E77E marker,
DS:CEAE colours, DS:0B50 picture pair counts, DS:94B8 units) and not re-derived.

Everything below was read from the disassembly (`tools/x86dis.py`); the Ghidra output of `0792`
is reliable apart from three things it hides: `gfx_fill_rect` calls that re-use the previous call's
`y0,y1` still on the stack (called out where it happens), the extra first argument Ghidra shows on
far library calls (a segment value, not an argument), and register-argument helpers in `0e12`.
Confidence is **verified** unless stated. Library routines are described only by their mode-13h
behaviour; their implementation belongs to the platform spec.

Scratch scripts used (not deliverables): decompile extractor, DGROUP dumper, picture renderer at a
given width, per-car table dumper (all in the session scratchpad).

-----------------------------------------------------------------------------------------------

## 1. Overview

### 1.1 Video mode and pages

* VGA runs in **library mode 13h** (`DS:E338 = 13h`, from `TD3.CFG` word 0 through the table
  `DS:00EA = {13h, 0Dh, 09h}`; every VGA-only branch in the game tests `E338 == 13h`). RE_GUIDE's
  "14h = game VGA mode" should be read as "13h" for the in-game code (platform spec to confirm).
* Two drawing **pages**, selected with `gfx_set_page(p)` (`1714:000e`); the game mirrors the choice
  in `DS:009A` because the picture RLE drawer (`0c1c:0c45`) takes its target from
  `DS:90CC[DS:009A]` instead of the library state:
  * **page 0** (`DS:90CC`) = the displayed screen. All HUD output goes straight to it; there is no
    double buffering and no retrace wait (as in TD1/TD2).
  * **page 1** (`DS:90CE`) = a 64000-byte off-screen page used as a picture store and backing store
    (layout in §1.3).
* The 3D view is rendered into a separate buffer (`DS:90D0`, render3d) and copied into page 0 rows
  16–111 by `view_present` (`0c1c:13d8`, render3d). That copy skips x 168–255 of its first 14 rows
  (the rear-view mirror, render3d).
* `DS:90F0` (0 or 80h) is added to every colour by the RLE drawer: car pictures are drawn with
  80h into the car palette (colours 144–255); `OTWCOL.BIN` fills 16–127.

### 1.2 Screen layout (page 0)

| Area | Rows | Content | Drawn by |
|---|---|---|---|
| Top bar | 0–15 | `<car>.TOP` (320×16) | `cockpit_pictures_draw(199)` once |
| – compass window | x 8–31, y 7–14 | 24×8 window into `COMPASS.LZ` | `compass_draw` (`0e12:0cbe`) |
| – debug readout | x 10–19, y 9–13 | two digits of `DS:B70E` (timer ticks of the last frame), replaces the compass when `DS:B6EA` (debug keys) is set | same |
| – race clock | x 41–62, y 8–13 | `m:ss` 4×5 digits | `clock_draw` (`0e12:0b85`) |
| – radar detector | x 284–311, y 9–11 | power light + up to 6 bars, blinking | `radar_draw` (`0e12:0db4`) |
| 3D view | 16–111 | full width, or x 40–279 × rows 16–79 in half-window mode (`DS:B6DC`) | render3d |
| Message box | 113–133 | grey box with text (modal) | `message_box` (`0000:179c`, game_flow) |
| Cockpit | 112–199 | `1.BOT` (rows 112–155) + `2.BOT` (156–199); the wheel region x 32–199 × 152–199 is swapped between three rim pictures | `cockpit_setup`, `wheel_rim_blit` |
| – tachometer | car rect `CC7C`,`CC7E`,`CC84` | backing + needle | `tach_draw` |
| – speedometer + odometer | car rect `CC80`,`CC82`,`CC86` | backing (incl. odometer) + needle | `odometer_draw`, `speedo_draw` |
| – steering marker | around `E784`,`E786` | 8×5 glyph on the wheel rim | `steer_marker_draw` |
| – gear lever | x 248–303 × 124–171 | gate + knob, shown while shifting (or always) | `shift_lever_draw`, `shifter_update` |
| Chase / replay panel | 112–199 | frame + `CHASE.LZ` buttons + labels, replaces the cockpit | `replay_panel_draw` |

Per-car screen rectangles (from the `.LST` gauge block, descriptions.md; odometer converted from
backing-store coordinates, §4.6):

| Car | tach centre / len | tach backing (x0–x1, y0–y1) | speedo centre / len | speedo backing | odometer digits (x0–x1, y0–y1) | marker centre / radius |
|---|---|---|---|---|---|---|
| CERV | 154,159 / 14 | 128–175, 136–168 | 92,159 / 14 | 64–111, 136–168 | 86–99, 161–165 | 116,205 / 92 |
| NSX | 92,154 / 15 | 72–111, 136–168 | 138,154 / 15 | 120–159, 136–168 | 132–145, 157–161 | 113,207 / 92 |
| Diablo | 168,139 / 17 | 144–191, 121–153 | 67,139 / 17 | 48–87, 121–153 | 62–75, 144–148 | 112,230 / 88 |
| Mythos | 88,151 / 16 | 64–111, 133–165 | 152,151 / 16 | 128–175, 133–165 | 144–157, 155–159 | 118,207 / 92 |
| Stealth | 87,156 / 17 | 64–111, 134–166 | 152,156 / 17 | 128–175, 134–166 | 146–159, 158–162 | 116,207 / 92 |

### 1.3 Page 1 layout during a race

| Page-1 area (x, y inclusive) | Content | Written by | Read by |
|---|---|---|---|
| 0–151, 0–7 | `COMPASS.LZ` strip 152×8 (N NE E SE S SW W NW N, 128 px = 360°) | `cockpit_pictures_draw`, `chase_view_exit` | `compass_draw` |
| rest of 0–319, 0–15 | copy of the `.TOP` (after set-up) / the saved screen top bar while the chase panel is up | `cockpit_pictures_draw(199)`, `chase_view_enter` | `chase_view_exit` |
| 0–319, 16–103 | the centre-wheel cockpit (= screen rows 112–199 shifted up 96) | `cockpit_setup` (copy from page 0), `cockpit_pictures_draw(0x67)` | `wheel_rim_blit` (centre), `chase_view_exit` |
| 0–167, 104–151 | `L.BOT` (wheel turned left, 168×48) | `cockpit_setup` | `wheel_rim_blit` |
| 0–167, 152–199 | `R.BOT` (wheel turned right) | `cockpit_setup` | `wheel_rim_blit` |
| 168–207, 104–136 | tach backing, first 40 columns | `cockpit_setup`, `wheel_rim_blit` | `tach_draw` |
| 168–(127+CC84), 137–169 | tach backing, remaining `CC84−40` columns | same | same |
| 208–263, 104–151 | screen saved under the gear lever (screen 248–303 × 124–171) | `shift_lever_draw` | `shifter_update`, `race_run` |
| 208–263, 152–199 | gear-lever compose area (gate copy + knob) | `shift_lever_draw` | `shift_lever_draw` |
| 264–319, 104–175 | `<car>.ETC` 56×72: gate = rows 104–151; marker glyph at 280–287 × 152–156 (colour 8Fh); knob sprite at 288–319 × 152–175 | `cockpit_setup` | `shift_lever_draw` (gate), `cockpit_setup` (mask capture) |
| 264–279, 152–156 | screen saved under the steering marker (16×5) | `wheel_update` | `wheel_update` |
| 264–(263+CC86), 167–199 | speedo backing incl. the odometer digits at (`CC78`,`CC7A`) | `cockpit_setup`, `wheel_rim_blit`, `odometer_draw` | `odometer_draw` |
| 0–319, 0–95 (temporary) | `WATER.LZ` 160×96 twice, rotated by the roll effect | `water_overlay_start`, `water_roll_step` | `water_roll_step` |

The speedo backing overwrites the bottom of the `.ETC` (rows 167–175) and the marker save area
sits inside it: both happen after the knob and marker masks were captured, so nothing is lost.

### 1.4 Frame integration (race_run is game_flow's; HUD calls only)

```
race_run (0792:000c)
  set-up: stage_load_objects, car_reset 083c, hud_reset_topbar 10a6, cockpit_setup 0922,
          shifter_update 10c8, wheel_update 1310, tach_draw 15d4, odometer_draw 16c4
          (+ message 9 "Player n, ready to drive...." when DS:0B08 != 1)
  loop:
    frame_ticks DS:B70E = (u8)(timer_ticks - previous start)
    if view mode DS:948B flipped or race_state changed:            (§4.13)
        hide the gear lever if shown; [car_reset]; hud_reset_topbar
        crash overlay (broken glass / water) + 044e, or lever reset
        chase_view_exit 04ca / chase_view_enter 059a
    if DS:9488: message_box(DS:9488), DS:9488 = 0         (WRONG WAY!, Go back to the main road!, ...)
    frame_update 0e12:76ec   -> topbar_update 0e12:0b1d -> clock_draw, compass_draw, radar_draw (page 0)
    race_input(0)            -> controls, wheel_update
    frame_draw 0e12:77d5     (3D into the render buffer)
    race_input(1)            -> controls (joystick only if the last frame took > 40 ticks), wheel_update
    if !crash DS:B78D && !chase DS:948B:
        shifter_update; tach_draw; odometer_draw; odometer_update
    if race_state != 2,3: view_present 0c1c:13d8   (3D buffer -> page 0 rows 16..111)
    race_input(2)            -> key_read (global keys, pause), key_code_dispatch, wheel_update
    ... wait until >= 5 timer ticks since the loop top
```

### 1.5 Call graph

```
race_input 0414 ─┬─ key_read 0000:0f80 (phase 2) ── message_box 0000:179c (pause, toggles)
                 ├─ key_code_dispatch 0e12:0084 (phase 2) ── key handlers (simulation), e.g.
                 │     F1 key_window_size 0e12:031c, F9 key_replay_pause 0e12:059c
                 ├─ joy_read 0000:1e44 / controls_update 0e12:0080 (phases 0,1)
                 └─ wheel_update 1310 ─┬─ wheel_rim_blit 1488
                                       ├─ polar 0e12:234b, gfx_copy_rect
                                       ├─ steer_marker_draw 155e
                                       └─ tach_draw 15d4, odometer_draw 16c4 (on rim change)
cockpit_setup 0922 ─┬─ palette_load OTWCOL.BIN, <car>COL.BIN
                    ├─ cockpit_pictures_draw 1c66(199)  (2.BOT, 1.BOT, .TOP, page copy, COMPASS)
                    ├─ .ETC, gfx_read_mask ×7, backing copies, L.BOT, R.BOT
                    ├─ view_first_frame 0cec (frame_update, frame_draw, view_present)
                    └─ palette_apply 01f4:1c48
shifter_update 10c8 ── shift_lever_draw 1156
tach_draw 15d4 ── polar, gfx_lineto, steer_marker_draw
odometer_draw 16c4 ─┬─ gfx_draw_mask ×3, gfx_plot
                    └─ speedo_draw 18da, steer_marker_draw
odometer_update 1952
topbar_update 0e12:0b1d ── clock_draw 0b85, compass_draw 0cbe, radar_draw 0db4
crash (race_run): broken_glass_overlay 1c00 | water_overlay_start 19ca + water_roll_step 1a72 ×40
                  + cockpit_pictures_draw(0x67)
chase view: chase_view_enter 059a ── replay_panel_draw 0602 ; chase_view_exit 04ca
            replay_panel_clear 0e12:04f1 (from 0e12:044e and 0ab4:1220)
```

-----------------------------------------------------------------------------------------------

## 2. Function table

### 2.1 Functions owned here

| address | proposed name | signature | purpose | confidence |
|---|---|---|---|---|
| 0792:0414 | race_input | `far void(int phase)` | Input between the frame phases; self-centring of the wheel; `wheel_update` | verified |
| 0792:04ca | chase_view_exit | `far void(void)` | Leave chase-car / replay view: music, restore top bar and cockpit from page 1, redraw compass strip, invalidate all instrument caches | verified |
| 0792:059a | chase_view_enter | `far void(void)` | Enter chase view: stop music, save top bar to page 1, blank it, `replay_panel_draw`, `DS:CC92 = 1` | verified |
| 0792:0602 | replay_panel_draw | `far void(void)` | Grey panel over the cockpit, `CHASE.LZ` buttons, title and labels | verified |
| 0792:0922 | cockpit_setup | `far void(void)` | Palettes, all cockpit pictures, page-1 layout, mask capture, backing stores, first 3D frame, palette on | verified |
| 0792:0cec | view_first_frame | `far void(void)` | One `frame_update` + `frame_draw` + `view_present` | verified |
| 0792:10a6 | hud_reset_topbar | `far void(void)` | `DS:B709 = FFh` (clock redraw), `DS:BD34 = FFFFh` unless `race_state == 2`, page 0 | verified |
| 0792:10c8 | shifter_update | `far void(void)` | Per frame: step the lever animation, show/hide the lever | verified |
| 0792:1156 | shift_lever_draw | `far void(void)` | Show the lever (save screen), compose gate + knob at the current step, copy to screen | verified |
| 0792:1310 | wheel_update | `far void(void)` | Wheel rim picture by `DS:E33A`, move the steering marker | verified |
| 0792:1488 | wheel_rim_blit | `far void(int dy, int dx)` | Copy one of the three rim pictures to the screen and refresh the lower gauge backings | verified |
| 0792:155e | steer_marker_draw | `far void(void)` | Draw the 8×5 marker glyph on the rim at the wheel angle | verified |
| 0792:15d4 | tach_draw | `far void(void)` | Restore tach backing and draw the needle when the step changed | verified |
| 0792:16c4 | odometer_draw | `far void(void)` | Render the rolling odometer into the speedo backing; restore speedo backing + needle when the speed step changed | verified |
| 0792:18da | speedo_draw | `far void(void)` | Speedometer needle | verified |
| 0792:1952 | odometer_update | `far void(void)` | Accumulate \|velocity\|, advance the odometer | verified |
| 0792:19ca | water_overlay_start | `far void(void)` | Unpack `WATER.LZ` twice into page 1, splash sound, first roll step | verified |
| 0792:1a72 | water_roll_step | `far void(void)` | Show page-1 water in the view area and rotate it down 2 rows | verified |
| 0792:1c00 | broken_glass_overlay | `far void(void)` | Glass sound, `BROKE.LZ` over the 3D buffer (0Fh transparent), present | verified |
| 0792:1c66 | cockpit_pictures_draw | `far void(int bottom_y)` | `2.BOT`/`1.BOT` (+ `.TOP` and page copy when 199), compass strip | verified |
| 0792:1dfe | load_opponent_pob_nonvga | `far void(void)` | Non-VGA only: re-load opponent `.POB` (the unpack buffer overlaps it there); **no-op on VGA** | verified |
| 0e12:0b1d | topbar_update | `near void(void)` | Per frame (from `frame_update`, not in the menu demo): race-clock tick, then the three top-bar draws | verified |
| 0e12:0b85 | clock_draw | `near void(void)` | Race clock `m:ss` when the second changed | verified |
| 0e12:0cbe | compass_draw | `near void(void)` | Compass window (or debug frame-time digits) | verified |
| 0e12:0db4 | radar_draw | `near void(void)` | Radar-detector bars, blink and beep | verified |
| 0e12:04f1 | replay_panel_clear | `near void(DL=keep_f5)` | Grey out the panel's button area before a message | verified |

Key handlers with HUD drawing (the key table and the other actions belong to the simulation spec):

| address | key code | name | HUD effect |
|---|---|---|---|
| 0e12:031c | 81h (F1) | key_window_size | toggle `DS:B6DC`, message 0Ah/0Bh, view geometry `0e12:409c` (render3d); when switching to half: black the border of the view |
| 0e12:059c | 89h (F9) | key_replay_pause | during replay (`DS:94B2`): toggle `DS:B6D4`, print "Paused" / blank |
| 0e12:0269 | 44h/64h | key_lever_display | toggle `DS:B6DB` (gear lever always shown) |
| 0e12:0133 | 43h/63h | key_wheel_centering | toggle `DS:B6DA`, message 0Fh/0Eh ("Wheel centering on/off") |

### 2.2 Library and other-subsystem routines used (names proposed; behaviour in mode 13h)

All coordinates are pixels; `y_bottom` = the **bottom** row (these routines fill upward).

| address | name | arguments | behaviour | owner |
|---|---|---|---|---|
| 1714:000e | gfx_set_page | `(page)` | `DS:BD40 = page`, `DS:BD88 = DS:BD8C[page]` (segment of the page). The game always sets `DS:009A = page` too | platform |
| 1703:0001 | gfx_set_colour | `(c)` | `DS:BD41 = DS:BD42 = c` | platform |
| 1785:000b | gfx_fill_rect | `(x0, x1, y0, y1)` | fill `[x0..x1]×[y0..y1]` of the current page, **no clipping** | platform |
| 1818:0003 | gfx_copy_rect | `(x0, x1, y0, y1, dx, dy_bottom, src_page, dst_page)` | copy `[x0..x1]×[y0..y1]` of `src_page` to `dst_page` with its bottom-left at (`dx`, `dy_bottom`); rows copied **bottom-up**, no clipping; same-page overlapping copies rely on the bottom-up order | platform |
| 16fb:0008 | gfx_moveto | `(x, y)` | `DS:BD4B = x`, `DS:BD4D = y` | platform |
| 16d9:0002 | gfx_lineto | `(x, y)` | line from the current point, current colour, then current point = (x, y); algorithm in §4.1 | platform |
| 173c:0005 | gfx_plot | `(x, y)` | one pixel if inside the clip window `DS:BD53..BD51 × DS:BD57..BD55` (full screen, set by `gfx_set_mode`) | platform |
| 192e:0005 | gfx_fill_clipped | `(x0, x1, y0, y1)` | clip to the window, then `gfx_fill_rect` | platform |
| 185f:000b | gfx_draw_mask | `(u8 *bits, bytes_per_row, rows)` | 1-bit mask at the current point: row 0 of the data is the **bottom** row at `y = BD4D`, each next row one pixel up; bits MSB = leftmost; set bits → current colour, clear bits untouched; no clipping | platform |
| 18b3:0004 | gfx_read_mask | `(u8 *bits, bytes_per_row, rows)` | inverse: builds the mask of the pixels equal to the current colour, same geometry | platform |
| 16ff:000b | bios_wait_ticks | `(n)` | busy-wait n BIOS ticks (INT 1Ah, 18.2 Hz) | platform |
| 16ef:0009 | mouse_set_pos | `(x, y)` | INT 33h fn 4 (scaled by `DS:BD4F/BD50`) | platform |
| 170e:0002 | gfx_show_page | `(page)` | display page select (`DS:BD43`); called by `message_box` | platform |
| 01f4:59ac | pic_draw | `(pairs, count, width, x, y_bottom, ega_arg)` | VGA: `0c1c:0c45` RLE of (colour,count) pairs, bottom-up, wrap at `width`, colour + `DS:90F0`, target `DS:90CC[DS:009A]`, no clipping (FORMATS.md) | platform / game_flow |
| 0c1c:1759 | pic_draw_view | `(pairs, count, y_bottom)` | RLE, 320 wide, into the 3D buffer `DS:90D0`; colour 0Fh = transparent; no `90F0` offset | platform / render3d |
| 01f4:1864 | gfx_frame | `(x0, x1, y0, y1)` | rectangle outline: moveto(x0,y0), lineto (x1,y0),(x1,y1),(x0,y1),(x0,y0) | game_flow |
| 01f4:1a82 | print_items | `(char *s, …) → next` | text items `<col><row>text`, `80h` = next item, `AAh` = end; returns the pointer after the `AAh` | game_flow |
| 0c1c:0733 | text_colours | `(fg, bg)` | `DS:90E3 = fg<<8`, `DS:90E1 = bg<<8` (Likely: fg/bg order from the call sites) | platform |
| 0ab4:0047 | lzw_unpack | `(far src, far dst)` | picture LZW (FORMATS.md) | platform |
| 0000:0ee0 | res_load_far | `(char *name, far dst)` | archive / file load | game_flow |
| 0000:0d62 | palette_load | `(char *name)` | 336 bytes to palette buffer colour `16 + DS:90F0`; in a race (`DS:0086 = 5`) not applied | game_flow |
| 01f4:1c48 | palette_apply | – | VGA: `0c1c:0c2a` buffer → DAC | platform |
| 01f4:1c00, 1c34 | ega_palette_* | – | EGA only; no-op on VGA | platform |
| 0e12:234b | polar | `(u16 angle, u16 len)` | → `DS:9460` x, `DS:9462` y; §4.1 | render3d (shared) |
| 0c1c:13d8 | view_present | – | 3D buffer → page 0 view area | render3d |
| 0e12:7c21 | – | – | render3d pass called after the glass picture | render3d |
| 0000:179c | message_box | `(id)` | modal message; §4.14 | game_flow |
| 0000:0f80 | key_read | `(u16 *k)` | key fetch + global keys (pause, sound, joystick) | game_flow / platform |
| 0000:1e44 | joy_read | `(u16 *k) → k` | joystick as a key code | platform |
| 0e12:0080 | controls_update | – | `call 0751; retf`: held keys/joystick → throttle, steering, gear buttons (**not** the key dispatcher, see §9) | simulation |
| 0e12:0084 | key_code_dispatch | – | page 0; dispatch `DS:E08C` through `CS:0000` (40h–7Fh) / `DS:B6EF` (81h–8Ah) | simulation |
| 0c1c:111d | sfx_play_ax / 0c1c:1110 sfx_play | `(id)` | sound effects 2 (glass), 6 (splash), 0Eh (radar blip) | sound |

-----------------------------------------------------------------------------------------------

## 3. Globals

### 3.1 Inputs from other subsystems

| DS | name | type | meaning | written by |
|---|---|---|---|---|
| E33A | steer_wheel | u8 0–32 | wheel position, 16 = centre | simulation (`0e12:09d6`, `08d9`), race_input |
| E5AE | tach_step | u8 | `rpm >> 5` clamped to `CC76` | `0977:0008` |
| E55F | speedo_step | u8 | `speed >> 2`, 0 when reversing (`DS:1250 < 0`), max 31 | `0977:0008` |
| 124E:1250 | car_velocity | s32 | signed velocity (odometer input) | simulation |
| B6D7 | gear | u8 | 0 = R, 1 = N, 2.. | simulation |
| CC57 | lever_target | u8 | lever animation step of the gear (`E565[gear]`) | `gear_lever_target 0e12:01c5`, race_run, cockpit_setup |
| 9498 | view_heading | u16 | heading of the view (car heading `DS:9491`, or the chase camera `DS:94AB<<8`) | `frame_update` |
| 95BE | leg_compass_offset | u8 | leg-map header byte 02Ch (world.md "Unknown"): added to the heading for the compass; values C0h, E0h (SCENE01 A, B), 20h (SCENE02 C), 0 otherwise | leg map |
| B70B, B70C | clock_sec, clock_min | u8 | race clock | `topbar_update` |
| B70E | frame_ticks | u8 | timer ticks taken by the last frame | race_run |
| B710 | radar_level | u8 | detector strength (`0x6F − depth>>7` of the nearest police car, max 6Fh) | `0e12:5ffe` |
| B6EA | debug_mode | u8 | enables debug keys (weather, colours) and the frame-time readout | – (never set in the code seen) |
| 948B | chase_view | u8 | 1 = external chase-car / replay camera | F5 `0e12:026f`, `0e12:4c51` (crash replay) |
| 94B2 | replay_playing | u8 | instant replay running | `0e12:4c51`, `0ab4:1220` |
| B78D | crash_active | u8 | crash sequence running | `0e12:0f31` |
| BD3B, BD3C | crash_in_water, water_pending | u8 | crash surface was water (0Eh) | `frame_draw` |
| B6DC | half_window | u8 | 3D view 240×64 instead of 320×96 | F1 |
| B6DB | lever_always | u8 | show the gear lever permanently | key 44h/64h |
| B6DA | wheel_centering | u8 | self-centring of the wheel | key 43h/63h |
| B704 | steer_key_held | u8 | frames a steering key has been held (0 = none) | simulation |
| B6CA | mouse_steering | u8 | | F7 |
| BD39 | frame_count | u8 | incremented by `frame_update` | frame_update |
| 0092 | joystick_on | u16 | | key_read |
| E338 | gfx_mode | u16 | 13h = VGA | config |
| 9488 | pending_message | u16 | message id to show at the next loop top | simulation |

### 3.2 HUD state (owned here)

| DS | name | type | meaning | written by | read by |
|---|---|---|---|---|---|
| 009A | cur_page | u16 | mirror of the library page | everywhere | pic_draw, 179c |
| 90F0 | colour_offset | u8 | 0 or 80h added by pic_draw | cockpit_setup, 1c66, 19ca, race_run | pic_draw, palette_load |
| E562 | rim_shown | u8 | rim picture on screen: 0 left, 1 centre, 2 right; FFh = force | wheel_update, cockpit_setup (1), chase_view_exit (FFh) | wheel_update |
| E7EE | rim_wanted | u8 | | wheel_update | wheel_update |
| E5B7 | marker_pos | u8 | wheel position the marker is drawn at; FFh = none | steer_marker_draw, cockpit_setup, chase_view_exit | wheel_update |
| E554 | marker_mask | u8[5] | 8×5 marker glyph (from the `.ETC`) | cockpit_setup | steer_marker_draw |
| E090 | knob_masks | u8[6][96] | 32×24 knob masks for colours 0Fh, 7, 8, 0, 0Ch, 4 (+80h) at E090, E0F0, E150, E1B0, E210, E270 | cockpit_setup | shift_lever_draw |
| CE94 | lever_step | u8 | current animation step | shifter_update, cockpit_setup, race_run | shift_lever_draw |
| E5B6 | lever_hold | u8 | frames to keep the lever up / "just drawn" | shift_lever_draw (1), shifter_update (10h), `gear_lever_target` (0) | shifter_update |
| CC52 | lever_shown | u8 | lever area saved and lever on screen | shift_lever_draw, shifter_update, race_run, chase_view_exit | same |
| CC56 | tach_drawn | u8 | tach step drawn (FFh = force) | tach_draw | tach_draw |
| E860 | speedo_drawn | u8 | speedo step drawn (FFh = force) | odometer_draw | odometer_draw |
| CE9D | odo_drawn | u8 | odometer sixtieths drawn (force: `CEA8 ^ 80h` or FFh) | odometer_draw | odometer_draw |
| CC51, CC8C | odo_roll_tens, odo_roll_units | u8 | roll offsets (0–6) of the two whole digits | odometer_draw (init 6 in cockpit_setup) | odometer_draw |
| E55E | odo_units | u8 0–99 | whole miles/km this leg | odometer_update; 0 in `stage_load_objects` | odometer_draw |
| CEA8 | odo_sixtieths | u8 0–60 | sixtieths of a unit (see §4.7 for 60) | same | same |
| 1186:1188 | odo_accum | u32 | accumulated \|velocity\| | odometer_update; 0 in `car_reset` | |
| B709 | clock_drawn | u8 | seconds drawn (FFh = force) | clock_draw, hud_reset_topbar | clock_draw |
| B712 | compass_drawn | u8 | strip x drawn (FFh = force) | compass_draw, cockpit_setup, chase_view_exit, car_reset | compass_draw |
| B711 | radar_phase | u8 | frame counter for blink/beep | radar_draw | radar_draw |
| B70F | radar_drawn | u8 | level drawn (FFh = blank phase) | radar_draw | radar_draw |
| B707 | clock_running | u8 | clock started (first gear change) | gear_up/down, others | topbar_update |
| B708, B70A | clock_frames, clock_frac | u8 | frames in the current second (0–4), fraction `(f + f>>1)>>1` (not displayed) | topbar_update; `message_box` stores `timer_ticks` in B708 | |
| CC92 | panel_shown | u8 | chase/replay panel is on screen | chase_view_enter (1), chase_view_exit / race_run (0) | render3d, 0e12:044e |
| B6D4 | replay_paused | u8 | | key_replay_pause | |
| E330 | – | u8 | cleared by cockpit_setup / race_run, never read (dead) | | |
| E550/E552, E55A/E55C, CE98/CE9A, E5BC/E5BE | pic_water, pic_broke, pic_chase, pic_compass | far ptr | packed `WATER.LZ` (VGA) / `WATEREGA.LZ`, `BROKE.LZ` / `BROKEGA.LZ`, `CHASE.LZ`, `COMPASS.LZ`, loaded once by `main` | main | here |
| CC5C/CC5E | pic_file_buf | far ptr | 7810h-byte buffer for per-race packed pictures | main (`0000:11d2`) | here |
| 2500 | unpack_buf | bytes | unpacked (colour,count) pairs (`sprite_set` in the decompile) | lzw_unpack | pic_draw |
| 9460, 9462 | polar_x, polar_y | s16 | polar results | polar | here |

### 3.3 Static tables (DGROUP)

| DS | size | content |
|---|---|---|
| B75B | 10 × 5 | digit font 4×5 for the top bar, 1 byte per row, **bottom row first**, bits 7–4 = columns 0–3 |
| B719 | 66 | odometer digit strip 3×5: digit d at `B719 + 6d` (5 rows bottom-first + 1 blank row), then a copy of "0" at `B755` and a blank at `B75A`; reading 5 bytes from `B719 + 6d + k` (k = 1–5) shows the digit rolling up into d+1 |
| 16DA | 34h × u16 | message pointers (`message_box`) |
| 0E12:0FF2 (code segment) | 129 × u16 | `sin_tab[i] = min(65535, round(65536·sin(i·π/256)))`, i = 0–128 (checked against every entry) |

-----------------------------------------------------------------------------------------------

## 4. Pseudocode

Types: `u8/s8/u16/s16/u32`. Arithmetic is 16-bit and wraps unless stated. `copy(x0,x1,y0,y1, dx,dyb, src,dst)`
is `gfx_copy_rect`, `fill` is `gfx_fill_rect`, `page(p)` means `gfx_set_page(p); DS:009A = p`.

### 4.1 Geometry primitives

```c
/* 0e12:234b — polar(angle, len): 512 angle steps per turn, 0 = up, clockwise */
void polar(u16 angle, u16 len)
{
    u16 a = (angle >> 7) & 0x1FF;            /* rol 1; xchg; and 1FFh */
    u16 i = a & 0x7F;
    if (a & 0x80) i = 0x80 - i;              /* fold into 0..128 */
    s16 x = (u16)(((u32)sin_tab[i] * len) >> 16);
    if (a & 0x100) x = -x;
    s16 y = (u16)(((u32)sin_tab[0x80 - i] * len) >> 16);
    u8 q = a >> 7;                           /* quadrant 0..3 */
    if (q == 1 || q == 2) y = -y;
    polar_x = x;  polar_y = y;               /* DS:9460, DS:9462 */
}
/* A needle / marker goes from (cx, cy) to (cx + polar_x, cy - polar_y). */

/* 16d9:0002 — gfx_lineto(x, y) */
void gfx_lineto(s16 x, s16 y)
{
    s16 x0 = cur_x, y0 = cur_y;              /* DS:BD4B, BD4D */
    if (x == x0) { cur_y = y; gfx_fill_clipped(x, x, min(y,y0), max(y,y0)); return; }
    if (y == y0) { cur_x = x; gfx_fill_clipped(min(x,x0), max(x,x0), y, y); return; }
    s16 sx = x < x0 ? -1 : 1, sy = y < y0 ? -1 : 1;
    s16 dx = abs(x - x0), dy = abs(y - y0);
    s16 major, minor, str_x, str_y;
    if (dx >= dy) { major = dx; minor = dy; str_x = sx; str_y = 0; }
    else          { major = dy; minor = dx; str_x = 0;  str_y = sy; }
    s16 e = 2*minor - major;
    s16 inc_str = 2*minor, inc_diag = 2*minor - 2*major;
    s16 n = major + 1, px = x0, py = y0;
    for (;;) {
        gfx_plot(px, py);                    /* clipped */
        if (--n == 0) break;
        if (e < 0) { px += str_x; py += str_y; e += inc_str; }
        else       { px += sx;    py += sy;    e += inc_diag; }
    }
    cur_x = px; cur_y = py;                  /* = (x, y) */
}
```

### 4.2 race_input (0792:0414)

```c
void race_input(int phase)
{
    u16 k;
    if (phase == 2) {
        key_read(&k);                        /* 0000:0f80: global keys, pause (msg 11h), sets key_code */
        key_code_dispatch();                 /* 0e12:0084 */
    } else {
        if (joystick_on) {                   /* DS:0092 */
            if (phase != 0 && !(phase == 1 && frame_ticks > 0x28)) goto tail;   /* unsigned */
            k = joy_read(&k);                /* 0000:1e44 */
        }
        controls_update();                   /* 0e12:0080 */
    }
tail:
    if (!crash_active && !chase_view) {      /* DS:B78D, DS:948B */
        if ((velocity_lo | velocity_hi) != 0 /* DS:124E|1250 */ &&
            !steer_key_held && !mouse_steering && wheel_centering && !(frame_count & 1)) {
            if (steer_wheel >= 0x0E && steer_wheel <= 0x12) steer_wheel = 0x10;   /* u8 compares */
            if (steer_wheel <  0x0E) steer_wheel += 2;
            if (steer_wheel >  0x12) steer_wheel -= 2;
        }
        wheel_update();
    }
}
```

`wheel_update` therefore runs up to three times per frame.

### 4.3 Cockpit set-up (0792:0922, 1c66, 0cec)

```c
void cockpit_setup(void)                     /* 0792:0922 */
{
    lever_shown = 0; E330 = 0; lever_hold = 0;
    lever_target = car_lever_step[gear];     /* DS:E565[DS:B6D7] */
    lever_step = lever_target + 1;           /* lever slides one step on the first frame */
    marker_pos = 0xFF; speedo_drawn = 0xFF; tach_drawn = 0xFF; odo_drawn = 0xFF;
    compass_drawn = 0xFF;
    rim_shown = 1;                           /* the centre rim is in 1.BOT/2.BOT */
    odo_roll_tens = 6; odo_roll_units = 6;

    colour_offset = 0;    palette_load("A:OTWCOL.BIN");          /* colours 16..127 */
    colour_offset = 0x80; palette_load("A:<car>COL.BIN");        /* colours 144..255 */
    ega_palette_clear();                                          /* 01f4:1c00, VGA no-op */
    page(1); gfx_set_colour(0); fill(0, 319, 16, 111);           /* black view area on page 1 */
    cockpit_pictures_draw(199);          /* leaves page 1 current, colour_offset = 80h */
    page(1);
    load_pic("<car>.ETC"); pic_draw(buf, DS:0B64, 56, 264, 175, 0);   /* page 1, rows 104..175 */

    u8 o = (gfx_mode == 0x13) ? 0x80 : 0;
    gfx_moveto(280, 156); gfx_set_colour(o + 0x0F); gfx_read_mask(marker_mask, 1, 5);
    gfx_moveto(288, 175);                               /* colour still o+0Fh */
    gfx_read_mask(DS:E090, 4, 24);
    gfx_set_colour(o + 7);    gfx_read_mask(DS:E0F0, 4, 24);
    gfx_set_colour(o + 8);    gfx_read_mask(DS:E150, 4, 24);
    gfx_set_colour(o + 0);    gfx_read_mask(DS:E1B0, 4, 24);
    gfx_set_colour(o + 0x0C); gfx_read_mask(DS:E210, 4, 24);
    gfx_set_colour(o + 4);    gfx_read_mask(DS:E270, 4, 24);

    copy(0, 319, 112, 199,  0, 103,  0, 1);             /* centre cockpit -> page 1 rows 16..103 */
    /* gauge backing stores (33 rows each), from the screen */
    copy(CC7C, CC7C+39,        CC7E-32, CC7E,  168, 136,  0, 1);
    copy(CC7C+40, CC7C+CC84-1, CC7E-32, CC7E,  168, 169,  0, 1);
    copy(CC80, CC80+CC86-1,    CC82-32, CC82,  264, 199,  0, 1);

    load_pic("<car>L.BOT"); pic_draw(buf, DS:0B60, 168, 0, 151, 0);   /* page 1 rows 104..151 */
    load_pic("<car>R.BOT"); pic_draw(buf, DS:0B62, 168, 0, 199, 0);   /* page 1 rows 152..199 */
    load_opponent_pob_nonvga();              /* VGA: nothing */
    colour_offset = 0;
    page(0);
    view_first_frame();                      /* 0792:0cec */
    ega_palette_set();                       /* 01f4:1c34, VGA no-op */
    palette_apply();                         /* 01f4:1c48: the cockpit appears */
}

/* load_pic(ext): strcpy(DS:0AC1, ext) on "A:<car>" (DS:0ABA); res_load_far(DS:0ABA, pic_file_buf);
   lzw_unpack(pic_file_buf, DS:2500). */

void cockpit_pictures_draw(int yb)           /* 0792:1c66; yb = 199 at set-up, 0x67 after water */
{
    mouse_set_pos(160, 100);
    colour_offset = 0x80;
    load_pic("2.BOT"); pic_draw(buf, DS:0B5E, 320, 0, yb, 0);         /* current page */
    load_pic("1.BOT"); pic_draw(buf, DS:0B5C, 320, 0, yb - 44, 1);
    if (yb == 199) {
        load_pic(".TOP"); pic_draw(buf, DS:0B5A, 320, 0, 15, 0);
        copy(0, 319, 0, 199,  0, 199,  1, 0);                        /* page 1 -> screen */
    }
    page(1);
    lzw_unpack(pic_compass, DS:2500);
    pic_draw(buf, 0x180, 152, 0, 7, 0);      /* NB 180h pairs; the file has 17Ah (§9) */
    if (yb != 199) load_opponent_pob_nonvga();
}

void view_first_frame(void)                  /* 0792:0cec */
{
    page(0); text_colours(15, 0);
    frame_update(); frame_draw();
    if (!chase_view) view_present();
    page(0);
}

void hud_reset_topbar(void)                  /* 0792:10a6 */
{
    clock_drawn = 0xFF;
    if (race_state != 2) DS_BD34 = 0xFFFF;   /* render3d cache */
    page(0);
}
```

Note: `cockpit_setup` is called with page 1 holding garbage outside the areas it writes; the page-1
screen copy in `cockpit_pictures_draw(199)` therefore shows black rows 16–111 until the first 3D
frame is presented. The palette is not applied until the very end, so the build-up is invisible
(the previous screen was faded out by game_flow).

### 4.4 Steering wheel rim and marker (0792:1310, 1488, 155e)

```c
static u16 marker_angle(u8 w) { return (u16)(E782 * w * 2) - (u16)(E782 << 5); }  /* E782*(2w-32) */

void wheel_update(void)                      /* 0792:1310 */
{
    int rim_changed = 0;
    rim_wanted = (steer_wheel < 8) ? 0 : 1;
    if (steer_wheel > 0x18) rim_wanted = 2;
    if (rim_wanted != rim_shown) {
        if      (rim_wanted == 0) wheel_rim_blit(0x30, 0);    /* L.BOT  */
        else if (rim_wanted == 2) wheel_rim_blit(0, 0);       /* R.BOT  */
        else                      wheel_rim_blit(0x60, 0x20); /* centre */
        rim_shown = rim_wanted;
        speedo_drawn = 0xFF; tach_drawn = 0xFF;
        odo_drawn = odo_sixtieths ^ 0x80;
        rim_changed = 1;
    }
    if (steer_wheel != marker_pos) {
        s16 x, y;
        if (marker_pos != 0xFF) {                             /* erase: restore saved 16x5 */
            polar(marker_angle(marker_pos), E780);
            x = E784 + polar_x;  y = E786 - polar_y;
            copy(264, 279, 152, 156,  x, y + 4,  1, 0);
        }
        polar(marker_angle(steer_wheel), E780);
        x = E784 + polar_x;  y = E786 - polar_y;
        copy(x, x + 15, y, y + 4,  264, 156,  0, 1);          /* save screen under the marker */
        page(0);
        steer_marker_draw();
    }
    if (rim_changed) { tach_draw(); odometer_draw(); }
}

void wheel_rim_blit(int dy, int dx)          /* 0792:1488; page-1 source = screen - (32-dx, dy) */
{
    copy(dx, dx + 167, 152 - dy, 199 - dy,  32, 199,  1, 0);          /* rim region x 32..199 */
    /* refresh the lower part (screen rows 152..bottom) of the gauge backing stores */
    copy(dx + CC7C - 32, dx + CC7C + 7,            152 - dy, CC7E - dy,  168, 136,  1, 1);
    copy(dx + CC7C + 8,  dx + CC7C + CC84 - 33,    152 - dy, CC7E - dy,  168, 169,  1, 1);
    copy(dx + CC80 - 32, dx + CC80 + CC86 - 33,    152 - dy, CC82 - dy,  264, 199,  1, 1);
}

void steer_marker_draw(void)                 /* 0792:155e */
{
    polar(marker_angle(steer_wheel), E780);
    gfx_moveto(E784 + polar_x, E786 - polar_y + 4);
    gfx_set_colour(car_steer_marker_colour);  /* DS:E77E (low byte) */
    gfx_draw_mask(marker_mask, 1, 5);         /* 8x5, rows y..y+4, current page */
    marker_pos = steer_wheel;
}
```

The three-way switch uses `jae 8` / `jbe 18h` on the unsigned byte: 0–7 → left rim, 8–24 → centre,
25–32 → right. `steer_marker_draw` draws on the current page; every caller has page 0 set. Rows of
the gauge backings above screen row 152 come from `1.BOT`, which never changes, so only the part
from row 152 down is refreshed. The marker save/restore does not include needles; a needle drawn
later under the marker is restored when the marker moves (faithful quirk).

### 4.5 Needles (0792:15d4, 18da)

```c
void tach_draw(void)                         /* 0792:15d4 */
{
    if (CC68 + CC6A == 0) return;            /* car without tach */
    if (tach_step == tach_drawn) return;     /* DS:E5AE vs DS:CC56 */
    page(0);
    copy(168, 207, 104, 136,  CC7C, CC7E,  1, 0);             /* backing, first 40 columns */
    copy(168, CC84 + 127, 137, 169,  CC7C + 40, CC7E,  1, 0); /* remaining columns */
    gfx_set_colour(CEB4);
    gfx_moveto(CC68, CC6A);
    polar((u16)(CC66 * tach_step) - (u16)(0x500 * CC8A), CC64);  /* mul / imul, 16-bit */
    gfx_lineto(CC68 + polar_x, CC6A - polar_y);
    tach_drawn = tach_step;
    steer_marker_draw();
}

void speedo_draw(void)                       /* 0792:18da (no backing restore of its own) */
{
    page(0);
    gfx_set_colour(CEB5);
    gfx_moveto(CC70, CC72);
    polar((u16)(CC6E * speedo_step) - (u16)(0x500 * CC88), CC6C);
    gfx_lineto(CC70 + polar_x, CC72 - polar_y);
}
```

Example (CERV): speedo zero angle = −19·500h = −5F00h → a = 142h (−133.6°, lower left);
full scale 31 steps × 648h = C2F8h → +130°. The needle is one Bresenham line, 1 px wide, both
end points drawn.

### 4.6 Odometer and speedometer (0792:16c4)

The odometer (`CC78`,`CC7A`) is in **page-1 speedo-backing coordinates**: screen position =
(`CC78 + CC80 − 264`, `CC7A + CC82 − 199`) (table in §1.2; descriptions.md calls them "odometer x, y"
without the frame). It is re-rendered into the backing store, and the needle is drawn over it.

```c
void odometer_draw(void)                     /* 0792:16c4 */
{
    if (CC70 + CC72 == 0) return;            /* car without speedo */
    if (odo_sixtieths != odo_drawn) {
        odo_roll_tens = 0; odo_roll_units = 0;
        if (odo_sixtieths > 0x36) {                              /* 55..60 */
            odo_roll_units = odo_sixtieths - 0x36;               /* 1..6 */
            if (odo_units % 10 == 9) odo_roll_tens = odo_roll_units;
        }
        s16 x = CC78, y = CC7A;
        if (y != 0) {
            page(1);
            gfx_set_colour(CEB3);                                /* digit background */
            fill(x, x + 7, y - 4, y);
            fill(x + 10, x + 13, y - 4, y);
            gfx_set_colour(CEB2);                                /* digits */
            gfx_moveto(x, y);      gfx_draw_mask(&DS_B719[(odo_units / 10) * 6 + odo_roll_tens], 1, 5);
            gfx_moveto(x + 4, y);  gfx_draw_mask(&DS_B719[(odo_units % 10) * 6 + odo_roll_units], 1, 5);
            gfx_moveto(x + 10, y); gfx_draw_mask(&DS_B719[odo_sixtieths], 1, 5);
            gfx_plot(x + 8, y);                                  /* decimal point */
            if (speedo_step == speedo_drawn) {                   /* needle not redrawn below */
                copy(x, x + 13, y - 4, y,  x + CC80 - 264, y + CC82 - 199,  1, 0);
                speedo_draw();
                steer_marker_draw();
            }
        }
        odo_drawn = odo_sixtieths;
    }
    if (speedo_step != speedo_drawn) {
        copy(264, CC86 + 263, 167, 199,  CC80, CC82,  1, 0);     /* whole speedo backing */
        speedo_draw();
        steer_marker_draw();
        speedo_drawn = speedo_step;
    }
}
```

`odo_units / 10` and `% 10` are byte divisions (`div cl`). The tenths digit is a continuous roll:
`odo_sixtieths` = 0–60 indexes the strip directly (6 positions per digit). The whole digits roll
during the last 6 sixtieths of a unit. Column x+9 and rows above the point are not cleared.

### 4.7 Odometer accumulation (0792:1952)

```c
void odometer_update(void)                   /* 0792:1952, per frame after odometer_draw */
{
    s32 v = velocity;                        /* DS:124E:1250 */
    if (v < 0) v = -v;                       /* neg/adc/neg */
    odo_accum += (u32)v;                     /* DS:1186:1188 */
    while (odo_accum >= car_odo_step) {      /* DS:94B9 u32: 50000h mile, 31999h km */
        odo_accum -= car_odo_step;
        u8 old = odo_sixtieths++;
        if (old >= 60) {                     /* only when it was already 60 */
            if (odo_units < 99) odo_units++;
            odo_sixtieths -= 60;             /* 61 -> 1 */
        }
    }
}
```

The sequence of `odo_sixtieths` is 0..60, 1..60, 1..60 …; 60 displays as the wrapped "0" of the
next unit with both whole digits fully rolled, so the display is continuous. `odo_units` stops at
99 while the tenths keep rolling. Both are zeroed per leg by `stage_load_objects` (a trip meter).

### 4.8 Gear lever (0792:10c8, 1156)

```c
void shifter_update(void)                    /* 0792:10c8, per frame */
{
    if (lever_always && !lever_shown) shift_lever_draw();
    if (lever_hold == 0 && lever_target != lever_step) {
        if (lever_step < lever_target) lever_step++; else lever_step--;
        shift_lever_draw();                  /* sets lever_hold = 1 */
        if (lever_step == lever_target) lever_hold = 0x10;
    }
    if (!lever_always && lever_hold == 0 && lever_shown == 1) {
        copy(208, 263, 104, 151,  248, 171,  1, 0);   /* put the cockpit back */
        lever_shown = 0;
    }
    if (lever_hold) lever_hold--;
}

void shift_lever_draw(void)                  /* 0792:1156 */
{
    if (!lever_shown) {
        copy(248, 303, 124, 171,  208, 151,  0, 1);   /* save screen under the lever */
        lever_shown = 1;
    }
    copy(264, 319, 104, 151,  208, 199,  1, 1);       /* fresh gate into the compose area */
    page(1);
    gfx_moveto(car_lever_path[lever_step].dx + 208,   /* DS:E56D + 2*step */
               car_lever_path[lever_step].dy + 175);  /* DS:E56E + 2*step */
    u8 o = (gfx_mode == 0x13) ? 0x80 : 0;
    gfx_set_colour(o + 0x0F); gfx_draw_mask(DS:E090, 4, 24);
    gfx_set_colour(o + 7);    gfx_draw_mask(DS:E0F0, 4, 24);
    gfx_set_colour(o + 8);    gfx_draw_mask(DS:E150, 4, 24);
    gfx_set_colour(o + 0);    gfx_draw_mask(DS:E1B0, 4, 24);
    gfx_set_colour(o + 0x0C); gfx_draw_mask(DS:E210, 4, 24);
    gfx_set_colour(o + 4);    gfx_draw_mask(DS:E270, 4, 24);
    page(0);
    copy(208, 263, 152, 199,  248, 171,  1, 0);       /* lever to the screen */
    lever_hold = 1;
}
```

Behaviour: a gear change (`gear_lever_target` sets `lever_target` and `lever_hold = 0`) makes the
lever appear and move one path step per frame; when it arrives it stays 16 more frames and is then
removed (unless `lever_always`). The knob is re-assembled from the six colour masks of the knob
sprite in the `.ETC` (the green background is not captured, so it is transparent). Paths per car
are in descriptions.md (DS:E564).

### 4.9 Top bar (0e12:0b1d, 0b85, 0cbe, 0db4)

All three draw on the current page, which is page 0 at this point of `frame_update`.

```c
void topbar_update(void)                     /* 0e12:0b1d, from frame_update when DS:09C4 == 0 */
{
    if (race_state != 1 || chase_view) { clock_frames = 0; return; }   /* no top-bar updates */
    if (clock_running) {                     /* DS:B707 */
        clock_frames++;
        clock_frac = (u8)(clock_frames + (clock_frames >> 1)) >> 1;
        if (clock_frames < 5) goto draw;
        clock_sec++; clock_frac = 0;
    }
    clock_frames = 0;
    if (clock_sec >= 60) { clock_sec = 0; if (++clock_min >= 60) clock_min--; }  /* stops at 59 */
draw:
    clock_draw(); compass_draw(); radar_draw();
}

static void digit(int x, int d) { gfx_moveto(x, 13); gfx_draw_mask(&DS_B75B[d * 5], 1, 5); }

void clock_draw(void)                        /* 0e12:0b85 */
{
    if (clock_sec == clock_drawn) return;
    clock_drawn = clock_sec;
    gfx_set_colour(0);
    fill(53, 62, 8, 13);
    fill(41, 50, 8, 13);                     /* y0,y1 re-used from the stack */
    gfx_set_colour(CEB7);
    digit(53, clock_sec / 10); digit(58, clock_sec % 10);
    if (clock_min != 0) {
        gfx_moveto(41, 13);
        if (clock_min / 10) gfx_draw_mask(&DS_B75B[(clock_min / 10) * 5], 1, 5);  /* no leading 0 */
        digit(46, clock_min % 10);
    }
}

void compass_draw(void)                      /* 0e12:0cbe */
{
    if (!debug_mode) {                       /* DS:B6EA */
        u8 x = (u8)((view_heading >> 8) + leg_compass_offset) >> 1;   /* 0..127 */
        if (gfx_mode != 0x13) x &= (gfx_mode == 9) ? 0xFE : 0xF8;    /* EGA/Tandy byte alignment */
        if (x != compass_drawn) {
            compass_drawn = x;
            copy(x, x + 23, 0, 7,  8, 14,  1, 0);                     /* page-1 strip -> screen */
        }
        return;
    }
    gfx_set_colour(0); fill(10, 19, 8, 13);
    gfx_set_colour(CEB6);
    digit(10, frame_ticks / 10); digit(15, frame_ticks % 10);
}

void radar_draw(void)                        /* 0e12:0db4, per frame */
{
    u8 lvl = 0xFF;                           /* blank phase */
    radar_phase++;
    if (!(radar_phase & 8)) {
        lvl = radar_level >> 4;              /* 0..6 */
        if ((radar_phase & 7) == 0 && lvl != 0) sfx_play(0x0E);        /* blip every 16 frames */
    }
    if (lvl == radar_drawn) return;
    radar_drawn = lvl;
    if ((s8)lvl < 0) { gfx_set_colour(0); fill(288, 311, 9, 11); return; }
    gfx_set_colour(CEAE); fill(284, 287, 9, 10);                      /* power light */
    gfx_set_colour(CEAF);
    for (int i = 0, x = 295; i < lvl; i++, x += 3) fill(x - 1, x, 9, 10);  /* 2-px bars */
}
```

The compass strip is 152 px for 360° + 24 px of wrap (128 px = 360°), so x+23 never passes 150.
Only the upper heading byte counts: 256 headings → 128 strip positions (2.8° each). The blank
phase clears x 288–311 × 9–11 but not the power light at 284–287, so the light stays on and the
bars blink with period 16 frames (8 on, 8 off).

Race-clock note: a "second" is **5 frames** (`clock_frames` 0–4), not a timer measure; see §7.

### 4.10 Chase-car view and replay panel (0792:059a, 0602, 04ca; 0e12:04f1, 059c)

```c
void chase_view_enter(void)                  /* 0792:059a, when DS:948B turns 1 */
{
    music_stop(0);
    copy(0, 319, 0, 15,  0, 15,  0, 1);      /* save top bar (overwrites the compass strip) */
    page(0); gfx_set_colour(0); fill(0, 319, 0, 15);
    replay_panel_draw();
    panel_shown = 1;                         /* DS:CC92 */
}

void replay_panel_draw(void)                 /* 0792:0602 */
{
    DS_90E0 = 1;                             /* text renderer flag (platform) */
    page(0);
    lzw_unpack(pic_chase, DS:2500);
    ega_palette_clear();
    colour_offset = 0;
    gfx_set_colour(8);    fill(0, 319, 112, 199);
    gfx_set_colour(0x0F); gfx_frame(0, 319, 112, 199);
    gfx_set_colour(7);    gfx_frame(1, 318, 113, 198);
    gfx_set_colour(0);    gfx_frame(2, 317, 114, 197);
    pic_draw(buf, 0x573, 248, 56, 195, 0);   /* CHASE.LZ 248x64 at rows 132..195 */
    load_opponent_pob_nonvga();
    DS_BD34 = 0xFFFF;
    text_colours(0x0C, 0);
    const u8 *s;
    if (!replay_playing && !crash_active) {                  /* F5 chase-car view */
        gfx_set_colour(8); fill(130, 299, 180, 195);        /* hide F9 / F10 buttons */
        s = print_items(DS:1EDE);            /* "CHASE CAR VIEW" col 13 row 1 (top bar), red on black */
        text_colours(0, 7); s = print_items(s);             /* "return" col 9 row 23 */
    } else {                                                /* instant replay */
        s = print_items(DS:1EF8);            /* "INSTANT REPLAY" col 13 row 1 */
        text_colours(0, 7); s = print_items(s);             /* "return", "pause", "replay" row 23 */
        if (!crash_active) { gfx_set_colour(8); fill(56, 129, 180, 195); }  /* hide F5 */
    }
    text_colours(0, 8); s = print_items(DS:1F23);           /* "Viewpoint Control:" col 12 row 15 */
    text_colours(0, 7); s = print_items(s);                 /* "higher", "closer", "tilt", ... labels */
    ega_palette_set();
    if (replay_playing && !crash_active) message_box(0x17); /* "Instant Replay" */
    DS_90E0 = 0;
}

void chase_view_exit(void)                   /* 0792:04ca, when DS:948B turns 0 */
{
    DS_B6D1 = 1;
    music_for_state(DS_B6CC);
    copy(0, 319, 0, 15,  0, 15,  1, 0);      /* restore top bar */
    page(1);
    lzw_unpack(pic_compass, DS:2500);
    pic_draw(buf, 0x17A, 152, 0, 7, 0);      /* compass strip back into page 1 */
    load_opponent_pob_nonvga();
    page(0);
    copy(0, 319, 16, 103,  0, 199,  1, 0);   /* centre-rim cockpit back on screen */
    marker_pos = speedo_drawn = tach_drawn = odo_drawn = compass_drawn = 0xFF;
    rim_shown = 0xFF;                        /* forces wheel_rim_blit on the next wheel_update */
    lever_shown = 0; panel_shown = 0;
}

void replay_panel_clear(u8 all /* DL */)    /* 0e12:04f1 */
{
    gfx_set_colour(8);
    fill(56, 303, 132, 179);                 /* arrow buttons */
    fill(56, all ? 303 : 199, 180, 195);     /* DL = 0: F5/F9 row only, the F10 "replay" button stays */
}
/* Callers: 0e12:044e(arg) passes its argument in DX when panel_shown (0 after a crash, so F10 can
   still request a replay during the lives message; 1 from F5 during a crash replay);
   0ab4:1220 passes 0 before "Replay Completed" (2Bh). */

/* F9, 0e12:059c: if (replay_playing) { text_colours(15, 8); replay_paused ^= 1;
       print_items(replay_paused ? DS:1ECC "Paused" : DS:1ED5 "      "); }  -- col 17 row 19 */
```

Text item layout (`print_items`): `<col><row>` in 8-pixel cells (x = 8·col, y = 8·row).
`DS:1EDE`: `0D 01 "CHASE CAR VIEW" AA 09 17 "return" AA`; `DS:1EF8`: `0D 01 "INSTANT REPLAY" AA
09 17 "return" 80 12 17 "pause" 80 1C 17 "replay" AA`; `DS:1F23`: `0C 0F "Viewpoint Control:" AA
09 11 "higher" 80 12 11 "closer" 80 1B 11 "tilt" …` (strings verified in DGROUP; the rest of the
label list follows the same pattern).

### 4.11 Crash overlays (0792:1c00, 19ca, 1a72; sequence in race_run)

Trigger (race_run, loop top): a crash (`0e12:0f31` sets `DS:B78D`) makes `frame_draw` start the
instant replay (`0e12:4c51`: `DS:948B = 1`, `DS:94B2 = 1`). On the next loop the view-mode flip
with `race_state == DS:E774` and `crash_active` runs:

```c
    if (!crash_in_water) broken_glass_overlay(); else water_overlay_start();
    for (i = 1; i < 0x50; i += 2) {                         /* 40 steps */
        if (!crash_in_water || water_pending != 1) bios_wait_ticks(1);  /* ~2.2 s total */
        else water_roll_step();                              /* full window: no wait (§7) */
        ega_palette_flash_step();                            /* 0e12:0fa1, VGA no-op */
    }
    if (crash_in_water && water_pending == 1) {
        page(1); cockpit_pictures_draw(0x67); page(0); colour_offset = 0;
    }
    water_pending = 0; DS_16D8 = 0;
    0e12:044e(0);                                            /* lives / replay flow (game_flow) */
    /* then chase_view_enter() because DS:948B changed: "INSTANT REPLAY" panel */

void broken_glass_overlay(void)              /* 0792:1c00 */
{
    lzw_unpack(pic_broke, DS:2500);
    sfx_play(2);
    page(0);
    pic_draw_view(buf, gfx_mode == 0x13 ? 0x23C2 : 0x19E1, 95);  /* 320x96 over the 3D buffer, 0Fh clear */
    render3d_7c21();
    DS_BAD4 = 0;
    view_present();
}

void water_overlay_start(void)               /* 0792:19ca */
{
    page(1);
    lzw_unpack(pic_water, DS:2500);
    colour_offset = 0;
    pic_draw(buf, gfx_mode == 0x13 ? 0x1729 : 0x12EA, 160, 0, 95, 0);  /* page 1, 0..159 x 0..95 */
    sfx_play(6);
    copy(0, 159, 0, 95,  160, 95,  1, 1);    /* second copy at x 160..319 */
    water_roll_step();
    page(0);
    load_opponent_pob_nonvga();
}

void water_roll_step(void)                   /* 0792:1a72 */
{
    if (half_window) {                       /* view x 40..279, rows 16..79 */
        copy(40, 168, 0, 15,   40, 31,  1, 0);   /* x1 = 168 (not 167) as coded */
        copy(256, 279, 0, 15,  256, 31, 1, 0);   /* the mirror columns 169..255 are skipped */
        copy(40, 279, 16, 63,  40, 79,  1, 0);
        copy(40, 279, 0, 61,   40, 63,  1, 1);   /* rotate down 2 rows (bottom-up copy) */
        copy(40, 279, 78, 79,  40, 1,   0, 1);   /* screen rows 78..79 -> page-1 rows 0..1 */
        bios_wait_ticks(1);
        return;
    }
    copy(0, 167, 0, 15,    0, 31,   1, 0);
    copy(256, 319, 0, 15,  256, 31, 1, 0);       /* x 168..255 of rows 16..31: mirror */
    copy(0, 319, 16, 95,   0, 111,  1, 0);
    copy(0, 319, 0, 93,    0, 95,   1, 1);
    copy(0, 319, 110, 111, 0, 1,    0, 1);
}
```

The water picture thus fills the view and scrolls down 2 rows per step, the bottom 2 rows
re-entering at the top (80 rows over the 40 steps). It destroys page-1 rows 0–95 (compass strip,
top-bar copy and the centre-rim cockpit copy), which is why `cockpit_pictures_draw(0x67)` rebuilds
the cockpit copy at page-1 rows 16–103 and the compass strip afterwards (the `.TOP` copy on page 1
is not rebuilt; only `chase_view_enter/exit` use that area and they re-save it first).

### 4.12 Window size (F1, 0e12:031c)

```c
    half_window ^= 1; DS_BAD6 = 0;
    message_box(10 + half_window);           /* 0Ah "Window size full" / 0Bh "Window size half" */
    view_geometry_0e12_409c();               /* render3d: DS:BA91 = 60h (96 rows) or 40h */
    if (DS_BA91 != 0x60) {                   /* switched to half: black the unused view area */
        page(0); gfx_set_colour(0);
        fill(0, 39, 16, 111);
        fill(280, 319, 16, 111);             /* y re-used from the stack */
        fill(40, 279, 80, 111);
    }
    gfx_set_page(DS_009A);                   /* no DS:009A write */
    DS_BD34 = 0xFFFF;
```

### 4.13 View-mode / state-change block of race_run (HUD parts)

At the loop top, when `DS:EA78 != DS:948B` (view mode flipped) or `race_state != DS:E774`:

```c
    if (lever_shown) { copy(208, 263, 104, 151,  248, 171,  1, 0); lever_shown = 0; }
    if (DS_E561) car_reset();                /* 0792:083c */
    hud_reset_topbar();
    if (race_state == DS_E774) { if (crash_active) { crash sequence §4.11 } }
    else { lever_shown = 0; E330 = 0; lever_hold = 0;
           lever_target = car_lever_step[gear]; lever_step = lever_target + 1; }
    if (view flipped && !chase_view) chase_view_exit();
    if (view flipped &&  chase_view) chase_view_enter();
    ...
    page(0);
```

### 4.14 Message box (0000:179c, owner game_flow — HUD summary)

`message_box(id)`: `DS:BA82 = 1`, `DS:BAD4 = DS:BAD6 = 15h` (view_present keeps off the box area);
text = `DS:16DA[id]`, first byte = column c. Box x0 = `c < 5 ? 0 : 8c − 40`, x1 = 319 − x0, rows
113–133 (71h–85h) on page 0: screen saved (`0c1c:11f7`), fill colour 7, outline colour 0 at
(x0,x1,113,133), outline colour 8 at (x0+1, x1−1, 114, 132), text black on grey (`text_colours(0,7)`),
then a per-id wait and the saved area restored (`0c1c:1303`), page and text colours restored,
`gfx_show_page(1)` (platform: page semantics to confirm). Id 11h first stops music and effects.
Waits by id (keys come from `DS:915B` / `joy_read`, polled every 4 BIOS ticks):

| ids | wait |
|---|---|
| default | 12 BIOS ticks (0.66 s) |
| 07h, 0Ch, 0Dh, 17h, 18h, 27h, 28h, 2Ch | 30 ticks (1.65 s) |
| 08h, 10h, 14h, 1Ah, 1Eh, 23h, 24h | any key; Esc (80h) quits to DOS (`0000:07e4`) |
| 12h | key; `Y`/`y` quits to DOS |
| 15h, 16h, 25h, 2Bh | any key; F10 (8Ah) during a replay sets `DS:B6D5` (replay requested) |
| 09h, 11h, 1Fh, 2Eh | 12 ticks, then any key |
| 21h | fills the mph digits of message 2Dh (`DS:1B87..1B89` = `BCA5·7 − (DS:00D4 & 7)`), queues 2Dh in `DS:9488`, then as 2Dh |
| 2Dh | stop effects, then as 09h |
| 26h | key; `Y`/`y` → `DS:E560 = 1` |
| 05h, 13h, 31h, 32h | interactive loops (joystick calibration, drive letter, steering 1–9, mouse sensitivity) |

In-race ids: 07h "Returning to road...", 0Ah/0Bh window size, 0Ch "Press F6 to return to the road",
0Dh "Go back to the main road!", 0Eh/0Fh wheel centering, 11h "PAUSE - Press space to resume"
(key code 12h in `key_read`), 15h "This is your last life...", 16h "You have nn lives left",
17h "Instant Replay", 18h "WRONG WAY!", 19h "Replay Requested", 1Bh–1Dh detail level, 1Fh "Your
time is up...", 21h "You just got a ticket, :20 penalty", 25h "GAME OVER", 29h/2Ah engine sound,
2Bh "Replay Completed", 2Ch "Your opponent got a ticket", 2Dh "I clocked you at over nnn mph back
there", 2Eh "Your alignment is out". "WRONG WAY!" and "Go back to the main road!" are raised by
the simulation (invisible trigger sprites, world.md) through `DS:9488`, shown by race_run.

-----------------------------------------------------------------------------------------------

## 5. File formats

All decoded in FORMATS.md / descriptions.md; the HUD-specific facts:

| File | Size (unpacked) | Pairs | Width × rows | Drawn at | Notes |
|---|---|---|---|---|---|
| `<car>.TOP` | | `DS:0B5A` | 320×16 | page 1, bottom 15 (then copied to screen) | clock, compass and radar boxes are painted in |
| `<car>1.BOT` | | `DS:0B5C` | 320×44 | bottom 155 (or 59) | dashboard with the dials |
| `<car>2.BOT` | | `DS:0B5E` | 320×44 | bottom 199 (or 103) | wheel (centre rim) and console |
| `<car>L.BOT`, `R.BOT` | | `DS:0B60/0B62` | 168×48 | page 1 x 0, bottom 151 / 199 | wheel turned left / right; screen x 32–199, rows 152–199 |
| `<car>.ETC` | | `DS:0B64` | 56×72 | page 1 x 264, bottom 175 | gate (top 48 rows), marker glyph (8×5 at column 16, rows 48–52, colour 0Fh) and knob sprite (32×24 at column 24, rows 48–71, colours 0Fh,7,8,0,0Ch,4 on a background of another colour) |
| `COMPASS.LZ` (DATAA and DATAC, identical 275 bytes) | 756 | 17Ah | 152×8 | page 1, bottom 7 | colours 0,4,7,0Ch,0Eh,0Fh from `OTWCOL` range; 128 px = 360° |
| `CHASE.LZ` | 2790 | 573h | 248×64 | page 0 x 56, bottom 195 | eight arrow/F-key buttons |
| `WATER.LZ` | 11858 | 1729h | 160×96 | page 1 x 0 and 160 | EGA: `WATEREGA.LZ`, 12EAh pairs |
| `BROKE.LZ` | 18308 | 23C2h | 320×96 | 3D buffer, bottom 95, 0Fh transparent | EGA: `BROKEGA.LZ`, 19E1h pairs |
| `OTWCOL.BIN` | 337 | | | colours 16–127 | in-game palette |
| `<car>COL.BIN` | 337 | | | colours 144–255 | cockpit palette |

Pair counts of the fixed pictures are hard-coded (last column of the pseudocode); the car ones come
from the `.LST` (descriptions.md, DS:0B50 table).

-----------------------------------------------------------------------------------------------

## 6. Hardware / DOS dependencies and SDL3 replacement

| Original | Use here | Port |
|---|---|---|
| VGA mode 13h memory (page 0) | every HUD draw goes straight to the visible screen | 320×200 8-bit `page0` buffer, converted with the 256-colour palette and presented once per loop iteration (after `race_input(2)`) and inside modal waits |
| Page 1 (conventional-memory 64000 bytes) | picture / backing store | plain `u8 page1[320*200]` |
| VGA DAC (via `palette_apply`) | cockpit palette on at the end of set-up | palette array → SDL texture conversion |
| INT 1Ah (`bios_wait_ticks`) | crash waits, water roll (half window), message waits | 18.2065 Hz tick clock from `SDL_GetTicksNS` |
| INT 33h fn 4 (`mouse_set_pos`) | centres the mouse (mouse steering) | set the virtual mouse position to (160,100) |
| PIT 145.6 Hz (`timer_ticks`) | `frame_ticks`, frame pacing | platform timer |
| Sound effects 2, 6, 0Eh | glass, splash, radar blip | sound spec |

The library routines have no clipping except `gfx_plot` / axis-parallel lines: the port must keep
all writes inside the 64000-byte pages (the only overrun found is the compass quirk, §9).

-----------------------------------------------------------------------------------------------

## 7. Timing

* HUD work happens once per race-loop iteration (a "frame"), which lasts **at least 5 PIT ticks
  (34.3 ms)**; `wheel_update` runs up to three times per frame (after each input phase). Nothing
  HUD-related runs in the timer interrupt.
* Frame-counted behaviour (depends on the frame rate of the original machine): lever animation
  (1 step/frame, 16-frame hold), radar blink (8 on / 8 off) and blip (every 16 frames), wheel
  self-centring (2 positions every other frame), **race clock (1 s = 5 frames)**, water roll in
  full-window mode (40 unpaced steps).
* Time-based: crash waits (40 × 1 BIOS tick ≈ 2.2 s), half-window water roll (1 BIOS tick per step),
  message boxes (12 or 30 BIOS ticks).
* Everything else is event-driven with "drawn" caches (`CC56`, `E860`, `CE9D`, `B709`, `B712`,
  `B70F`, `E5B7`, `E562`); FFh (or `^80h`) forces a redraw.

-----------------------------------------------------------------------------------------------

## 8. Differences from TD2 (style reference)

* TD2 draws its instruments into dedicated 4-plane RAM buffers and copies them; TD3 draws them
  directly on the visible 8-bit page and uses a second full page as picture and backing store.
* TD3 needles are true lines from binary angles (`polar` + Bresenham) with per-car geometry from the
  `.LST`; TD2 used pre-drawn needle positions. The rolling odometer, animated H-gate lever, rim
  pictures, compass strip and radar bars have no TD2 equivalent code; nothing is reusable beyond the
  port skeleton (palette upload, presentation).

-----------------------------------------------------------------------------------------------

## 9. Open questions and notes

1. **Compass pair count quirk**: `cockpit_pictures_draw` draws `COMPASS.LZ` with 180h pairs while
   the file has 17Ah (`chase_view_exit` uses 17Ah). The 6 extra pairs are stale bytes of the unpack
   buffer and are written above row 0 of page 1 (offsets just below the page start, wrapping in the
   segment). Port: draw 17Ah pairs (`/* PORT: */`), or emulate the wrap if a difference is ever seen.
2. `0e12:0080` is named `key_dispatch` in `port/symbols.csv` (seed), but it is `call 0751; retf`,
   the held-control poller; the key-code dispatcher is `0e12:0084`. The simulation spec should settle
   the names; this spec uses `controls_update` / `key_code_dispatch`.
3. RE_GUIDE says VGA uses library mode 14h; the in-game code uses 13h (`DS:00EA`). Platform spec to
   confirm how pages 0/1 map to VGA memory / RAM in 13h (`DS:BD8C`) and what `gfx_show_page(1)` at
   the end of `message_box` does.
4. The race clock advances one second per 5 frames. At the original's likely 5–7 fps this is close
   to real time; at the maximum 29 fps it runs ~6× fast. game_flow/simulation should decide whether
   the port paces frames to reproduce the original speed.
5. The full-window water roll runs 40 steps without any wait (the half-window path waits 1 BIOS
   tick per step). On a 1990 machine each step copies ~60 KB; the port should pace it (suggest 1 BIOS
   tick per step, `/* PORT: */`).
6. `0e12:044e` (lives / end-of-attempt, game_flow) and `0ab4:1220` (replay, simulation) call
   `replay_panel_clear` and `message_box` while the chase panel is up; their control flow is only
   summarised here.
7. `DS:95BE` (leg header 02Ch) is taken as the compass north offset of the leg (Likely: C0h/E0h/20h
   in three legs, 0 elsewhere); world.md lists it as Unknown.
8. `DS:B6EA` (debug mode) has no writer in the indexed code: the frame-time readout and the debug
   keys are unreachable in this build unless set through a path the index missed.
9. No map screen, no look-left/right/rear views and no in-race police pictures exist: `L.BOT` /
   `R.BOT` are steering-wheel rim positions, and `COPA/COPB/COPSEQ.LZ` are only referenced by the
   dead copy-protection code (`01f4:1f2c`).
10. `DS:E330` is written (0) but never read.
