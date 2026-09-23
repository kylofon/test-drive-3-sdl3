# Simulation subsystem (TDIII.EXE: `0977:0008`, `0ab4:1220`, `0e12` driving / traffic / police / collision code, key handlers)

Target: `work/TDIII_unp.exe` (DGROUP `1BE4`). Addresses as in `port/RE_GUIDE.md`. Everything here was
read from `tools/x86dis.py` output; the Ghidra decompile (`port/decomp/tdiii_ds.c`) is only a guide and is
**wrong for `0977:0008`** (it drops blocks as "unreachable" and garbles the long arithmetic). Car constants:
`port/formats/descriptions.md` (`DS:1200..`, `E564..`); world data: `port/formats/world.md`,
`port/formats/objects.md`. Symbols: `port/spec/simulation_symbols.csv`.

Confidence tags: **verified** (read instruction by instruction), **likely**, **guess**.

## 1. Overview

TD3 simulates everything **once per frame**, synchronously, inside the `race_run` loop (`0792:000c`,
game_flow). There is no timer-driven simulation (unlike TD2). The frame loop waits until at least 5
PIT ticks (145.65 Hz) have passed since the frame started, so the game runs at **≤ 29.13 fps**, slower
when rendering takes longer. All speeds, timers and the race clock are **per frame**; only the steering
rate and one joystick poll look at the measured frame length `DS:B70E` (§7).

Per frame (`race_run`, simulation-relevant parts; details §4.15):

```
race_run loop  (0792:000c)
 ├ B70E = ticks of the previous frame; state changes (car_place 0792:083c, crash pictures, life_lost)
 ├ pending message DS:9488 -> show_message
 ├ frame_update 0e12:76ec ........................................... §4.1
 │   ├ 0fa1 screen_shake_step, 095a auto_shift, 23df engine_sound (clamps rpm)
 │   ├ 0b1d race_clock_hud -> 0b85 clock_draw, 0cbe compass, 0db4 radar_detector
 │   ├ 0977:0008 car_physics  (unless ext_view 948B)  ...................... §4.2
 │   ├ 0ab4:1220 replay_update (record / playback / wreck animation) ........ §4.11
 │   ├ 6e92 camera_pos_update (camera X/Z, sun sprite, ground probe point)
 │   ├ 70cd world_cells_collect (render; route detection 0A74) .............. §4.9
 │   └ 34c3 (render root) -> 4d60 -> 4fc7 / 509b traffic_update -> 5466 vehicle_follow_lane
 │                                    5ad4 vehicle_collisions -> 5cf2 ....... §4.12
 │                        5e70 crossing_gate_update, 34f1 near_face_collisions,
 │                        373c (ground reset), 4d6d (traffic lights), 5ffe police_update  §4.13
 ├ race_input(0) 0792:0414 -> 0e12:0080 controls_poll 0751 ..................... §4.4
 ├ frame_draw 0e12:77d5: 323e -> 33f7 sprite_collisions, 3a7c ground query;
 │                        6421 ground_slope_update; water test; crash immunity; 0f31 crash_start;
 │                        4c51 replay_start when crashed ............................. §4.9, §4.10
 ├ race_input(1); HUD (hud spec); race_input(2) -> 0000:0f80 key read, 0e12:0084 key_dispatch §4.7
 ├ surface zones 1Ch-1Fh (weather, wind, FINISH) ........................................ §4.15
 └ wait until >= 5 ticks since the frame start
```

Data dependencies across the frame: `car_physics` in frame N uses the ground (`BA85`, `BA9B`, `BCDC`,
`BCDD`) found while drawing frame N−1; the controls it uses were polled during frame N−1.

### Units

| Quantity | Globals | Encoding |
|---|---|---|
| Position | `pos_x/z/y` `DS:125E/1262/1266` (i32) | world position units << 7 (`sprite_x[0] = pos_x >> 7 & 7FFFh`, `z & 3FFFh`); a map cell is 400h units |
| Speed | `speed_long` `DS:124E` (i32, signed along `course`), `car_speed` `DS:1296` (u16) | per-frame step = `speed_long/16` split by the course angle, then halved; `car_speed` = octagonal norm /180h ≈ `|speed_long|/2048` ≈ 1/1.64 mph (dial calibration, descriptions.md) |
| Angles | `course 124A`, `body_heading 124C`, `view_heading 9491` | u16 binary angle (10000h = 360°); 0 = +Z (north), 4000h = +X (east) for `polar()`; the object heading byte is `(view_heading − 4000h) >> 8` |
| Engine | `engine_rpm` `DS:129C` | ≈ rpm / 10 |
| Throttle | `throttle` `DS:B6D6` | 0..31, ±2 per control poll (3 polls per frame) |
| Wheel | `steer_wheel` `DS:E33A` | 0..32, 16 = centre, 32 = full right |
| Time | race clock `DS:B70C:B70B` | minutes:seconds, **5 frames = 1 clock second** |

## 2. Function table

| Address | Name | Signature | Purpose | Conf. |
|---|---|---|---|---|
| 0e12:76ec | frame_update | far void() | per-frame update root (seed name) | verified |
| 0977:0008 | car_physics | far void() | engine, forces, speed, ground contact/gravity, grip, steering, gauges, rev limit (compiled C) | verified |
| 0e12:234b | polar | far (u16 angle, u16 len) → DS:9460/9462 | len·sin / len·cos from the quarter-sine table CS:0FF2 | verified |
| 0e12:23b1 | grip_yaw_limit | far u16 (u16 grip) | max course change per frame | verified |
| 0e12:6686 | pitch_shear | far u8 (u16 pitch) | pitch → model shear code (table DS:BCE1) | verified |
| 0e12:0fa1 | screen_shake_step | far void() | DS:947C countdown → display offset (CS:0F81) | verified |
| 0e12:0fd6 | bounce_back | near void(), keeps BX CX | position ← 3 frames ago; shake 4 | verified |
| 0e12:0e74 | overrev_damage | far void() | engine or gear damage every 4th over-rev | verified |
| 0e12:0ec5 | landing_damage | far void() | 1/32 gear damage else falls into random_damage | verified |
| 0e12:0edb | random_damage | far void() | suspension/brakes 1/8, alignment 1/64 | verified |
| 0e12:0080 | controls_poll_far | far void() | `call 0751; retf`: far stub of the held-controls poll (the seed name `key_dispatch` belongs to 0e12:0084) | verified |
| 0e12:0084 | key_dispatch | far void() | key code DS:E08C → CS:0000 (40h–7Fh) / DS:B6EF (81h–8Ah) | verified |
| 0e12:0751 | controls_poll | near void() | keys/joystick/mouse bits → throttle, brake, shift, steering; chase-camera keys | verified |
| 0e12:08d9 | mouse_controls | near CL→CL | mouse → wheel, gas/brake | verified |
| 0e12:09d6 | steer_throttle | near (CL bits) | throttle ±2, brake, steering ramp, analog joystick | verified |
| 0e12:0b02 | shift_keys | near (CL) | Enter+Up/Down → gear gates | verified |
| 0e12:0156 | gear_up | near | gear +1, throttle preset | verified |
| 0e12:01d8 | gear_down | near | gear −1, reverse-at-speed damage | verified |
| 0e12:0188 | gear_up_gate | near | key A: gear up / CERV range jump | verified |
| 0e12:0229 | gear_down_gate | near | key Z: gear down / CERV range jump | verified |
| 0e12:01c5 | gear_lever_target | far | lever animation target | verified |
| 0e12:095a | auto_shift | near | automatic box and CERV in-range shifting | verified |
| 0e12:23df | engine_sound | near | sound hook; clamps rpm ≤ 1500 (sound spec) | verified |
| 0e12:00d5 … 06b6, 0534 … 059c | key handlers | near | see §4.7 | verified |
| 0e12:03d4 | detail_apply | near | view window by detail, traffic thinning by skill | verified |
| 0e12:06c3 | debug_projection | near | unreachable debug vertex projection | verified (dead) |
| 0e12:0f31 | crash_start | near | latch `crashed`, music stop, shake 10h, find the object hit | verified |
| 0e12:4c51 | replay_start | near | start replay / crash camera (sets ext_view, replay_state 1) | verified |
| 0e12:044e | life_lost | far (int skip) | lives message, replay request, next life / game over | verified |
| 0e12:04f1 | panel_erase | far (DX) | drawing (replay panel) | verified |
| 0ab4:1220 | replay_update | far | ring-buffer recorder / player / wreck animation | verified |
| 0ab4:120f | replay_clear | far | zero the ring buffer | verified |
| 0ab4:1460 | debris_sprites | near (BX,AX,DX,CX) | 3 debris sprite instances | verified |
| 0ab4:14b9 | replay_play_frame | near | read one recorded frame | verified |
| 0ab4:1544 | replay_rec_objects | near | write visible moving objects | verified |
| 0ab4:1604 | replay_play_objects | near | read object records | verified |
| 0e12:6e92 | camera_pos_update | near | camera/probe point, sun sprite, crash camera target | verified |
| 0e12:6f9c | polar_small | near | 8-bit angle/radius → chase-camera offset | verified |
| 0e12:0b1d | race_clock_hud | near | race clock (5 frames/s), calls the top-bar drawers | verified |
| 0e12:0b85 | clock_draw | near | mm:ss in the top bar (hud) | verified |
| 0e12:0cbe | compass_draw | near | compass tape / dead debug readout of B70E (hud) | verified |
| 0e12:0db4 | radar_detector | near | detector bars and beep from B710 | verified |
| 0e12:2465 | life_reset | far | per-life reset, return-to-road position | verified |
| 0e12:255e | leg_state_reset | far | per-leg reset (world spec name kept) | verified |
| 0e12:61d2 | police_end_chases | near | clear chase flags of all police | verified |
| 0e12:4d60 | moving_objects_update | near | calls 4fc7, 509b, 5ad4, 5df9 | verified |
| 0e12:4fc7 | obj_emit_parked | near | parked 1000h objects; lightning (thunder) | verified |
| 0e12:509b | traffic_update | near | per-object lane following + movement + emit | verified |
| 0e12:5466 | **vehicle_follow_lane** | near (BX = 2i) | lane/waypoint following of every moving object (chosen over `traffic_follow_path`: it also drives opponents, police, train, gulls) | verified |
| 0e12:5aa4 | **lane_list_for_cell** | near (SI cell) → ES:SI | P.BIN list of a cell (chosen over `path_tile_lookup`: the input is a cell) | verified |
| 0e12:5ad4 | vehicle_collisions | near | vehicle/vehicle and vehicle/player boxes | verified |
| 0e12:5cf2 | vehicle_collision_boxes | near (BX = 2j) | box tests → DS:946C | verified |
| 0e12:22fe | vec_angle | near (CX x, DX z) → AX; CX min, DX max | atan2 via CS:12F6 | verified |
| 0e12:5ffe | police_update | near | radar level, chase, pull-over, tickets, siren | verified |
| 0e12:5e70 | crossing_gate_update | near | train detection, gate position BCD8, bells | verified |
| 0e12:5f57 | crossing_gate_arm_vertices | near (SI) | drawing | verified |
| 0e12:61fd / 624a | opponents_final_times / opponent_final_time | far / near | opponent times at leg end | likely |
| 0e12:34c3 | scene_update | near | render root; calls the collision/traffic/police code | verified |
| 0e12:34f1 | near_face_collisions | near | faces near the car: solid, vehicle, signs, barriers | verified |
| 0e12:4590 | barrier_knockdown | near (ES:BP) | flatten a type-1Ah barrier | verified (meaning likely) |
| 0e12:4631 | object_face_hit | near (CH type, ES:DI) | knock over a sign/cone/bird | verified |
| 0e12:33f7 | sprite_collisions | near | sprite collision classes | verified |
| 0e12:3a7c | draw_face_dispatch | near | render dispatcher (the objects spec name is right); contains the ground query at 3d28 | verified |
| 0e12:3d28 | ground_face_check (label) | – | ground height/surface from the triangle around the car | verified |
| 0e12:6316 | tri_height_at_viewpoint | near | height at the probe point | verified |
| 0e12:6421 | ground_slope_update | near | BCDC roll code, BCDD pitch error, BA85 fallback | verified |
| 0e12:6604 | rotate_by_heading | near | 2-D rotation by DS:946A | verified |
| 0e12:6666 | slope_to_tilt_code | near | table DS:BD01 | verified |
| 0e12:70cd | world_cells_collect | near | render; route index 0A74 | verified |
| 0e12:70b8 | obj_face_ranges_clear | near | clear B0F9/B379 | verified |

## 3. Globals

Car constants `DS:1200–1244` and gearbox `DS:E564..` are in `descriptions.md` (referenced here as
`car[0xNNNN]`). "L-r" = cleared by `life_reset` (§4.14).

### Car state

| DS | Name | Type | Meaning | Written by | Read by |
|---|---|---|---|---|---|
| 1246 | overrev_count | u16 | rev-limit points; 10h → overrev_damage | car_physics | car_physics |
| 1248 | offroad_frames | u16 | frames since last on a road surface (& 1FFh); 40h → msg 0Ch | car_physics, F6, car_place | car_physics |
| 124A | course | u16 | direction of travel | car_physics, car_place | car_physics, 4c51, 4c51 camera |
| 124C | body_heading | u16 | car yaw | car_physics, car_place | car_physics |
| 124E | speed_long | i32 | signed speed along the course | car_physics, car_place | race_input, odometer 0792:1952 |
| 1252/1256/125A | vel_x / vel_z / vel_y | i32 | per-frame step (x, z halved) and vertical speed | car_physics | 0ab4:1220 (wreck bounce) |
| 125E/1262/1266 | pos_x / pos_z / pos_y | i32 | position << 7 | car_physics, 0fd6, car_place | car_physics |
| 126A/1276/1282 | pos_hist1/2/3 | i32[3] each | position 1, 2, 3 frames ago | car_physics | 0fd6 |
| 128E | accel | i16 | net force → speed change | car_physics | – |
| 1290 / 1292 | accel_smooth / accel_pitch | i16 | longitudinal accel >> 9, smoothed → pitch | car_physics | – |
| 1294 | speed_oct | u16 | (\|vx\|+\|vz\|+2max)/12, no reader | car_physics | – |
| 1296 | car_speed | u16 | speed magnitude | car_physics | many |
| 1298 | speed_sq | u32 | low word of speed², no reader | car_physics | – |
| 129C / 129E | engine_rpm / rpm_prev | u16 | ≈ rpm/10 | car_physics, 23df (clamp), car_place | HUD, auto_shift |
| 12A0 | on_ground | u8 | | car_physics, car_place | car_physics |
| 12A1 | yaw_slide | u8 | course cannot follow the body | car_physics | car_physics |
| 12A2 | skid | u8 | traction exceeded | car_physics | car_physics |
| 12A4 | rpm_inertia | i32 | inertia term of the drive force | car_physics, car_place | car_physics |
| 9486 | damage | u16 | §4.8 | 0e74, 0ec5, 0edb, gear_down; L-r | car_physics, 23df |
| 948D / 948F | pitch_rate / pitch | i16 | car nose pitch (no clamp: see §4.2 step 11) | car_physics, 14b9, car_place; L-r (948D) | camera, replay |
| 9491 | view_heading | u16 | body_heading & FFC0h | car_physics, 14b9, car_place, 2465 | camera, replay, many |
| 9493 | view_roll | u8 | = BCDC on the ground | car_physics; L-r | camera 94A0 |
| 9494 / 9495 | pos_frac_x / z | u8 | (pos & 7Fh) << 1 | car_physics | 6e92 |
| 9CF3/9F73/A1F3 [0] | sprite_x/z/y | s16 | car position (y = eye) | car_physics, 14b9, 2465 | everything |
| A5B9/A6F9/A839/A979/AAB9 [0] | obj_* | | object 0 = player car | car_physics | render |
| B6D6 | throttle | u8 | 0..31 | controls, gears, car_physics; L-r | car_physics |
| B6D7 | gear | u8 | 0 R, 1 N, 2.. | gears; L-r = 1 | car_physics |
| B6D9 | brake | u8 | 0/1/4/5 | controls_poll, steer_throttle | car_physics |
| E33A | steer_wheel | u8 | 0..32 | controls, race_input, car_place = 10h | car_physics, HUD |
| E08E | crosswind | i16 | wind steering drift | car_physics | car_physics |
| E332 / E548 / E5A8 | resist / engine_drag / drive | i32 | forces | car_physics | car_physics |
| E336 | wind_speed | i16 | random walk in [W·200h, W·400h] | car_physics | car_physics |
| E55F | speedo_step | u8 | car_speed >> 2, ≤ 1Fh, 0 in reverse | car_physics | HUD, police |
| E5AE | tach_step | u8 | rpm >> 5, ≤ CC76 | car_physics | HUD |
| E7E8 | grip | u16 | 7Dh − 6·snow − 3·rain, ×3/4 terrain, ×4/5 verge, 0 water | car_physics | 23b1 |
| CEAA | y_before | i32 | pos_y at frame start | car_physics | car_physics |
| B9BB/B9C3/B9CB | road_mem[3] | {x,z,y,heading} | last safe road positions (every 2nd frame on road); `B9CB` (oldest) is used for respawn | car_physics | life_reset |

### Environment, ground, collisions

| DS | Name | Type | Meaning | Written by | Read by |
|---|---|---|---|---|---|
| 9594 | leg_gravity | u16 | leg file +002 (600 in all legs): gravity 900/frame, snap 2400, grip scale | leg file | car_physics |
| 95D7 | wind_dir | u16 | leg file +045 read as a word | leg file | car_physics |
| BC75 / BC77 | rain_level / snow_level | u16 (byte writes) | 0/4/8 ramps (zones 1Dh/1Eh) | race_run, 255e | car_physics, traffic, colours |
| BC79 | wind_level | u16 | 0/2/4 (zone 1Ch) | race_run, 255e | car_physics |
| BA85 | ground_y | u16 | height of the highest ground face under the probe point | 373c (0), 3d28, 6421, F5, car_place | car_physics, frame_draw |
| BA9B | surface | u8 | ground face type (0Eh–13h, 14h–17h, 1Ch–1Fh; FFh none) | 373c (FFh), 3d28, F5 | car_physics, frame_draw, race_run |
| BCDC | ground_roll | s8 | horizon tilt shift code (0 level) | 6421, 2465, F5 | car_physics → 9493 |
| BCDD | ground_pitch | s16 | ground pitch − car pitch, ±18h | 6421, 2465, F5 | car_physics → 948D |
| BAA7 | bump_frames | u8 | vertical bumps after a hit (1/3/5) | 33f7, 4590, 4631 | car_physics |
| BAA8 | crash_flag | u8 | crash request this frame | 76ec (0), car_physics, 34f1, 3d28, 33f7, 5ad4, frame_draw | frame_draw |
| BAA9 | crash_face | u16 | face index hit, FFFFh none | 76ec, 34f1 | 0f31 |
| BAAB | crash_object | u16 | 2·object hit (0 scenery) | 0f31 | 0ab4:1220, 6e92 |
| BAAD | immunity | u8 | 32h at life start; counts down while `clock_running` or on water; crashes cancelled, no damage | 2465, frame_draw | frame_draw, 0e74, 0ec5 |
| B78D | crashed | u8 | crash latch | 0f31, 2465 | race_run, car_physics, 4c51, … |
| BD3B / BD3C | crash_water / water_anim | u8 | drowned | frame_draw, race_run | race_run, 0ab4 |
| 947C | shake | u8 | screen shake countdown (≤ 10h) | many | 0fa1 |
| 9488 | pending_msg | u16 | message shown at the next frame start | 33f7, 373c, 179c | race_run |
| B6D3 | f6_allowed | u8 | set at 64 off-road frames | car_physics | F6 |
| B9D3 | return_to_road | u8 | F6 request | F6; L-r | life_reset |
| 0A74 | route_index | u8 | route 0–2 of the leg | 70cd | results |

### Modes, replay, clock, input

| DS | Name | Type | Meaning | Written by | Read by |
|---|---|---|---|---|---|
| 948B | ext_view | u8 | chase view (F5) / replay / crash camera: physics, traffic, clock, collisions stop | F5, 4c51, 044e, 1220; L-r | many |
| 948C | frozen | u8 | never non-zero (race_run writes 0): dead | race_run | frame_update, frame_draw, keys |
| 94B2 | replay_state | u8 | 0 record, 1 play, 2 wreck animation | 4c51, 1220, 044e; L-r | many |
| 94B3 | wreck_timer | u8 | 20-frame countdown | 1220 | 1220 |
| 16D4 / 16D6 | replay_pos / replay_end | u16 | ring indices (& FFFh) | 1220, 14b9, 4c51 | same |
| 16D8 | cam_manual | u8 | chase camera does not follow the heading | F5, 0751, F10, 044e, race_run | frame_update |
| 94A5/94A7/94A9 | cam_pitch / cam_y / cam_height | s16 | chase camera | F5, 4c51, 6e92, 0751, frame_draw | frame_update |
| 94AB/94AC/94AD | cam_yaw / cam_dist / cam_roll | u8 | chase camera orbit | F5, 4c51, 0751, frame_update | 6e92 |
| B6D4 / B6D5 | replay_paused / replay_request | u8 | F9 / F10 | keys, 179c, 1220, 044e; L-r | 1220, 044e |
| B707 | clock_running | u8 | set by any gear shift (not in ext_view) and on resume; clock and traffic run only while set | gear_up/down, race_run, 1220; L-r | 0b1d, 509b, frame_draw |
| B708 / B70A / B70B / B70C | clock_frames / clock_sub / clock_sec / clock_min | u8 | race clock | 0b1d, 5ffe, 179c (B708), 255e | results, traffic, police |
| B70E | frame_ticks | u8 | ticks of the previous frame | race_run | car_physics, race_input |
| B710 | radar_level | u8 | 0..6Fh | 5ffe | 0db4 |
| BCA6[3] | tickets | u8 | player, opponent 1, 2 | 5ffe | 5466, 624a |
| 915C | kbd_bits | u8 | ISR direction bits | kbd_isr | controls_poll |
| 947B | joy_bits | u8 | joystick bits | 0000:1e44 | controls_poll |
| E08C | key_code | u8 | pending key | 0000:0f80 | key_dispatch |
| B704 / B705 / B706 | steer_hold / prev_bits / throttle_released | u8 | control state; L-r | controls | controls, car_physics |
| B6CA / B6CB | mouse_on / mouse_div | u8 | F7 | F7 | controls |
| B6DA | wheel_centring | u8 | C key (1 by default) | C | race_input |
| B6D2 / B6D8 / BC7B / B6DB | mirror / headlights / wipers / knob_hidden | u8 | R / H / W / D keys (likely) | keys | render, hud |
| B6DC / B6DD | window_half / detail | u8 | F1 / F2 | keys | render |
| B6DE / B6E0 / B6E2 | view_dist ×1/×2/×4 | u16 | 300h/440h/700h by detail | 03d4 | traffic, render |
| B6EA | debug_keys | u8 | 0 in the exe, never written: dead | – | S/T/N keys, frame_draw, 0cbe |
| 0092 / 0094 / 0095 | joystick_on / joy_analog / auto_gearbox | u16/u8/u8 | Ctrl-J/K, Ctrl-A/D; skill < 3 | 0000:0f80, main | controls, auto_shift |
| 00D2 / 00D4 | rand | u32 | LCG `s = s·41C64E6Dh + 3039h` (0000:0f58), read without advancing | 0000:0f58 (race_run: once per frame + pacing loop) | many |

## 4. Pseudocode

Names follow §3; `car[0xNNNN]` = the car constant at that DS offset (descriptions.md).

### 4.1 `0e12:76ec frame_update` (simulation view; verified)

```c
void frame_update(void)                                  /* 0e12:76ec, far, once per frame */
{
    frame_count++;                                       /* BD39 u16 */
    crash_flag = 0;                                      /* BAA8 u8 */
    crash_face = 0xFFFF;                                 /* BAA9 u16: face index hit, set by the collision code */
    screen_shake_step();                                 /* 0e12:0fa1 */
    auto_shift();                                        /* 0e12:095a */
    engine_sound();                                      /* 0e12:23df: clamps engine_rpm to 1500 */
    if (!attract_mode /*09C4*/) hud_top_update();        /* 0e12:0b1d: clock, countdown, radar detector */
    frame_count8++;                                      /* 9484 u8, no other reader */
    if (!frozen /*948C*/) {
        if (!ext_view /*948B*/) car_physics();          /* 0977:0008 */
        replay_update();                                 /* 0ab4:1220 (records or plays back) */
    }
    replay_camera();                                     /* 0e12:6e92 */
    /* camera from the car (render3d owns 9496..94A0) */
    u16 hd = view_heading; i16 pit = pitch; u8 roll = view_roll; i16 cy = sprite_y[0];
    if (ext_view) {
        hd = cam_yaw << 8;
        if (!cam_moved /*16D8*/) {                       /* auto-follow the car by 2 per frame */
            i8 d = (i8)((view_heading >> 8) - cam_yaw);
            if (d < 0) { if ((u8)d < 0xF0) { cam_yaw -= 2; hd = cam_yaw << 8; } }
            else       { if ((u8)d > 0x10) { cam_yaw += 2; hd = cam_yaw << 8; } }
        }
        pit = cam_pitch; cy = cam_y; roll = cam_roll;    /* 94A5, 94A7, 94AD */
    }
    cam_heading = hd;  cam_pitch_out = pit + BA8F;  cam_roll_out = roll;  cam_eye_y = cy;   /* 9498 9496 94A0 949E */
    /* drifting sprites (clouds/birds) move against the camera direction: fold the signs */
    sprite_drift_z = abs(sprite_drift_z); if ((i16)hd < 0) sprite_drift_z = -sprite_drift_z;
    sprite_drift_x = abs(sprite_drift_x); if ((i16)(hd - 0x4000) < 0) sprite_drift_x = -sprite_drift_x;
    draw_page = 1; gfx_select_page(1);                   /* 1714:000e */
    world_cells_collect();                               /* 0e12:70cd (render3d; route detection §4.9) */
    world_update_and_emit();                             /* 0e12:34c3 (render3d; contains traffic, police, collisions) */
}
```

### 4.2 `0977:0008 car_physics` (compiled C, no optimisation; verified line by line against the disassembly)

The Ghidra output for this function drops several blocks ("Removing unreachable block") and mangles
the long arithmetic: use this pseudocode. Types: `i16/u16/i32/u32`; `L(x)` = sign-extend a 16-bit
value to 32 bits (`cwd`), `UL(x)` = zero-extend. `lmul` = `_aFlmul` (32×32→32), `ldiv` = `_aFldiv`
(signed, truncates toward 0), `uldiv` = `_aFuldiv` (unsigned), `ldiv_eq(p,d)` = `*p = ldiv(*p,d)`.
Unless stated, 16-bit expressions wrap like the original (`mul` keeps only AX where noted).

```c
void car_physics(void)                                   /* 0977:0008, far, once per frame */
{
    i16 squeal = 0;                                      /* bp-8 */
    i32 floor;  i16 a, t, lim_slide, lim_grip;  u16 ang;  int i;

    /* 1. engine inertia ("turbo") term: decays /4, fed by the rpm drop of the last frame */
    ldiv_eq(&rpm_inertia, 4);                            /* DS:12A4 i32 */
    rpm_inertia += L((i16)(rpm_prev - engine_rpm)) * 6;  /* 129E - 129C, cwd, *6 as long */
    rpm_prev = engine_rpm;

    /* 2. position history (3 frames): hist3 <- hist2 <- hist1 <- pos, per axis x,z,y */
    for (i = 0; i < 3; i++) { hist3[i] = hist2[i]; hist2[i] = hist1[i]; hist1[i] = pos[i]; }
          /* hist3 = DS:1282, hist2 = DS:1276, hist1 = DS:126A, pos = DS:125E (x) 1262 (z) 1266 (y) */

    /* 3. remember the last road positions for F6 "return to road"; reset the off-road timer */
    if (on_ground && surface >= 0x10 && surface <= 0x14 && surface != 0x12) {   /* 12A0, BA9B (prev frame) */
        if ((frame_count & 1) == 0) {                    /* BD39 */
            road_mem[2] = road_mem[1]; road_mem[1] = road_mem[0];               /* DS:B9CB <- B9C3 <- B9BB */
            road_mem[0] = (struct){ sprite_x[0], sprite_z[0], sprite_y[0], view_heading };  /* 4 words */
        }
        offroad_frames = 0;  f6_allowed = 0;             /* 1248, B6D3 */
    }

    /* 4. wind gust: random walk of wind_speed inside [W*200h, W*400h], W = wind_level */
    if (wind_level == 0) wind_speed = 0;                 /* BC79, E336 */
    else {
        i16 lo = wind_level << 9;
        t = rand_lo & 0x9C;  if (rand_lo & 0x4000) t = -t;       /* DS:00D2 */
        t += wind_speed;
        if ((i16)(lo << 1) < t) t = lo << 1;             /* signed compares */
        if (t < lo) t = lo;
        wind_speed = t;
    }

    view_heading = body_heading & 0xFFC0;                /* 9491 = 124C & FFC0 (before this frame's steering) */
    ang = course & 0xFC00;                               /* 124A: travel direction quantised to 64 steps */
    y_before = pos_y;                                    /* DS:CEAA/CEAC */

    /* 5. collision bump (set by the sprite/face collision code) */
    if (bump_frames) {                                   /* BAA7 */
        u32 b = (u32)((rand_lo & 0x1C) * 0x60) >> 2;     /* _aFlmul then logical >>2 */
        pos_y += (i32)b - 0xC0;
        bump_frames--;
    }

    /* 6. velocity vector from the scalar speed along the course */
    t = (i16)ldiv(speed_long, 16);                       /* 124E/1250 i32, low word kept */
    if (speed_long < 0) { ang += 0x8000; t = -t; }
    polar(ang, t);                                       /* 0e12:234b -> sin_out DS:9460, cos_out DS:9462 */
    vel_x = L(sin_out);  vel_z = L(cos_out);             /* 1252, 1256 (i32) */
    if (crashed) { vel_y = 0; vel_z = 0; vel_x = 0; }    /* B78D */

    /* 7. speed magnitude (octagonal norm of the per-frame vector) */
    u16 ax_ = abs16(vel_x_lo), bz_ = abs16(vel_z_lo);    /* 16-bit neg of the low words */
    u16 m = (ax_ < bz_) ? bz_ : ax_;                     /* unsigned compare */
    speed_oct = (u16)(ax_ + bz_ + 2*m) / 12;             /* DS:1294, 16-bit sum, unsigned div */
    car_speed = (u16)(ax_ + bz_ + 2*m) / 0x180;          /* DS:1296 */

    /* 8. integrate: the per-frame step is half the vector */
    vel_x = ldiv(vel_x * 6, 12);   vel_z = ldiv(vel_z * 6, 12);                 /* = trunc(v/2) */
    pos_x += vel_x;  pos_z += vel_z;  pos_y += vel_y;    /* loop over 3 longs 1252.. -> 125E.. */

    /* 9. publish the player position (object 0 / sprite 0) */
    sprite_x[0] = obj_x[0] = (u16)((u32)pos_x >> 7) & 0x7FFF;   /* 9CF3, A5B9: logical shift */
    pos_frac_x = (u8)((pos_x & 0x7F) << 1);                       /* 9494 */
    sprite_z[0] = obj_z[0] = (u16)((u32)pos_z >> 7) & 0x3FFF;   /* 9F73, A6F9 */
    pos_frac_z = (u8)((pos_z & 0x7F) << 1);                       /* 9495 */
    sprite_y[0] = (u16)((u32)pos_y >> 7) + car_eye_height;       /* A1F3, 94BF */
    obj_y8[0]   = (sprite_y[0] - car_eye_height) << 3;           /* A839 */
    obj_heading[0] = (((u16)(view_heading - 0x4000) & 0xFF00) >> 8) * 0x101;  /* A979: hi = lo */

    /* 10. ground contact. ground_y = DS:BA85 (height of the face under the car, render/collision) */
    floor = L(ground_y) << 7;
    on_ground = 1;                                       /* 12A0 */
    if (floor < pos_y) {                                 /* above the ground */
        i32 lim = L((i16)(leg_gravity * 4));             /* 9594 = 600 in every leg */
        if (pos_y - floor > lim || vel_y > L(leg_gravity) || vel_y < 0) {
            on_ground = 0;                               /* airborne: gravity */
            vel_y -= L((i16)(leg_gravity * 3) / 2);      /* 16-bit, signed /2 (toward 0): 900 */
            pitch_rate = (speed_long < 0) ? 3 : -3;      /* 948D: nose drops in the air */
        } else if (vel_y >= 0) {                         /* close enough and not rising fast: snap */
            vel_y = 0;  pos_y = floor;
        }
    }
    if (pos_y == floor && vel_y == 0) {                  /* resting */
        view_roll = ground_roll;                         /* 9493 = BCDC (u8) */
        pitch_rate = (damage & 4) ? ground_pitch         /* BCDD */
                   : (i16)((i32)car[0x1240] * ground_pitch / car[0x1242]);   /* imul/idiv 32/16 */
    }
    if (floor > pos_y) {                                 /* hit the ground this frame */
        view_roll = ground_roll;  pitch_rate = (as above);
        i16 u = sgn(car_impact) * (abs16(car_impact) >> 4);   /* 94BD = 1C00h -> 448 */
        i32 pen = floor - pos_y - vel_y;                 /* depth + fall speed */
        if (pen >= L((i16)(u * 5))) sfx_play(19);        /* landing thud (0c1c:1110) */
        if (pen >= L((i16)((0x24 - 2*skill_level) * u))) {     /* 0B04; imul result truncated to 16 bits */
            crash_flag = 1;                              /* BAA8: crash landing */
            vel_y = 0; vel_z = 0; vel_x = 0;
            sprite_y[0] = (u16)((u32)pos_y >> 7) + car_eye_height - 0x10;
        } else if (pen >= L((i16)((0x12 - skill_level) * u))) {
            landing_damage();                            /* 0e12:0ec5 */
        }
        if (vel_y >= 0) vel_y += ldiv(floor - pos_y, 8); /* push out of the ground */
        else            vel_y  = ldiv(-vel_y, 8);        /* bounce: 1/8 of the fall speed, upward */
        pos_y = floor + 1;
    }

    /* 11. pitch (nose angle) integration and the car model's shear */
    t = pitch + pitch_rate;                              /* 948F, 948D */
    if (on_ground) t += accel_smooth;                    /* 1290 */
    pitch = t;       /* the original's clamp (t<=FFh || t>=-FFh) is always true: no clamp */
    obj_pitch[0] = (u16)pitch_shear(pitch) << 8;         /* AAB9 = 0e12:6686(948F) << 8 */

    speed_sq = (u16)(car_speed * car_speed);  speed_sq_hi = 0;   /* 1298/129A: MUL keeps AX only; no reader */

    /* 12. engine speed from the wheels */
    if (on_ground && gear != 1)
        engine_rpm = (u16)uldiv(lmul(L(gear_ratio[gear]), UL(car_speed)), L(car[0x122A]));

    /* 13. surface grip (E7E8) and bumps */
    if (on_ground) {
        grip = 0x7D - (snow_level*6 + rain_level*3);     /* E7E8; BC77, BC75 */
        u16 susp = (damage & 4) >> 1;                    /* 0 or 2 */
        switch (surface) {
        case 0x0E: grip = 0; break;                                            /* water */
        case 0x0F: case 0xFF:                                                  /* terrain / none */
            grip = (u16)(grip * 3) >> 2;
            if (!(rand_lo & 0x60) && car_speed && !shake)
                shake = ((rand_lo & 0x1C) >> 2) + susp + 1;                    /* 947C */
            break;
        case 0x11:                                                             /* verge */
            if (!(frame_count & 1) && car_speed) sfx_play(17);
            grip = (u16)(grip * 4) / 5;
            if (!(rand_lo & 0x70) && car_speed && !shake) shake = susp + 1;
            break;
        case 0x12:
            if (!(frame_count & 1) && car_speed) sfx_play(17);
            grip = (u16)(grip * 4) / 5;
            if (car_speed && !shake) shake = 4;
            break;
        }                                                /* 10h road, 13h, others: full grip */
    }

    /* 14. forces. The loop repeats only for the "throttle released" cruise search (B706). */
    for (;;) {
        drive = L(car[0x1230]) * throttle + L(car[0x122E]);                  /* E5A8 i32 */
        drive = (i32)((u32)lmul(drive, UL(engine_rpm) + 0x200) >> 10);      /* logical shift */
        drive -= (u16)(engine_rpm * engine_rpm) / 30;    /* !! 16-bit square: MUL, then DX cleared */
        drive += rpm_inertia;
        if (car_speed >= car_top_speed) drive = 0;       /* 1244, unsigned */
        if (damage & 0x20)               ldiv_eq(&drive, 4);   /* engine damaged */
        if (damage & (0x100 << gear))    ldiv_eq(&drive, 4);   /* this gear damaged */
        engine_drag = lmul(UL(engine_rpm), UL(engine_rpm)) + lmul(L(car[0x1202]), UL(engine_rpm));
        ldiv_eq(&engine_drag, L(car[0x1208]));           /* E548 */
        resist = uldiv(UL(car_speed), L(car[0x120E]));   /* E332 */
        polar((u16)(body_heading - wind_dir) & 0xFFC0, wind_speed);   /* wind_dir = word DS:95D7 */
        resist += L((i16)(cos_out * 12) / car[0x123E]);  /* head wind; 16-bit product, idiv */
        crosswind = 0;                                   /* E08E */
        if (speed_long != 0) {
            t = (i16)(L(sin_out) / car[0x123E]);
            crosswind = sgn(t) * (abs16(t) >> 4);
            resist += 0x30;
            if (yaw_slide || skid) {                     /* 12A1, 12A2 */
                resist += 0x280;
                if (snow_level) resist += 0x28;
                if (rain_level) resist += 0x14;
            }
            if (on_ground) {
                i32 b = L(car[0x1226]) * brake;          /* B6D9 = 0,1,4,5 */
                switch (damage & 0x18) { case 0: resist += b << 2; break;
                                         case 0x18: resist += b; break;
                                         default: resist += b << 1; }
            }
            if (on_ground) switch (surface) {            /* MUL keeps AX only (DX cleared) */
                case 0x0E: resist += (u16)(0xA0 * car_speed); break;
                case 0x0F: case 0xFF: resist += (u16)(0x50 * car_speed); break;
                case 0x11: resist += (u16)(0x1E * car_speed); break;
                case 0x12: resist += (u16)(0x37 * car_speed); break;
            }
        }
        if (throttle == 0 || !throttle_released) break;  /* B6D6, B706 */
        accel = (i16)drive - (i16)engine_drag - (i16)resist;                   /* 128E, low words */
        if (accel > 0) { throttle--; continue; }
        if (throttle < 0x1D) throttle += 2;
        throttle_released = 0;                           /* one more pass, then break */
    }
    throttle_released = 0;

    /* 15. climbing resistance: dy this frame */
    if (on_ground && car_speed)
        resist += ldiv(lmul(pos_y - y_before, L(leg_gravity)), lmul(L(car[0x1210]), UL(car_speed)));

    /* 16. net force */
    if (gear == 0 && speed_long == 0) speed_long = -1;   /* reverse from standstill: start backwards */
    if (gear == 1 || !on_ground) {                       /* neutral or airborne: engine revs freely */
        accel = -(i16)resist;
        if (drive != 0)
            engine_rpm += (i16)ldiv(drive - engine_drag, L(gear == 1 ? car[0x1236] : (i16)(car[0x1236] << 1)));
    } else {
        accel = (i16)drive - (i16)engine_drag - (i16)resist;
        if ((gear == 0 && speed_long > 0) || (gear != 0 && speed_long < 0))
            accel = -(i16)engine_drag - (i16)drive - (i16)resist;   /* gear against the motion */
    }
    if (on_ground) {                                     /* project on the slip angle */
        polar(course - body_heading, 0x101);             /* cos_out = 256*cos(slip) */
        accel = (i16)((lmul(L(accel), L(cos_out))) >> 8);   /* arithmetic shift of the long */
        if ((u16)((u16)(course - body_heading + 0x2000) & 0x7F00) > 0x4000) {   /* sideways */
            accel -= 0xF0;
            if (snow_level) accel += 0x28;
            if (rain_level) accel += 0x14;
        }
    }

    /* 17. traction limit (skid) */
    a = abs16(accel);
    if (on_ground) {
        lim_slide = (i16)uldiv(lmul(lmul(L(car[0x1204]), L(leg_gravity)), UL(grip)), 0x4B0);
        lim_grip  = (i16)uldiv(lmul(lmul(L(car[0x1206]), L(leg_gravity)), UL(grip)), 0x4B0);
        if (a > lim_slide) skid = 1; else if (a <= lim_grip) skid = 0;         /* signed */
        if (a >= lim_grip) squeal = 1;
        if (yaw_slide == 1 || skid == 1) a = lim_grip;
    } else skid = 0;

    accel_pitch = sgn(a) * (abs16(a) >> 9);              /* 1292 */
    if ((gear != 0 && accel < 0) || (gear == 0 && accel > 0)) accel_pitch = -accel_pitch;
    accel_smooth += (i16)(accel_pitch - accel_smooth) / 2;   /* 1290; signed /2 toward 0 */

    i16 old = accel;
    accel = (i16)ldiv(L(a) * L(car[0x1228]), 4);         /* imul 16x16 -> 32 */
    if (speed_long >= 0) {
        if (old < 0) { if (L(accel) >= speed_long) speed_long = 0; else speed_long -= L(accel); }
        else speed_long += L(accel);
    } else {
        if (old < 0) { if (-speed_long <= L(accel)) speed_long = 0; else speed_long += L(accel); }
        else speed_long -= L(accel);
    }

    /* 18. steering */
    t = steer_wheel * 2 - 0x20;                          /* E33A 0..32 -> -32..+32 */
    t += (i16)((i16)(t * brake) * car[0x123A]) / car[0x123C];   /* 16-bit products, idiv */
    t += crosswind;
    if (damage & 1) t += 4; else if (damage & 2) t -= 4; /* "alignment is out" */
    speedo_step = (speed_long >= 0) ? (u8)(car_speed >> 2) : 0;          /* E55F */
    i16 r = (i16)ldiv(lmul(lmul(UL((u16)((u16)((frame_ticks + 0x11) * car[0x1222]) * 7)), L(t)), 0x50),
                      L((i16)(0x2B8 * car[0x1224])));
    r = (i16)ldiv(lmul(L((i16)(3 * steering_response + 0x10)), L(r)), 0x24);   /* 09C2, 1..9 */
    if (car_speed < 0x0C) r = (i16)ldiv(lmul(L(r), UL(car_speed)), 0x0C);
    if (steer_wheel == 0x20 || steer_wheel == 0) r <<= 1;                    /* full lock */
    if (on_ground && speed_long != 0) {
        body_heading += (speed_long >= 0) ? r : -r;      /* 124C */
        i16 d = body_heading - course;  i16 ad = abs16(d);
        i16 m1 = grip_yaw_limit(car[0x1204]);            /* 0e12:23b1 */
        if (m1 < ad) yaw_slide = 1;
        i16 m2 = grip_yaw_limit(car[0x1206]);
        if (m2 >= ad) yaw_slide = 0;
        if (ad >= m2) squeal = 1;
        if (yaw_slide) ad = m2;
        course += (d < 0) ? -ad : ad;                    /* 124A follows the body at most m per frame */
    }

    /* 19. gauges */
    tach_step = (u8)(engine_rpm >> 5);                   /* E5AE */
    if (speedo_step > 0x1F) speedo_step = 0x1F;
    if (car_tach_max < (u16)tach_step) tach_step = (u8)car_tach_max;        /* CC76 */

    /* 20. tyre noise */
    if (yaw_slide == 1 || skid == 1 || (squeal && on_ground)) {
        if (surface == 0x0E || surface == 0x0F || surface == 0xFF) sfx_play(16);   /* skid on dirt */
        else if (surface == 0x11 || surface == 0x12)                sfx_play(17);
        else                                                        sfx_play(15);   /* squeal */
    } else if (sfx_15_16_started) {                      /* DS:90DE */
        sfx_play(0x8F); sfx_play(0x90); sfx_15_16_started = 0;                 /* stop 15 and 16 */
    }

    /* 21. off-road timer: after 64 frames off a road surface offer F6 */
    offroad_frames = (offroad_frames + 1) & 0x1FF;
    if (offroad_frames == 0x40) { show_message(0x0C); f6_allowed = 1; }      /* "Press F6 to return to the road" */

    /* 22. rev limiter */
    if (engine_rpm > car[0x122C]) {                      /* unsigned */
        overrev++;                                       /* 1246 */
        if (engine_rpm > car[0x122C] + 0x1E) overrev++;
        if (engine_rpm > car[0x122C] + 0x3C) overrev += 2;
        if (overrev >= 0x10) { overrev = 0; overrev_damage(); }                /* 0e12:0e74 */
    } else overrev = 0;
}
```

### 4.3 Small helpers (verified)

```c
/* 0e12:234b polar(angle, len): far, stack (angle, len); unsigned len; results in DS:9460/9462 */
void polar(u16 a, u16 len)
{
    u16 i = (a >> 7) & 0x1FF;                            /* 512 steps per turn */
    u16 f = i & 0x7F;  if (i & 0x80) f = 0x80 - f;
    i16 s = (i16)(((u32)SIN[f] * len) >> 16);         if (i & 0x100) s = -s;
    i16 c = (i16)(((u32)SIN[0x80 - f] * len) >> 16);  if (((i >> 7) & 3) == 1 || ((i >> 7) & 3) == 2) c = -c;
    sin_out = s;  cos_out = c;                           /* 9460 = x (east), 9462 = z (north) */
}
/* SIN = 129 u16 at CS 0e12:0FF2 (image 0x0F112): 0000 0324 0648 096C ... FFFB FFFF,
   = min(FFFFh, round(65536*sin(i*90deg/128))). Read it from the exe. Heading 0 = +Z, 4000h = +X. */

/* 0e12:23b1 grip_yaw_limit(g): far, stack; max change of the course per frame */
u16 grip_yaw_limit(u16 g)
{
    u16 t = (u16)(g * car[0x1200]) >> 2;                 /* MUL, AX only: the CERV (1204=111) overflows: 66600 -> 1064 */
    t = (u16)(((u32)t * grip) / 0xFA);                   /* grip = E7E8; 32/16 DIV */
    u32 p = (u32)t * car[0x1238];
    if (car_speed <= (u16)(p >> 16)) return 0x7FFF;      /* also at speed 0 */
    return (u16)(p / car_speed) >> 1;
}

/* 0e12:6686 pitch_shear(p): far, stack; AL result */
u8 pitch_shear(u16 p)
{
    i8 v = (i8)p;  u8 a = (v < 0) ? -v : v;              /* low byte only */
    u8 d = pitch_shear_tab[(a & 0x7F) >> 2];             /* DS:BCE1, 32 bytes, constant: 00 07 06 05 05 04 04 03 03 03 03 03 03 03 03 03 02 x16 */
    return (v >= 0) ? (u8)-d : d;
}

/* 0e12:0fa1 screen_shake_step: far, per frame */
void screen_shake_step(void)
{
    if (shake == 0) return;                              /* 947C */
    shake--;
    if (video_hw /*E776*/ == 0x13) return;
    u8 dx = SHAKE[shake*2], dy = SHAKE[shake*2+1];       /* CS 0e12:0F81, 16 pairs */
    display_offset(dy, dx);                              /* 1776:0008 (platform: CRTC start / panning); ES = DS */
}
/* SHAKE at 0e12:0F81: 00 00 01 01 01 02 03 00 00 04 02 02 04 01 05 05 01 04 07 02 03 06 04 03 00 07 06 01 02 05 08 08
   pairs are pushed as (first byte, second byte) -> 1776:0008(arg0 = second, arg1 = first). Entry 0 = (0,0) re-centres. */

/* 0e12:0fd6 bounce_back: near; restore the position of 3 frames ago and shake */
void bounce_back(void) { shake = 4; for (i = 0; i < 6; i++) ((u16*)&pos)[i] = ((u16*)&hist3)[i]; }
```

### 4.4 Controls (verified)

Input bits (`DS:915C` from the keyboard ISR, `DS:947B` from the joystick reader `0000:1e44`, same
layout; the ISR maps the numeric keypad / grey arrows through `0c1c:0e12`: Home 5, Up 1, PgUp 9,
Left 4, Right 8, End 6, Down 2, PgDn 0Ah; also `` ` `` = Up and `\` = Left):

| Bit | Key | Meaning here |
|---|---|---|
| 01h | Up | accelerate (throttle +2 per poll) |
| 02h | Down | brake (`brake |= 1`, throttle −2) |
| 04h | Left | steer left (wheel position down toward 0) |
| 08h | Right | steer right (toward 32) |
| 10h | Enter / keypad Enter | shift modifier: with Up = gear up, with Down = gear down |
| 20h | Space | hard brake (`brake = 4`, throttle −2) |

Joystick "back" (947B bit 1) also sets 20h. With the mouse on (F7), `0e12:08d9` replaces the bits.

```c
/* 0792:0414 race_input(n) -- game_flow owns it; called 3x per frame by race_run:
   n=0 after frame_update, n=1 after frame_draw, n=2 after the HUD updates */
void race_input(int n)
{
    if (n == 2) { key = key_read(); /* 0000:0f80: Esc, Ctrl keys, sets key_code DS:E08C */
                  key_dispatch(); /* 0e12:0084 */ }
    else if (!joystick_on /*0092*/ ) controls_poll_far();          /* 0e12:0080 = controls_poll */
    else if (n == 0 || frame_ticks /*B70E*/ > 0x28) { joystick_read(); /*0000:1e44*/ controls_poll_far(); }
    if (crashed /*B78D*/ || ext_view /*948B*/) return;
    /* wheel self-centring ("C" key, B6DA): every other frame while moving and not steering */
    if (speed_long != 0 && steer_hold == 0 && !mouse_on && wheel_centring && !(frame_count & 1)) {
        if (steer_wheel >= 0x0E && steer_wheel <= 0x12) steer_wheel = 0x10;
        if (steer_wheel < 0x0E) steer_wheel += 2;
        if (steer_wheel > 0x12) steer_wheel -= 2;
    }
    wheel_marker_draw();   /* 0792:1310, hud */
}

void controls_poll(void)                                 /* 0e12:0751, near; 0e12:0080 is its far stub */
{
    if (mouse_on) mouse_read(&mouse_x /*E864*/, &mouse_y /*E86E*/, &mouse_buttons /*0104*/);  /* 16f1:000d(E864,E86E,0104) */
    brake = 0;                                           /* B6D9 */
    u8 bits = kbd_bits | joy_bits;                       /* 915C | 947B */
    bits |= (joy_bits & 2) << 4;
    bits &= 0x3F;
    if (mouse_on && !ext_view) bits = mouse_controls(bits);     /* 0e12:08d9 */
    if (!ext_view) {
        u8 old = prev_bits; prev_bits = bits;            /* B705 */
        if ((old & 3) == 1 && !(bits & 1)) throttle_released = 1;   /* B706: Up let go */
    }
    if (bits == 0) {
        steer_hold = 0;                                  /* B704 */
        if (!mouse_on && joystick_on && joy_analog /*0094*/) steer_wheel = 0x10;
        return;
    }
    if (debug_cam_adjust /*B6CD, never set*/) { if (bits & 3) debug_cam_y /*B6CE*/ += (bits & 1) ? 8 : -8; return; }
    if (!ext_view) {
        if (bits & 0x20) { brake = 4; throttle = (throttle >= 2) ? throttle - 2 : 0; }
        if ((bits & 0x10) && lever_target /*CC57*/ == lever_pos /*CE94*/) shift_keys(bits);  /* 0e12:0b02 */
        steer_throttle(bits);                            /* 0e12:09d6 */
        return;
    }
    /* replay camera (948B): cursor keys move the camera (render3d reads these) */
    steer_hold = 0;
    bits &= 0x0F;  if (!bits) return;
    cam_moved = 1;                                       /* 16D8: stops the camera auto-follow */
    cam_elev &= 0xF8;                                    /* 94AC */
    if (bits < 3) {
        u8 e = cam_elev;
        if (bits & 1) { if (e > 0x10) cam_elev = e - 8; }
        else          { e += 8; if (e != 0) cam_elev = e; }
    }
    if (bits == 4) cam_yaw += 4;                         /* 94AB (hi byte of an angle) */
    if (bits == 8) cam_yaw -= 4;
    if (bits == 9    && cam_pitch < 0x30)   cam_pitch += 8;     /* 94A5, signed */
    if (bits == 0x0A && cam_pitch > -0x18)  cam_pitch -= 8;
    if (!frozen /*948C*/) {
        if (bits == 5 && cam_dist < 0x140) cam_dist += 4;       /* 94A9, unsigned */
        if (bits == 6 && cam_dist > 0x2E)  cam_dist -= 4;
    } else {
        if (bits == 5) cam_y += 4;                        /* 94A7 */
        if (bits == 6) cam_y -= 4;
    }
}

u8 mouse_controls(u8 bits)                               /* 0e12:08d9, CL in/out */
{
    i16 x = mouse_x - 0xA0;  u8 q = (u8)(abs16(x) / mouse_div);   /* B6CB, 8-bit DIV */
    i8 w = (x < 0 ? -q : q) + 0x10;  if (w < 0) w = 0;  if ((u8)w > 0x1F) w = 0x1F;
    steer_wheel = w;
    bits &= 0xF0;
    if (mouse_buttons & 1) bits |= 2;  else if (mouse_buttons & 2) bits |= 1;   /* left = brake, right = gas */
    if (!(bits & 3)) {
        i16 y = mouse_y - 0x64;  q = (u8)(abs16(y) / (u8)(mouse_div + 5));
        i8 v = (y < 0 ? -q : q) + 8;  if (v < 0) v = 0;  if ((u8)v > 0x10) v = 0x10;
        if ((u8)v <= 2) bits |= 1;  else if ((u8)v >= 0x0E) bits |= 2;       /* push forward = gas */
    }
    return bits;
}

void steer_throttle(u8 bits)                             /* 0e12:09d6, CL = bits */
{
    if (bits & 3) {
        if (bits & 1) {
            if (throttle < 0x1E) {                       /* B6D6; reaches 30 (even) or 31 (odd start) */
                throttle += 2;
                if (auto_gearbox /*0095*/ && gear == 1) gear_up();     /* auto: N -> 1st on gas */
            }
        } else {
            brake |= 1;
            throttle = ((i8)(throttle - 2) >= 0) ? throttle - 2 : 0;
        }
    }
    u8 w = steer_wheel, w0 = w;
    if (!(bits & 0x0C)) {
        steer_hold = 0;
        if (joystick_on && joy_analog && !mouse_on) steer_wheel = 0x10;
        return;
    }
    u8 step = steer_hold + 1;  if (step < 3) steer_hold = step;   /* 1, 2, 3, 3 ... */
    if ((bits & 0x0C) == 0x0C) {                         /* both: towards the centre */
        if (w > 0x10) { w -= step; if (w < 0x10) w = 0x10; }
        else if (w < 0x10) { w += step; if (w > 0x10) w = 0x10; }
    } else {
        if (bits & 8) {
            if (w < 0x0A) w += 1;
            w += step;  if (w > 0x20) w = 0x20;
            if (w >= 0x10 && w0 < 0x10) { steer_hold = 0; w = 0x10; }       /* stop at centre */
        }
        if (bits & 4) {
            if (w > 0x16) w -= 1;
            w -= step;  if ((i8)w < 0) w = 0;
            if (w <= 0x10 && w0 > 0x10) { steer_hold = 0; w = 0x10; }
        }
    }
    steer_wheel = w;
    if (joystick_on && joy_analog) {                     /* analog stick overrides */
        u16 c = joy_cx /*00BE*/, x = joy_x /*00C2*/;
        if ((i16)(x - c) >= 0) steer_wheel = (u8)((u32)(x - c) * 15 / (u16)(joy_xmax /*00C0*/ - c) + 0x10);
        else                   steer_wheel = (u8)(0x0F - (u32)(c - x) * 15 / (u16)(c - joy_xmin /*00BC*/));
    }
}

void shift_keys(u8 bits)                                 /* 0e12:0b02 */
{
    if (bits & 1) gear_up_gate(); else if (bits & 2) gear_down_gate();
}
```

### 4.5 Gearbox (verified)

`gear` `DS:B6D7`: 0 = R, 1 = N, 2.. = 1st..; `car_top_gear` `DS:E564` = highest index (7 CERV, 6 others);
`car_gears_per_range` `DS:E5A5` = 1 (sequential) or 3 (CERV: low range 2–4, high range 5–7).
`auto_gearbox` `DS:0095` = 1 when skill < 3 (set by `main`). All shifts are ignored while `ext_view`.

```c
void gear_up(void)                                       /* 0e12:0156 */
{
    if (ext_view) return;
    clock_running = 1;                                  /* B707: starts the race clock and traffic; lets the immunity count down */
    if (gear >= car_top_gear) return;
    gear++;
    if (gear >= 2) throttle = (u8)((gear == 2) ? car[0x1232] << 1 : car[0x1232]);   /* 1st gets 2x */
    gear_lever_target();
}
void gear_down(void)                                     /* 0e12:01d8 */
{
    if (ext_view) return;
    clock_running = 1;
    if (gear == 0) return;
    if (--gear == 0 && speed_long >= 0 && car_speed > 0x14) {   /* into R while rolling forward */
        damage |= 0x100;                                 /* reverse gear broken */
        sfx_play_ax(11);                                 /* grind */
    }
    if (gear != 1) throttle = (u8)((gear == 0) ? car[0x1234] << 1 : car[0x1234]);    /* R gets 2x */
    gear_lever_target();
}
void gear_lever_target(void) { lever_done = 0; /*E5B6*/  lever_target = car_lever_step[gear]; /*CC57 = E565[gear]*/ }  /* 0e12:01c5 far */

void gear_up_gate(void)                                  /* 0e12:0188: key A, Enter+Up */
{
    u8 n = car_gears_per_range, g = gear;
    if (n == 1 || g < 2) { gear_up(); return; }
    u8 t = n + 2;                                        /* first gear of the high range */
    if (g >= t) { t += n; if (t > car_top_gear || g >= t) return; }
    gear = t;  throttle = (u8)car[0x1232];  gear_lever_target();              /* no replay check, no B707 */
}
void gear_down_gate(void)                                /* 0e12:0229: key Z, Enter+Down */
{
    u8 n = car_gears_per_range, g = gear;
    if (n == 1 || g <= 2) { gear_down(); return; }
    u8 t = n + 1;                                        /* top of the low range */
    if (g <= t) { gear = 2; gear_down(); return; }       /* low range -> N */
    if (g > (u8)(t + n)) t += n;
    gear = t;  throttle = (u8)car[0x1234];  gear_lever_target();
}

void auto_shift(void)                                    /* 0e12:095a, per frame before car_physics */
{
    u16 rpm = engine_rpm;  u8 g = gear;
    if (rpm >= car[0x120A] && g >= 2) {                  /* up-shift point */
        if (auto_gearbox) { gear_up(); return; }
        u8 n = car_gears_per_range;
        if (n == 1) return;                              /* manual: never */
        /* CERV in manual mode: shifts inside the current range */
        if (g != n + 1 && g != 2*n + 1 && g < 3*n + 1) { gear_up(); return; }
    }
    if (throttle != 0 || rpm >= car[0x120C] || g == 1) return;
    if (g == 0) { if (auto_gearbox) gear_up(); return; }        /* R -> N when coasting */
    if (auto_gearbox) { gear_down(); return; }                  /* down to N when coasting */
    u8 n = car_gears_per_range;
    if (n == 1) return;
    if (g != 2 && g != n + 2 && g != 2*n + 2) gear_down();
}
```
Note the automatic drops to **neutral** when the throttle is 0 and rpm < `120C`, and the next
throttle press (`steer_throttle`) engages 1st again.

### 4.6 Engine-sound hook (`0e12:23df`, sound spec owns the driver)

Simulation-relevant side effect: when the engine sound is on (`DS:C900 == 0`) and not in replay it
**clamps `engine_rpm` to 1500 (5DCh)** every frame before `car_physics` runs; pitch = `rpm·4 + 1300h`
(+400h unless the engine is damaged, `damage & 20h`). With the engine sound off (Ctrl-E) the clamp
does not happen.

### 4.7 Key dispatch and every key (verified)

`0e12:0084 key_dispatch` (far; called by `race_input(2)` after `0000:0f80` put the key in
`key_code` `DS:E08C`):

```c
void key_dispatch(void)                             /* 0e12:0084 */
{
    draw_page = 0; gfx_select_page(0);                   /* DS:009A = 0; 1714:000e(0) */
    u8 k = key_code;  key_code = 0;
    if (k >= 0x40 && k < 0x80)                  key_table_cs[k - 0x40]();   /* 64 near ptrs at 0e12:0000 */
    else if (k >= 0x81 && k <= 0x8A && race_state /*DS:008A*/ < 2) key_ext_handlers[k - 0x81]();  /* DS:B6EF */
    else controls_poll();                                 /* 0, <40h, 80h, >8Ah, or F-keys when race_state >= 2 */
}
```
`0e12:0080 controls_poll_far` (far) is only `controls_poll()`. Unassigned entries point at `0e12:00d5`
(= `controls_poll`), so a key with a handler **replaces** that poll's control reading.
The ISR (`0c1c:0e98`, platform) produces lower case letters, upper case with Shift or Caps Lock
(`0c1c:0d32` / `0c1c:0da2` tables), F1–F10 = 81h–8Ah, Esc = 80h, and with Ctrl held the codes of
`0c1c:0cc2` (Ctrl-Q 11h, Ctrl-P 12h, Ctrl-S 13h, Ctrl-J 14h, Ctrl-K 15h, Ctrl-E 16h, Ctrl-A 17h, Ctrl-D 18h).

Table `0e12:0000` (hex dump of the 64 words, index = code − 40h):
```
00d5 0188 00d5 0133 0269 00d5 00d5 00d5 030f 00d5 00d5 00d5 00d5 0630 0667 06b6
00d5 00d5 012d 0606 05e3 00d5 0149 0302 00d5 00d5 0229 00d5 00d5 00d5 00d5 00d5
00d5 0188 00d5 0133 0269 00d5 00d5 00d5 030f 00d5 00d5 00d5 00d5 0630 00d5 00d5
00d5 00d5 012d 0606 00d5 00d5 0149 0302 00d5 00d5 0229 00d5 00d5 00d5 00d5 00d5
```
`DS:B6EF` (F1..F10): `031c 03ab 05cd 00d5 026f 056a 00d9 00d5 059c 0534`.

| Key | Handler | Action | Conf. |
|---|---|---|---|
| A / a | `0188 gear_up_gate` | gear up (CERV: jump to the high range) | verified |
| Z / z | `0229 gear_down_gate` | gear down (CERV: back to the low range / N) | verified |
| C / c | `0133 key_wheel_centring` | `wheel_centring` `DS:B6DA` ^= 1; message 0Eh + B6DA ("Wheel centering off" / "on"); default on | verified |
| D / d | `0269 key_knob_toggle` | `DS:B6DB` ^= 1: hides/shows the gear-lever knob animation (`0792:10c8`, hud) | likely |
| H / h | `030f key_headlights` | not in replay: `DS:B6D8` ^= 1 (read by `0e12:4b37` cockpit drawing; reset per leg) — headlights | likely |
| W / w | `0302 key_wipers` | not in replay: `DS:BC7B` ^= 1 (read by `0e12:4a5c` windscreen drawing) — wipers | likely |
| R / r | `012d key_mirror` | `DS:B6D2` ^= 1 (read by the renderers `3a9e`, `7b9b`, `7c21`, `3a7c`, `323e`) — rear-view mirror on/off | likely |
| M / m | `0630 key_radio` | radio station `DS:B6CC` = (B6CC+1) % 3; `music_for_state(B6CC)` (`01f4:1d22`); message 22h "Change radio station" if it returned non-zero, else 28h "Music must be on to hear radio" when `music_off` (`DS:008E`) = 1, else 27h "Sounds must be off to hear radio" | verified |
| V / v | `0149` | message 20h "TEST DRIVE III Version 3.0" | verified |
| S / s | `0606` | **debug**, needs `DS:B6EA` ≠ 0 (0 in the exe, never written → dead): snow level `BC77` = (BC77+1) % 9, `95C8` = (BC77 ≠ 0), `build_colour_remap` | verified |
| T | `05e3` | debug (B6EA): rain level `BC75` cycle 0–8 likewise | verified |
| N | `0667` | debug (B6EA): toggle `colour_mode` `95C7` and set the sky colours `95C0..C3` = 9,8,72h,13h (95C7 → 0) or 0Bh,7,79h,19h (→ 1); `build_colour_remap` | verified |
| O | `06b6` | only while frozen (`948C`): `DS:B6D0` ^= 1 (flag of the unreachable debug projection `0e12:06c3`) | verified |
| F1 | `031c key_window_size` | `DS:B6DC` ^= 1, message 0Ah+B6DC ("Window size full"/"half"), `0e12:409c` (view layout), redraw borders when `BA91` ≠ 60h, `BD34` = FFFFh (force the visible-cell rebuild) | verified |
| F2 | `03ab key_detail` | `detail` `DS:B6DD` = (B6DD+1) % 3, `detail_apply` `0e12:03d4`, message 1Bh+B6DD ("Detail level low/medium/high"), `BD34` = FFFFh | verified |
| F3 | `05cd` | message 31h "Select steering sensitivity (1=low to 9=hi): n" with the digit `09C2`+'0' patched in (`DS:1C18`); the input/new value is handled inside `show_message` (`0000:179c`, game_flow) | verified |
| F4, F8 | `00d5` | nothing (controls poll) | verified |
| F5 | `026f key_chase_view` | "CHASE CAR VIEW": if `replay_state` (`94B2`) = 0: `BAD6` = 0; toggle `ext_view` `948B` (world stops: no physics, traffic, clock, collisions); on → `sfx_play_ax(0)`, `BA55` = 1, camera = behind the car (`94AB` = heading hi byte, `16D8` = 1, `94AD` = 0, `94A5` = 0, `94A7` = sprite_y[0], `94AC` = 40h, `94A9` = eye height), save `BA85`/`BCDD`/`BCDC`/`BA9B` to `B6E4/B6E6/B6E8/B6E9`; off → restore them. If `replay_state` ≠ 0 and `crashed`: `life_lost(1)` (`0e12:044e`, skip the crash replay) | verified |
| F6 | `056a key_return_to_road` | if not replay and `f6_allowed` (`B6D3`, set after 64 frames off-road): `B9D3` = 1 (reset request, see §4.12), `E561` = 1, `E774` = 2, `offroad_frames` = 0, message 7 "Returning to road..." | verified |
| F7 | `00d9 key_mouse` | only when `DS:09C4` = 0, `race_state` = 1 and not replay: `mouse_on` `B6CA` ^= 1; on → `16ef:0009(A0h, 64h)` (centre the mouse), message 32h asks "Mouse steering sensitivity desired (1-9)", `mouse_div` `B6CB` = 3Bh − answer ('9' → 2 … '1' → 10); off → message 33h "Mouse steering off" | verified |
| F9 | `059c key_replay_pause` | only while `replay_state` ≠ 0: text colours (`0c1c:0733(0Fh, 8)`), `replay_paused` `B6D4` ^= 1, print "Paused" (`DS:1ECC`) or blanks (`DS:1ED5`) via `01f4:1a82` | verified |
| F10 | `0534 key_instant_replay` | `replay_state` = 0: `replay_start()` (`0e12:4c51`, instant replay of the ring buffer), `16D8` = 0, `sfx_play_ax(0)`. `replay_state` ≠ 0: `sfx_play_ax(0)`, `replay_request` `B6D5` = 1, message 19h "Replay Requested" (replay again) | verified |
| Esc, Ctrl-keys | `0000:0f80` (game_flow) | Esc: in a race (`game_state` 5) and not `replay_mode` message 26h "Exit to MAIN SELECT SCREEN (Y/N)?", Y → `race_state` = 3, leg = last; otherwise 12h "Exit to DOS". Ctrl-Q music (1/2), Ctrl-E engine sound `C900` (29h/2Ah), Ctrl-S sound (3/4), Ctrl-P pause (11h), Ctrl-J joystick on `0092`=1 (5), Ctrl-K keyboard only (6), Ctrl-A analog joystick `0094`=1 (2Fh), Ctrl-D digital (30h) | verified (dispatch), game_flow |

`detail_apply` (`0e12:03d4`, also called at every life start by `0e12:2465`):
```c
void detail_apply(void)                                  /* 0e12:03d4 */
{
    u16 cx = rand_seed_ba /*B6ED*/, dist = 0x700;        /* high */
    if (detail <= 1) { cx &= 0x5555; dist = 0x440; if (detail != 1) { cx = 0; dist = 0x300; } }
    view_dist = dist; view_dist2 = dist << 1; view_dist4 = dist << 2;   /* B6DE, B6E0, B6E2 */
    veg_mask = cx;                                       /* BA56: vegetation sprite bits (world.md) */
    /* traffic density by skill: strip the flags of some moving objects */
    u8 keep;  if (skill_level < 3) keep = 2; else if (skill_level < 6) keep = 3; else return;
    u16 bx = 2*obj_a475;  u8 ch = 5, dl = keep;          /* exact register loop */
    goto L43d;
L426: if ((i8)--dl >= 0) goto L43d;
      { u16 m = obj_flags[bx/2] & 0x3F;
        if (m < 0x12 || m > 0x14) obj_flags[bx/2] = m; } /* strip all flags except the model; trains 12h-14h kept */
L43d: if (--ch == 0) { ch = 4; dl = keep; }
      bx -= 2;  if (bx > 4) goto L426;
}
```
Objects `A475−1` down to 3 are processed in groups of 4: the first `keep` of each group are left
alone, the rest lose their flags (no "moving vehicle" bit 2000h any more): skill 0–2 keeps 2 of 4
traffic vehicles, skill 3–5 keeps 3 of 4, skill 6–8 all. Objects 0–2 (player, opponents) are never
touched. Idempotent, so running it on every life start and F2 press is harmless.

### 4.8 Damage (verified)

`damage` `DS:9486` (u16), cleared per life (`0e12:2465`, see §4.12):

| Bit | Set by | Effect |
|---|---|---|
| 0001h / 0002h | `random_damage` (message 2Eh "Your alignment is out") | steering input +4 / −4 |
| 0004h | `random_damage` (sfx 12) | suspension: `pitch_rate` = raw ground pitch; +2 on terrain shake |
| 0008h | `random_damage` (sfx 12) | brakes: brake force ×2 instead of ×4 (×1 if 10h also set; nothing sets 10h) |
| 0020h | `overrev_damage` (sfx 10) | engine: drive force /4; engine note 400h lower |
| 0100h << gear | `overrev_damage`, `landing_damage` (sfx 11); `gear_down` into R at speed > 14h (bit 0100h) | that gear: drive force /4 |

```c
void overrev_damage(void)                                /* 0e12:0e74 far */
{
    if (skill_level < 3 || invulnerable /*BAAD*/) return;
    overrev_events = (overrev_events + 1) & 3;           /* B78E: every 4th event */
    if (overrev_events) return;
    if (gear == 1 || ((rand_hi /*DS:00D5*/ & 3) == 0)) { sfx_play_ax(10); damage |= 0x20; return; }
    if (gear == 1) return;
    sfx_play_ax(11);  damage |= 0x100 << gear;
}
void landing_damage(void)                                /* 0e12:0ec5 far, falls into 0edb */
{
    if (skill_level < 3 || invulnerable) return;
    if (((rand_lo >> 8) & 0xF8) == 0) {                  /* 1/32 */
        if (gear != 1) { sfx_play_ax(11); damage |= 0x100 << gear; }
        return;
    }
    random_damage();
}
void random_damage(void)                                 /* 0e12:0edb far; also called by 33f7, 4631 */
{
    u16 r = rand_lo >> 2;  u8 h = r >> 8;  u8 side = r & 1;
    if ((h & 0x65) == 0) { sfx_play_ax(12); damage |= 4 << side; return; }     /* 1/8: suspension or brakes */
    if (h & 0x1F) return;
    show_message(0x2E); damage |= 1 << side;                                   /* alignment */
}
```
(`rand_lo` = `DS:00D2` u16, `rand_hi` = `DS:00D4`; owner of the generator: game_flow/platform.)

### 4.9 Ground query and collisions (verified unless noted)

The ground under the car is **not** a map lookup: while `frame_draw` draws each triangle it checks
whether the probe point (`DS:94A1/94A3` = camera X/Z in vertex units, = the car in the cockpit view)
lies inside it, and if so interpolates the height. The results are read by `car_physics` next frame.

Collision summary:

| Source | Detector | Test | Effect |
|---|---|---|---|
| face type 1 (walls, rails) near the car | `34f1` (update) | sort depth ≤ `94C1` (60h) and vertical overlap with `[eye−28h, eye+28h]` | `car_speed < 14h`: `bounce_back`; else `crash_flag = 1` |
| face type 2 (all vehicle bodies) | `34f1` | same | `crash_flag = 2` (no slow exception), `crash_face` = face → `crash_object` |
| face types 18h/19h/1Bh (signs, cones, stop sign, birds) | `34f1` | depth only | `object_face_hit`: model swap, sfx 5 (12h for model 38h→39h), shake 5, `bump_frames` 5, `random_damage` |
| face type 1Ah (road-tile barriers) | `34f1` | depth only | `barrier_knockdown`: flatten, sfx 4, shake 5, `bump_frames` 3 |
| face type 1 containing the probe point | `3d28` (draw) | point in triangle | as type 1 above |
| sprites (class = id hi & E0h) | `33f7` (draw) | distance < `95CF` (80h) | table below |
| moving vehicles | `5ad4` (§4.12) | collision boxes | player hit → `crash_flag |= 1` |
| hard landing | `car_physics` | penetration | `crash_flag = 1` |
| water | `frame_draw` | `surface == 0Eh && sprite_y[0] − eye ≤ ground_y` | `crash_flag = 1`, drowned |

```c
void near_face_collisions(void)                          /* 0e12:34f1, only when !ext_view */
{
    for (k = 0; k < face_count /*BAD8*/; k++) {          /* sorted, nearest first; ES = face segment E5BA */
        if (sort_key[k] > near_hit_depth /*94C1 = 60h*/) return;
        rec = face_ptr[k];  u16 t = (rec.w3 & 0xF800) >> 11;
        if (t == 0) continue;
        if (t >= 0x18) { if (t > 0x1B) continue;
                         if (t == 0x1A) barrier_knockdown(rec); else object_face_hit(t, k); continue; }
        if (t > 2) continue;
        u16 mn = FFFFh, mx = 0; for each vertex v of rec: mn = min(mn, Y[v]), mx = max(mx, Y[v]);  /* u16 */
        u16 top = sprite_y[0] + 0x28 /*94C3*/, bot = sprite_y[0] - 0x28; if ((i16)bot < 0) bot = 0;
        if (mn > top || mx < bot) continue;
        if (t == 1 && abs16(car_speed) < 0x14) { bounce_back(); continue; }
        crash_flag = t;  crash_type /*BAAE, unread*/ = t;  crash_face = (rec - face_base /*E5B8*/) / 10;
    }
}
/* sort key (0e12:361c, render): d(v) = horizontal distance + |Y[v] - eye|; point d0; line (d0+d1)>>1;
   triangle s=(d0+d1+d2)>>1 (RCR), s - s/4 - s/16; quad (sum>>1)>>1; w0 bit 13: max. */

void sprite_collisions(void)                             /* 0e12:33f7 */
{
    if (ext_view) return;
    for (k = sprite_list_count /*94AE*/ - 1; k >= 0; k--) {   /* near -> far */
        if (sprite_dist[k] /*8902*/ >= sprite_hit_dist /*95CF = 80h*/) return;
        u16 *id = &sprite_id[sprite_idx[k] /*8B62*/ / 2];
        switch (hi(*id) & 0xE0) {
        case 0x00: break;
        case 0x60: if (abs16(car_speed) < 0x14) { bounce_back(); return; }       /* trees, rocks */
                   if (lo(*id) == 0x10 && gate_pos /*BCD8*/ < 0x30) return;      /* crossing barrier sprite */
                   crash_flag = 1; return;
        case 0x20: case 0x40:                                                     /* knock over */
                   bump_frames = 1; sfx_play_ax(3); shake = 5; *id |= 1;
                   if ((hi(*id) & 0xE0) == 0x20) *id = 0x001F; else random_damage();   /* 40h: keeps its class */
                   break;
        case 0x80: *id = 0; windscreen_splat(rand_lo, (rand_hi & 8) ? 0xC0 : 0); break;  /* 0e12:47fc, render */
        case 0xA0: *id = 0; pending_msg = 0x0D; break;    /* "Go back to the main road!" */
        case 0xC0: *id = 0; pending_msg = 0x18; break;    /* "WRONG WAY!" */
        case 0xE0: *id = 0; sfx_play_ax(0x0D); break;
        }
    }
}
/* sprite distance (0e12:316c, render): dx=|4*x - 94A1|, dz=|4*z - 94A3|, major*65536/CS:10F6[atan idx>>4]
   + |sprite_y - eye|; id low byte 0 -> FFFFh. */

void object_face_hit(u8 type, face k)                    /* 0e12:4631 */
{
    idx = face index;
    for (i = obj_count - 1; i >= 1; i--) {
        if (!obj_face_count[i] /*B379*/ || idx < obj_face_first[i] /*B239*/ || idx >= first + count) continue;
        u8 m = lo(obj_flags[i]) | 1;                     /* NB: low byte (model + speed class) */
        if (type <= 0x19) m = (type == 0x19) ? 0x0B : 0x09;
        lo(obj_flags[i]) = m;  cell_key /*BD34*/ = 0xFFFF;
        if ((m & 0x3F) == 0x39 && type == 0x1B) { obj_flags[i] = 0x3039; obj_vel[i] = 0; obj_wp[i] = 0; sfx_play_ax(0x12); }
        else sfx_play_ax(5);
        shake = 5; bump_frames = 5; random_damage(); return;
    }
}
void barrier_knockdown(rec)                              /* 0e12:4590 (meaning likely) */
{
    sfx_play_ax(4); shake = 5; bump_frames = 3;
    R = (hi(rec.colour) == 6) ? rec : rec - 1;           /* 10-byte records */
    v = R.w0 & 7FFh; R.w0 &= E7FFh; A = Y[v] + 6; R.w3 = (R.w3 & 7FFh) | 1800h;   /* type 3: no collision */
    N = R + 1;
    v = N.w0 & 7FFh; N.w0 &= E7FFh; d = Y[v] - A; Y[v] = A;   Z[v] += d;
    v = N.w1 & 7FFh; d = Y[v]; A++; Y[v] = A; Z[v] += d - A;
    N.w3 = (N.w3 & 7FFh) | 1800h;
}   /* lasts until the visible cell list is rebuilt */
```

**Ground query** (inside the triangle path of `draw_face_dispatch`, `0e12:3a9e` → label `3d28`):
point-in-triangle by vertex bearings seen from the probe point (sort the three bearings; inside
iff all three circular gaps are in [1, 8000h]); quads that surround the point are split into
(w0,w1,w2),(w0,w2,w3). A port may use any exact point-in-triangle test with the same rule (a gap of 0
= outside). Then:

```c
u16 tri_height_at_viewpoint(b, s, d)                     /* 0e12:6316; PX, PZ = 94A1, 94A3 */
{
    if (Y[b] == Y[s] && Y[b] == Y[d]) h = Y[b];
    else {
        if (X[b] > PX) { swap(s,b); if (X[b] > PX) { swap(d,b); goto L2; } }   /* u16 compares */
        if (X[d] <= PX) swap(d,s);
    L2: if (X[s] <= PX) swap(d,b);
        bp = X[b]; t = PX - bp;
        Z1 = Z[b] + (X[s]!=bp ? (i16)((i32)(Z[s]-Z[b]) * t / (i16)(X[s]-bp)) : 0);
        Z2 = Z[b] + (X[d]!=bp ? (i16)((i32)(Z[d]-Z[b]) * t / (i16)(X[d]-bp)) : 0);
        H1 = Y[b] + (X[s]!=bp ? (i16)((i32)t * (Y[s]-Y[b]) / (i16)(X[s]-bp)) : 0);
        H2 = Y[b] + (X[d]!=bp ? (i16)((i32)t * (Y[d]-Y[b]) / (i16)(X[d]-bp)) : 0);
        h  = H1 + (Z2!=Z1 ? (i16)((i32)(PZ - Z1) * (H2-H1) / (i16)(Z2 - Z1)) : 0);
    }
    BCDF = h;  BACF = ((i16)h > (i16)eye_y /*949E*/);
    return h;
}
void ground_face_check(u16 h)                            /* 3d28-3da4; t = BA9A >> 3 = face type */
{
    if (t == 0) return;
    if (t < 0x0E) { if (t == 1) { if (abs16(car_speed) < 0x14) bounce_back(); else { crash_flag = 1; crash_type = 1; } } return; }
    if (t >= 0x14 && t < 0x1C) {
        if (t >= 0x18) return;
        if ((i16)(eye_y - car_eye_height - h + bridge_clearance /*95D3 = 20h*/) < 0) { overhead_y /*BA83*/ = h; return; }
    }
    BACF = 0;
    if (h > ground_y) {                                  /* u16: highest qualifying face wins */
        ground_y = h;  ground_tri = (b, s, d);           /* BA89/BA8B/BA8D */
        if ((i8)surface < 0x1C) surface = t;             /* 1Ch-1Fh stick for the frame; FFh = -1 */
    }
}
/* 0e12:373c resets every frame: BA9A = 70h, overhead_y = 0, ground_y = 0, surface = FFh.
   surface stays FFh (and ground_y 0) where no ground triangle contains the point (empty tile 67h,
   overhead-only). car_physics treats FFh like terrain 0Fh. */

void ground_slope_update(void)                           /* 0e12:6421, frame_draw */
{
    if (ground_y == 0) { ground_y = ground_y_prev /*BA87*/; return; }   /* keep last; slopes unchanged */
    ground_y_prev = ground_y;
    i16 ax = 0, cx = 0;
    if (!(Y[b]==Y[s] && Y[b]==Y[d])) {
        i16 sz = edge_slope(X, Z), sx = edge_slope(Z, X);
        ax = -(sx >> 9);  cx = sz >> 9;                  /* arithmetic */
        rot /*946A*/ = hi(view_heading); rotate_by_heading(&ax, &cx);   /* 0e12:6604 */
    }
    ground_roll = slope_to_tilt_code(lo(ax));            /* 0e12:6666 */
    cx -= pitch;
    ground_pitch = (cx == 0) ? 0 : clamp(cx, -0x18, 0x18);
}
/* edge_slope(P,Q): order b,s,d so P[b] < P[s] < P[d] (u16; ties -> vertex b); bp = P[d]-P[b], t = P[s]-P[b];
   my = Y[b] + t*(Y[d]-Y[b])/bp; mq = Q[b] + t*(Q[d]-Q[b])/bp (i32/i16); dy = Y[s]-my; if (!dy) return 0;
   dq = Q[s]-mq; if (!dq) return 7FFFh; if ((u16)|dy| < ((u16)|dq| >> 1)) return (i16)(((i32)dy << 16) / dq);
   return ((dy ^ dq) < 0) ? 8001h : 7FFFh.
   Sort as coded: if P[s] < P[b] swap(d,b); if P[s] > P[d] swap(d,s); if P[s] < P[b] swap(s,b).
   rotate_by_heading: q = 946A & C0h, i = 946A & 3Fh; if i: S = DS:B7CF[i], C = DS:B851[i] (Q15);
   a' = (a*C)>>15 + (c*S)>>15, c' = -((a*S)>>15) + (c*C)>>15; then q 40h: (c,-a), 80h: (-a,-c), C0h: (-c,a).
   slope_to_tilt_code(al): m = |al| & 7Fh; r = DS:BD01[m >> 2]; return al < 0 ? -r : r;
   DS:BD01 = 00 0C 0B 0A 09 09 08 08 08 07 07 07 07 06 06 06 06 06 05 05 05 05 05 05 04 04 04 04 04 04 04 04 */
```

**Route detection** (`0e12:70cd`, non-drawing part): the visible-cell list is rebuilt when the probe
cell or the heading octant (`BD34`/`BD36`) changes; **only in frames where neither changed**:
`t = tile of the current cell; if (t == route_tiles[0]) route_index = 0; else if (t == [1]) 1;
else if (t == [2]) 2;` (otherwise unchanged). The map wraps like a torus (`& 1FFh`, X & 7FFFh,
Z & 3FFFh); there is no off-map or wrong-way logic besides the sprite classes A0h/C0h.

### 4.10 Crash detection and sequence (verified)

```c
/* frame_draw 0e12:77d5, simulation part (after 323e and 6421) */
if (surface == 0x0E && (i16)(sprite_y[0] - car_eye_height) <= (i16)ground_y) crash_flag = 1;   /* water */
if (ext_view) { if (!frozen) cam_y = ground_y + cam_height; }
else {
    if (debug_keys) immunity = 2;                         /* dead */
    if (immunity) { if (surface == 0x0E || clock_running) immunity--; crash_flag = 0; }
}
if (crash_flag && !ext_view) {
    crash_water = 0; if (surface == 0x0E) { crash_water = 1; water_anim = 1; }
    crash_start();                                        /* 0e12:0f31 */
}
/* 46c0 (render) */
if (crashed) replay_start();                              /* 0e12:4c51 */

void crash_start(void)                                   /* 0e12:0f31 */
{
    if (crashed) return;
    crashed = 1;  music_stop(0);  shake = 0x10;  crash_object = 0;
    for (i = obj_a475 - 1; i > 0; i--)
        if (B379[i] && B239[i] <= crash_face && crash_face < B239[i] + B379[i]) { crash_object = 2*i; return; }
}
void replay_start(void)                                  /* 0e12:4c51; also F10 */
{
    if (ext_view) return;
    replay_state = 1;  RB(replay_pos) = 0xFFFF;  replay_pos = (replay_pos + 2) & 0x7FF;   /* sic: 7FFh */
    ext_view = 1;  cam_pitch = -16;  cam_roll = 0;  BA55 = 1;
    cam_height = car_eye_height + 0x26;  cam_dist = 0x30;  cam_yaw = hi(course) + 0x10;
}
```

Timeline: frame N crash → `crashed`, ext_view (everything stops). Next `race_run` iteration sees the
state change: BROKE.LZ (or WATER.LZ + animation for `crash_water`), 40 × 1 BIOS tick (≈ 2.2 s real
time) with shake steps, then `life_lost(0)` (lives message). Without F10: life lost, `replay_state`
= 0, `ext_view` = 0, `race_state` = 2 (3 = game over), `E561` = 1 → next frame `car_place` puts the
car on `road_mem[2]` with 50 frames of immunity. With F10 during the picture/message: the replay
plays (§4.11), then the 20-frame wreck animation, then `life_lost` (F10 again replays; F5 skips).

```c
void life_lost(int skip)                                 /* 0e12:044e */
{
    u8 n = lives /*09DE*/ - 1;  u16 msg;
    if (n == 0) msg = 0x25;                               /* GAME OVER */
    else if (n == 1) msg = 0x15;                          /* This is your last life... */
    else { msg = 0x16; /* "You have nn lives left": tens digit (or space) to DS:194B, units to 194C */ }
    if (panel_mode /*CC92*/ == 1) panel_erase(skip);
    ascii_key /*915B*/ = 0;  show_message(msg);            /* blocking; F10 there sets replay_request */
    if (!skip && replay_request) { if (panel_mode == 1) replay_panel_draw(); else { replay_request = 0; cam_manual = 0; } return; }
    crash_count /*0A75*/++;  replay_state = 0;  ext_view = 0;
    if (--lives == 0) race_state = 3; else { race_state = 2; need_place /*E561*/ = 1; }
}
```

### 4.11 Replay (`0ab4:1220`, verified)

Ring buffer of 800h words **inside the code segment** at `0ab4:020F` (port: a static `u16[0x800]`),
byte index `DS:16D4` (& FFFh), `16D6` = end of the last recorded frame; neither is ever reset
(start 0). `replay_clear` (`0ab4:120f`) zeroes it at every `car_place`. Values equal to FFFFh are
stored as FFFEh. Frame record: `FFFFh, sprite_x[0], sprite_z[0], sprite_y[0], view_heading, pitch`,
then for i = `A475−1` down to 1 with `(obj_flags[i] & 2000h) && B379[i]` (moving and drawn last
frame): `(AAB9[i] & FF00h) | i, A479, A5B9, A6F9, A839, A979, ABF9, AD39, AE79` (9 words).
Capacity 2048 / (6 + 9n) frames (341 with no traffic in view).

```c
#define RB(b) buf[(b) / 2]
void replay_update(void)                                 /* 0ab4:1220 */
{
    u16 b = replay_pos;
    if (replay_state == 0) {                              /* record (also while in chase view) */
        RB(b) = 0xFFFF; b = (b + 2) & 0xFFF;
        PUT(sprite_x[0]); PUT(sprite_z[0]); PUT(sprite_y[0]); PUT(view_heading); PUT(pitch);   /* PUT: FFFF->FFFE */
        replay_rec_objects(&b);                           /* 0ab4:1544 */
        replay_pos = replay_end = b; return;
    }
    clock_running = 0;
    if (replay_paused) return;
    if (replay_state >= 2) {                              /* wreck animation */
        if (--wreck_timer) {
            if (crash_water == 1) { obj_y8[0] = (sprite_y[0] - car_eye_height) << 3; return; }
            i16 dx = (i16)(vel_x >> 8), dz = (i16)(vel_z >> 8);   /* word at DS:1253 / 1257 */
            if (crash_object) { u8 m = obj_flags[o] & 0x3F; if (m < 0x12 || m > 0x14) { obj_x[o] += dx; obj_z[o] += dz; } }
            sprite_x[0] = obj_x[0] = sprite_x[0] - dx;  sprite_z[0] = obj_z[0] = sprite_z[0] - dz;
            return;
        }
        if (!replay_request) { life_lost(0); if (!replay_request) return; }
        goto restart;
    }
    replay_play_frame();                                  /* 0ab4:14b9 */
    if (replay_pos != replay_end) return;
    if (crashed) {                                        /* crash replay finished */
        replay_state = 2; wreck_timer = 0x14;
        if (crash_water == 1) { sprite_y[0] -= 0x10; obj_y8[0] = (sprite_y[0] - car_eye_height) << 3; return; }
        sprite_id[0] = 0x10B;                             /* smoke replaces the car */
        u16 x = sprite_x[0], z = sprite_z[0], y = (sprite_y[0] -= 0x10) + 0x0C;
        debris_sprites(2, &x, &z, &y);                    /* instances 1..3 */
        x += ((rand_lo >> 1) & 0x7F) - 0x40;  z += (*(u16*)&DS[0x00D3] & 0x7F) - 0x40;
        debris_sprites(8, &x, &z, &y);                    /* instances 4..6 */
        if (crash_object) { sprite_x[7] = obj_x[o]; sprite_z[7] = obj_z[o]; sprite_y[7] = (obj_y8[o] >> 3) + 0x1C; sprite_id[7] = 0x10B; }
        return;
    }
    if (replay_request) goto restart;
    ascii_key = 0; panel_erase(0); show_message(0x2B);   /* "Replay Completed" */
    if (replay_request) goto restart;
    replay_state = 0; ext_view = 0; race_state = 2; return;   /* resume driving, physics state untouched */
restart:
    replay_panel_draw();  replay_request = 0;  replay_state = 1;
    replay_pos = (replay_end + 2) & 0xFFF;
    for (i = 0; i < 8; i++) sprite_id[i] = 0;
}
void replay_play_frame(void)                             /* 0ab4:14b9 */
{
    b = replay_pos; do { v = RB(b); b = (b+2) & 0xFFF; } while (v != 0xFFFF);
    sprite_x[0] = obj_x[0] = next; sprite_z[0] = obj_z[0] = next;
    sprite_y[0] = next; obj_y8[0] = (sprite_y[0] - car_eye_height) << 3;
    view_heading = next; obj_heading[0] = (u8)((view_heading >> 8) - 0x40) * 0x101;
    pitch = next; obj_pitch[0] = (u16)pitch_shear(pitch) << 8;
    while (RB(b) != 0xFFFF) { w = next; i = w & 0xFF; AAB9[i] = w & 0xFF00;
                              A479[i], A5B9[i], A6F9[i], A839[i], A979[i], ABF9[i], AD39[i], AE79[i] = next ... }
    replay_pos = b;                                       /* every step & 0xFFF */
}
/* debris_sprites (0ab4:1460, BX = 2k, AX X, DX Z, CX Y in/out): instance k: id 8 (X-2, Z-4, Y+10h);
   k+1: id 7 (X+2, Z+3, Y-4); k+2: id 6 (X+8, Z, Y+4); returns X-2, Z-4, Y+10h. */
```

The chase camera during replay: `camera_pos_update` (`0e12:6e92`) offsets the camera by
`polar_small(cam_yaw, cam_dist)·2` from the car (render3d owns the camera maths), and in
`replay_state` 2 puts sprite 7 on the object hit.

### 4.12 Traffic, opponents and vehicle collisions (verified)

All moving objects (`obj_flags & 2000h`) are simulated every frame regardless of distance; only the
drawing is culled. Movement is frozen while `replay_state ≠ 0` or (`ext_view` or `!clock_running`)
except in the attract/showroom mode `09C4`.

Per-object runtime fields:

| Array | Meaning |
|---|---|
| A479 flags | bits 0–5 model; 6–7 speed class → `traffic_speeds[]` (`95AF` = 18h,1Eh,37h,78h); 8–11 behaviour; **1000h** halted; **2000h** moving; **4000h** racer/chasing police (ignores lights and stop signs, overtakes); **8000h** braking (half movement, lane timer every 2nd frame, brake lamps) |
| A5B9 / A6F9 / A839 | X (& 7FFFh), Z (& 3FFFh), height × 8 |
| A979 | hi current heading, lo target; slews 8/256 per frame |
| AAB9 | lo = s8 vy (A839 += vy·4); hi = pitch shear code |
| ABF9 | lo = s8 vx, hi = s8 vz per frame |
| AD39 | current cell 0..1FFh |
| AE79 | bits 6–15 frames to the next waypoint event (0 = parked until woken), bits 0–5 waypoint |
| AFB9/B0F9, B239/B379 | vertex / face ranges this frame; **B379 = 0 → not visible** (collisions, police, gate use it) |

Behaviour (bits 8–11): 0 not lane-driven; 1/9 shuttle (reverses every `95CC`·4 frames: seagulls —
world.md's "turnaround delay" reading of `95CC` is wrong); 2 forced branches only; 3 always branch A;
4 always B; 5 random A/none; 6 random B/none; ≥7 random none/A/B; 8 half speed, no skill scaling (train).
Traffic data: 27xxh (beh 7), 22xxh (beh 2); opponents 62C2h/63C3h; police 2791h (model 11h), 27B0h (30h).

```c
u16 vec_angle(i16 x, i16 z)                              /* 0e12:22fe; also CX = min, DX = max */
{
    u16 oct = 0, a = x, b = z;
    if (x < 0) { a = -x; oct = 6; }  if (z < 0) { b = -z; oct ^= 2; }
    u16 lo = a, hi = b;  if (!(lo < hi)) { swap(lo, hi); oct ^= 8; }
    u16 idx = (hi == lo) ? 0x1FF : (u16)(((u32)lo << 16) / hi) >> 7;
    const u16 *t = (const u16*)(CS_0e12 + 0x12F6 + T1[oct >> 1]);   /* T1 = DS:B79F {0,402h,0,402h,402h,0,402h,0} */
    return t[idx] + T2[oct >> 1];                          /* T2 = DS:B7BF {C000h,4000h,4000h,C000h,0,0,8000h,8000h} */
}   /* CS:12F6 u16[513] atan table (t[i] ~ floor(atan(i/512)*8000h/pi/20h)*20h, t[512] = 1FE0h), CS:16F8 = -t.
       Dump both from the exe. Result frame: 0 = +X, 4000h = -Z, C000h = +Z (object headings). */

Wp far *lane_list_for_cell(u16 cell)                     /* 0e12:5aa4 */
{
    u16 m = leg_map[cell];  place_rot /*946A*/ = hi(m);
    u8 far *p = P_BIN + ((u16 far*)P_BIN)[lo(m)];
    lane_hdr /*BCB8*/ = *p;  lane_count /*946D*/ = *p & 0x7F;  return (Wp far*)(p + 1);   /* {u16 flags; i16 h, x, z} */
}

void vehicle_follow_lane(int i)                          /* 0e12:5466 */
{
    u8 tgt = lo(A979[i]), cur = hi(A979[i]), d = tgt - cur;
    if (d) BCC3 = 1;
    d = (d + 4) & 0xF8;  if (!d) cur = tgt; else if ((i8)d < 0) cur -= 8; else cur += 8;
    A979[i] = cur << 8 | tgt;
    if (attract_mode /*09C4*/) return;
    u16 f = A479[i];
    if (!(f & 0x0F00)) { lo(AAB9[i]) = 0; return; }
    if (!(i <= 2 && opp_recover[i] /*BCAA+i*/)) if (f & 0x1000) return;
    if ((f & 0x8000) && ABF9[i] && (frame8 /*BCC1*/ & 1)) return;
    u16 t = AE79[i];
    if (!(t & 0xFFC0)) return;
    t -= 0x40; AE79[i] = t; if (t & 0xFFC0) return;
    /* waypoint (t & 3Fh) reached */
    u8 beh = (f >> 8) & 0x0F;
    if (beh == 9 || beh < 2) {                            /* shuttle */
        AE79[i] = (shuttle_period /*95CC*/ << 8) | lo(t);
        ABF9[i] = ((u8)-hi(ABF9[i]) << 8) | (u8)-lo(ABF9[i]);  lo(AAB9[i]) = -lo(AAB9[i]);  A979[i] ^= 0x8080; return;
    }
    u16 sp = traffic_speeds[rol8((u8)f, 2) & 3];          /* bits 6-7 */
    beh_lo3 /*946F*/ = beh; BCB7 = beh;
    if (beh == 8) sp >>= 1;
    else {
        beh_lo3 = beh & 7;
        if (race_computer_cars /*0B09*/ && i <= 2) {
            if (*(u8*)(DS + 0x9671 + AD39[i]) >= 0x74) sp += sp >> 2;   /* BUG: byte offset = cell, not 2*cell */
            if (opp_overtake[i] /*BCAC+i*/) sp += sp >> 2;
        }
        sp = (u16)(((skill_level >> 2) + 6) * sp) >> 4;
        if (rain_level) sp -= 2;  if (snow_level) sp -= 4;
    }
    sc_a /*9460*/ = sp;  branch_taken /*BCC2*/ = 0;
    u8 r = rand_hi_lo /*byte 00D4*/, m;
    if (beh < 3) m = 0; else if (beh == 3) m = 1; else if (beh == 4) m = 2;
    else if (beh == 5) m = r & 1; else if (beh == 6) m = (r & 1) << 1; else { m = r & 3; if (m == 3) m = 0; }
    branch_mode /*946E*/ = m;
    wp_idx /*946B*/ = lo(t); u16 cell = AD39[i];
    Wp far *w = lane_list_for_cell(cell) + wp_idx; u16 wf = w->flags;

    if ((wf & 0xC0) == 0x80) {                            /* lane exit by local edge wf & 3 */
        u16 key;
        if (uturn_obj /*BCB1*/ == 2*i) { uturn_obj = 0; key = (u8)wf ^ 0x40; goto search; }   /* police U-turn */
        exit_flags /*946C*/ = (u8)wf;
        u8 dir = (rol8(place_rot, 2) + (u8)wf) & 3;
        i16 dd = dir == 0 ? -32 : dir == 2 ? 32 : dir == 1 ? 1 : -1;
        cell = ((cell + dd) & 0x1E0) | ((cell + dd) & 0x1F);
        key = 0xC0 | (((dir ^ 2) - rol8(hi(leg_map[cell]), 2)) & 3);
    search:
        for (;;) { w = lane_list_for_cell(cell); u8 k = 0;
            do { if (key == w->flags) { wp_idx = k; goto target; } w++; k++; } while (--lane_count);   /* count 0 -> 256 loops */
            key = 0xC0 | (exit_flags & 3); cell = AD39[i]; }   /* dead end: U-turn in the same cell (can loop forever on bad data) */
    }
    if (wf & 0x80) wf = 0;                                /* entry waypoint */
    u16 stop = wf & 0x4040;
    if (stop) {
        i8 vx = lo(ABF9[i]), vz = hi(ABF9[i]); int halt = 0; u16 wait;
        if (stop == 0x0040) {                             /* stop sign */
            if (ABF9[i] && !(f & 0x4000)) { halt = 1; u8 a = (vx <= 0) + ((vz <= 0) << 1); wait = (((a << 3) - frame8) & 0x1F) + 0x10; }
        } else if (stop == 0x4040) {                      /* level crossing: nobody exempt */
            if (gate_pos >= 0x10 && ABF9[i]) { halt = 1; wait = 5; }
        } else {                                          /* 4000h traffic light */
            if (ABF9[i] && !(f & 0x4000)) {
                u8 axis = (vx == 0 || (vx > 0 && vz < 0) || (vx < 0 && vz > 0)) ? 2 : 0;
                if (((light_phase /*BCCF*/ & 2) ^ axis) == 0) { halt = 1; wait = light_timer /*BCCE*/ + ((light_phase & 1) ? 0 : 9) + 4; }
            }
        }
        if (halt) { A479[i] |= 0x8000; ABF9[i] = 0; lo(AAB9[i]) = 0; AE79[i] |= wait << 6; return; }
        A479[i] &= 0x7FFF;
    }
    if (wf & 0xBF3F) {                                    /* opponent route script (DS:9597) */
        u16 g = A479[i]; int k = (g & 0x3F) - 2;
        if (k == 0 || k == 1) {
            u8 s = opp_scripts[k][++script_pos[k] /*BCB9+k, pre-increment*/];
            u16 ng = g & 0xF8FF;
            if (!s) {                                     /* finished: store the time minus 20 s per player ticket, remove */
                u8 tk = clock_sub, sec = clock_sec, mn = clock_min;
                for (u8 n = tickets[0]; n; n--) { sec -= 20; if ((i8)sec < 0) { sec += 60; mn--; } }
                opp_time[k] = {tk, sec, mn} /*BCBB+3k*/;  ng = 0;
            }
            A479[i] = ng | (s << 8);
        }
    }
    u16 sel = wf; int take = 0;                           /* branch */
    if (branch_mode == 1 && (sel & 0x3F)) take = 1;
    else if (branch_mode == 2 && (sel & 0x3F00)) { sel = swapb(sel); take = 1; }
    else if (sel & 0x8000) { if (beh_lo3 == 6) sel = swapb(sel); if (!(sel & 0x3F)) sel = swapb(sel); take = 1; }
    if (take) { i8 o = ((i8)(sel << 2)) >> 2; branch_taken = 1; wp_idx += o; w += o; } else { wp_idx++; w++; }
target:
    AD39[i] = cell;  lo(AE79[i]) = wp_idx;
    sc_b /*9462*/ = (u16)(w->h + (leg_map[cell] & 0x3F00)) << 3;
    i16 x = w->x, z = w->z;
    switch (place_rot & 0xC0) { case 0x40: { i16 q = x; x = z; z = -q; } break; case 0x80: x = -x; z = -z; break;
                                case 0xC0: { i16 q = x; x = -z; z = q; } break; }
    i16 dX = x + ((cell & 0x1F) << 10) + 0x200 - A5B9[i];         if ((u16)abs16(dX) >= 0x4000) dX += 0x8000;
    i16 dZ = z + ((0x1E0 - (cell & 0x1E0)) << 5) + 0x200 - A6F9[i]; if ((u16)abs16(dZ) >= 0x2000) dZ += dZ < 0 ? 0x4000 : 0xC000;
    if (A479[i] & 0x4000) {                               /* overtake: aim 64 units to the left */
        u8 ovt = traffic_overtake /*BCAF*/, rec = 0; if (i <= 2) { ovt = opp_overtake[i]; rec = opp_recover[i]; }
        if (!rec && (ovt || (BCB7 & 8))) {
            A479[i] &= 0xF7FF;
            if (!(lane_hdr & 0x80)) {
                if (i > 2) traffic_overtake = 0x28; else opp_overtake[i] = 0x28;
                u16 s0 = sc_a, a0 = sc_b; polar(0x4000 - vec_angle(dX, dZ), 0x40);
                dX += cos_out; dZ += sin_out; sc_a = s0; sc_b = a0;
            }
        }
    }
    u16 ang = vec_angle(dX, dZ); lo(A979[i]) = hi(ang);
    u16 mn = min(|dX|,|dZ|), mx = max(|dX|,|dZ|);
    u16 D = (u16)(((u32)mn + 3u*mx) >> 1);
    u16 spd = sc_a; if (branch_taken == 1) spd >>= 1;
    u16 az = |dZ|, axx = |dX|;
    i8 vz = (u16)(((u32)(D > az ? (u16)(((u32)az << 16) / D) : 0xFFFF) * spd) >> 16);  if (dZ < 0) vz = -vz;
    i8 vx = (u16)(((u32)(D > axx ? (u16)(((u32)axx << 16) / D) : 0xFFFF) * spd) >> 16); if (dX < 0) vx = -vx;
    ABF9[i] = (u8)vz << 8 | (u8)vx;
    u16 big = axx; i8 vb = vx; if (axx < az) { big = az; vb = vz; }
    u16 n = vb ? big / (u16)abs8(vb) : 0; if (!n) n = 1;
    AE79[i] |= n << 6;                                    /* 10-bit counter, truncates */
    i16 vy = (i16)((i16)(sc_b - A839[i]) / (i16)n) >> 2; u8 vyb = (u8)vy;
    u16 res = vyb; u8 sum = (u8)(abs8(vx) + abs8(vz));
    if (sum) {                                            /* pitch shear code */
        u8 c = 3; u16 q16 = (u16)abs8(vyb) << 4;
        if ((q16 >> 8) < sum) { u8 q = q16 / sum;
            if (q < 0x18) { c = 4; if (q < 0x0C) { c = 5; if (q < 6) { c = 6; if (q < 3) { c = 7; if (q < 1) c = 0; } } } } }
        if ((i8)vyb >= 0) c = -c;
        res = (u8)c << 8 | vyb;
    }
    if (hi(AAB9[i]) != hi(res)) BCC3 = 1;
    AAB9[i] = res;
}

void traffic_update(void)                                /* 0e12:509b (movement part) */
{
    for (p in BCAB..BCAF) if (*p) --*p;                   /* opponent/traffic overtake timers (not BCAA) */
    for (int i = (attract_mode ? 1 : obj_a475) - 1; i >= 0; i--) {
        BCC3 = 1;
        if (ext_view == (u8)(2*i)) { BCC3 = 0; if (!i) continue; }   /* sic: compares 948B with 2i */
        if (!(A479[i] & 0x2000)) continue;
        BCC4 = BCC6 = BCC8 = 0;
        if (!replay_state && (attract_mode || (!ext_view && clock_running))) {
            vehicle_follow_lane(i);
            if (!(A479[i] & 0x2000)) continue;
            if (!attract_mode) {
                i16 dx = (i8)lo(ABF9[i]), dz = (i8)hi(ABF9[i]), dy = (i8)lo(AAB9[i]) << 2;
                u16 f = A479[i]; int mv = !(f & 0x1000);
                if (!mv && i >= 1 && i <= 2 && race_computer_cars) {       /* stuck opponent */
                    if (opp_recover[i]) mv = 1;
                    else { u8 c = opp_stuck[i] /*BCA8+i*/ + 1; if (c >= 0x1E) { opp_recover[i] = (i << 4) + 0x1E; c = 0; } opp_stuck[i] = c; }
                }
                if (mv) {
                    if (f & 0x8000) { dx >>= 1; dz >>= 1; dy >>= 1; }
                    BCC4 += dx; A5B9[i] = (A5B9[i] + dx) & 0x7FFF;
                    BCC6 += dz; A6F9[i] = (A6F9[i] + dz) & 0x3FFF;
                    A839[i] += dy; BCC8 += dy + (A839[i] & 7);
                }
            }
        }
        /* visibility window (view_dist B6DE / B6E0) and emission: render3d */
    }
}
```
`opp_recover[i]` (`BCAA+i`) counts down elsewhere in `509b` (not in the `BCAB..BCAF` loop).

```c
void vehicle_collisions(void)                            /* 0e12:5ad4 */
{
    for (i = obj_a475 - 1; i >= 1; i--) { u16 f = A479[i];            /* pass 1: recover */
        if (!(f & 0x2000) || !ABF9[i]) continue;
        A479[i] = ((f & 0x9000) == 0x9000) ? ((f & ~0x1000) | 0x0800) : (f & 0x6FFF); }
    for (i = obj_a475 - 1; i >= 1; i--) { u16 f = A479[i];            /* pass 2 */
        if (!(f & 0x2000)) continue; u8 m = f & 0x3F; if (m >= 0x12 && m <= 0x14) continue;   /* trains never the subject */
        for (j = i - 1; j >= 0; j--) {
            if (j && !B379[j]) continue;
            if ((u16)abs16(A5B9[j]-A5B9[i]) >= coll_window /*95D1 = FFh*/ || (u16)abs16(A6F9[j]-A6F9[i]) >= coll_window
                || (u16)abs16((A839[j]>>3)-(A839[i]>>3)) >= coll_window) continue;
            coll_bits /*946C*/ = 0;  W9464 = 2*j;
            for each collision box of model(i) {         /* objects.md: s16 A,B,C + u8 r, u8 h */
                BD38 = hi(AAB9[i]); place_rot = hi(A979[i]);
                (A, x, z) = vertex_rotate(box);          /* 0e12:7653, render */
                sc_b = box.r | box.h << 8;
                BCC4 = x + A5B9[i]; BCC6 = z + A6F9[i]; BCC8 = A + (A839[i] >> 3);
                vehicle_collision_boxes(j);
            }
            if (!coll_bits) continue;
            if (coll_bits & 1) { A479[i] |= 0x9000; if (j) A479[j] |= 0x9000; else crash_flag |= 1; continue; }
            u16 fl = (coll_bits & 2) ? 0x9000 : 0x8000;  /* near -> halt, close -> brake */
            u16 a = vec_angle(A5B9[j]-A5B9[i], A6F9[j]-A6F9[i]);
            if ((u8)(hi(vec_angle((i8)lo(ABF9[i]), (i8)hi(ABF9[i]))) - hi(a) + 8) < 0x10) A479[i] |= fl;
            else if ((u8)(hi(vec_angle((i8)lo(ABF9[j]), (i8)hi(ABF9[j]))) - hi(a) + 0x88) < 0x10 && j) A479[j] |= fl;
        }
    }
}
void vehicle_collision_boxes(int j)                      /* 0e12:5cf2; point BCC4/6/8, sc_b = r_i | h_i << 8 */
{
    BD38 = hi(AAB9[j]); place_rot = hi(A979[j]); if (!nk(model j)) return;
    for each box of j {
        (A, x, z) = vertex_rotate(box); x += A5B9[j]; z += A6F9[j]; A += A839[j] >> 3;
        u16 a = |x - BCC4|, c = |z - BCC6|; if (a > c) swap(a, c);
        u16 dist = (u16)(((u32)(u16)(a + c) + (u16)(2*c)) >> 1) >> 1;   /* (min+3max)/4 */
        if (dist >> 8) continue;
        u16 dy = |A - BCC8| >> 1; if (dy >> 8) continue;
        u16 rh = (box.r | box.h << 8) + sc_b;             /* 16-bit add: r carries into h */
        u8 al = dist, dl = dy;
        if (al <= lo(rh) && dl < hi(rh)) { coll_bits |= 7; continue; }
        rh <<= 3; if (al < lo(rh) && dl < hi(rh)) { coll_bits |= 6; continue; }
        u8 ch = hi(rh) * 2, cl2 = lo(rh) * 2;             /* 8-bit doubling */
        if ((cl2 overflowed || al < cl2) && dl < ch) coll_bits |= 4;
    }
}
```
A player/vehicle hit only sets `crash_flag`; the crash sequence does the rest. Traffic/traffic hits
halt both cars (9000h, re-set while in contact); pass 1 turns a halted car that still has a speed
into 0800h ("swerve" behaviour bit), which makes 4000h cars overtake at the next waypoint.

Traffic lights (`0e12:4d6d`, per frame): `frame8++; BCD0 = 0; if (!ext_view && --light_timer == 0)
{ light_phase = (light_phase + 1) & 3; light_timer = (light_phase & 1) ? 9 : 0x5A; } BCD1++;`
Phases 0/2 last 90 frames, 1/3 last 9; phases 0–1 let axis 2 (north–south) go.

Parked objects / lightning (`0e12:4fc7`): objects with `1000h` but not `2000h` (boats …) are only
drawn; objects with flags hi byte 1Fh are lightning: when `rain_level ≥ 6` and
`((((4i) & FFh) << 8) + rand_lo) & 5D9h) == 0`: sky flash `BAA6` = 2, sfx 9, bolt drawn that frame.

### 4.13 Police, radar detector, crossing gate (verified)

```c
void police_update(void)                                 /* 0e12:5ffe */
{
    if (ext_view) return;
    if (ticket_cooldown /*BCB5*/) ticket_cooldown--;
    radar_level /*B710*/ = 0;  u8 close = 0 /*946A*/, audible = 0 /*946B*/;
    for (i = obj_a475 - 1; i >= 1; i--) { u16 f = A479[i];
        if (!(f & 0x2000)) continue; u8 m = f & 0x3F; if (m != 0x11 && m != 0x30) continue;
        i16 lv;
        if (!B379[i]) {                                   /* not drawn: detector only */
            u16 d = |A5B9[i]-A5B9[0]| + |A6F9[i]-A6F9[0]|;
            if (d < 0x800) lv = 0x21; else if (d < 0xC00) lv = 0x11; else continue;
        } else {
            if (race_computer_cars && (f & 0x4000)) for (k = 1; k <= 2; k++)      /* opponent ticket */
                if (|A5B9[i]-A5B9[k]| + |A6F9[i]-A6F9[k]| < 0xD8 && (i8)(hi(A979[i]) - hi(A979[k]) + 0x40) >= 0) {
                    tickets[k]++; sky_flash /*BAA6*/ = 3; flash_msg /*BCB0*/ = 0x2C; ticket_cooldown = 0x96; police_end_chases(); return; }
            lv = 0x6F - (i16)(vertex_depth /*6382*/[AFB9[i]] >> 7); if (lv < 0) continue;
        }
        if (lv >= 0x20) {
            if (f & 0x4000) audible++;
            if (lv >= 0x50 && !ticket_cooldown) {
                int near = 1;
                if (!(f & 0x4000)) {
                    if (speedo_step < 0x0C) near = 0;        /* car_speed < 48 */
                    else { A479[i] = f | 0x40C0; clocked_speed /*BCA5*/ = speedo_step;   /* chase at speed class 3 */
                        u16 t = AE79[i];
                        if (!(t & 0xFFC0)) AE79[i] = t + 0x40;                          /* wake a parked cop */
                        else if ((i8)(hi((u16)(A979[i] - A979[0])) - 0x40) >= 0) { uturn_obj = 2*i; near = 0; }
                    }
                }
                if (near && lv >= 0x65) { close++;
                    if (++pullover_count /*BCB6*/ >= 0x1E) {                             /* 30 frames */
                        sky_flash = 3; flash_msg = 0x21; ticket_cooldown = 0x96; police_end_chases();
                        tickets[0]++; u8 s = clock_sec + 20; if (s >= 60) { clock_min++; s -= 60; } clock_sec = s; return; } }
            }
        }
        if (radar_level < lv) radar_level = lv;
    }
    if (!close) pullover_count = 0;
    sfx_play_ax(audible ? 0x14 : 0x94);                   /* siren on/off (sound spec: 14h -> 17h when 9596) */
}
void police_end_chases(void)                             /* 0e12:61d2 */
{   pullover_count = 0; for (i = obj_a475 - 1; i >= 1; i--) { u8 m = A479[i] & 0x3F; if (m == 0x11 || m == 0x30) A479[i] &= 0xBFBF; } }
```
The ticket message (21h "You just got a ticket, :20 penalty" / 2Ch "Your opponent got a ticket") is
posted when the 3-frame sky flash `BAA6` expires (`0e12:373c` copies `BCB0` to `pending_msg`). The
penalty is **20 clock seconds** added directly to the race clock; `tickets[k]` also adds 20 s to an
opponent's time. Message 2Dh ("I clocked you at over 000 mph", speed = `clocked_speed·7 − (rand & 7)`,
`0000:1968`) is formatted by game_flow; where it is posted was not found.

```c
void radar_detector(void)                                /* 0e12:0db4 (drawing part: hud) */
{
    u8 t = ++radar_phase /*B711*/, n = 0xFF;
    if (!(t & 8)) { n = radar_level >> 4; if (!(t & 7) && n) sfx_play_ax(0x0E); }   /* blink 8/8, beep every 16 frames */
    if (n == radar_drawn /*B70F*/) return; radar_drawn = n;
    /* n == FFh: clear the bars; else LED colour CEAE + n bars colour CEAF (hud) */
}

void crossing_gate_update(void)                          /* 0e12:5e70 */
{
    if (gate_obj /*BCD4*/ == 0xFFFF) { train_near = 0; gate_pos = 0; train_min_d = 0xFFFF; return; }
    train_near /*BCD9*/ = 0;
    for (i = obj_a475 - 1; i >= 1; i--) { if (!(A479[i] & 0x2000) || !B379[i]) continue;
        u8 m = A479[i] & 0x3F; if (m < 0x12 || m > 0x14) continue;
        u16 d = |((A5B9[g] + 0x100) & 0xFE00) - A5B9[i]| + |((A6F9[g] + 0x100) & 0xFE00) - A6F9[i]|;
        if (d >= 0x240) continue; if (train_min_d >= d) train_min_d = d; train_near = 1; break; }
    u8 p = gate_pos; if (p) sfx_play_ax(7);               /* bell */
    if (train_near) { if (p < 0x40) { if (p < 0x3C && train_min_d <= 0x60) p += 3; p++; if (p == 0x3E) sfx_play_ax(8); } }
    else if (p) { p--; p &= 0x3E; }
    gate_pos = p;  /* then arm vertices (render) for BCD4 and BCD6 */
}
```
Vehicles wait at 4040h waypoints while `gate_pos ≥ 10h`; the crossing-barrier sprite (id 10h, class 60h)
only crashes the player while `gate_pos ≥ 30h`. The gate only runs while a gate object is in the
drawn cell set. Trains are never the subject of `vehicle_collisions`, so they cannot hit the player
there (only through their faces in `34f1`, if they are type 2).

### 4.14 Resets (verified)

```c
void leg_state_reset(void)                               /* 0e12:255e, from stage_load */
{
    do r = random(); while (popcount16(r) < 10 || popcount16(r) > 12);
    veg_mask = rand_seed_ba /*B6ED*/ = r;
    row_tables();                                         /* 0e12:25f4, render */
    life_reset();
    route_index = 0; tickets[0..2] = 0; BCA9..BCAF = 0; clock_min = clock_sec = 0; headlights = 0; wipers = 0;
    BB34 = BCB5 = BCB9 = BCBA = BCBD = BCC0 = 0;  BCB1 = BCBB = BCBE = 0;  rain_level = snow_level = wind_level = 0;
    cam_x /*949A*/ = sprite_x[0]; cam_z /*949C*/ = sprite_z[0];
}
void life_reset(void)                                    /* 0e12:2465, from 255e and car_place */
{
    if (return_to_road || crashed) {                      /* respawn at road_mem[2] */
        sprite_x[0] = obj_x[0] = B9CB; sprite_z[0] = obj_z[0] = B9CD;
        sprite_y[0] = B9CF; obj_y8[0] = (B9CF - car_eye_height) << 3;
        view_heading = B9D1; obj_heading[0] = (u8)((B9D1 >> 8) - 0x40) * 0x101;
    }
    build_colour_remap();  detail_apply();
    damage = 0; overrev_events = return_to_road = replay_paused = replay_request = steer_hold = 0;
    sky_flash = gate_pos = train_near = view_roll = ground_roll = clock_frames = prev_bits = throttle_released = throttle = 0;
    pitch_rate = ground_pitch = 0; sprite_list_count /*94AE*/ = 0;
    replay_state = crash_flag = crashed = ext_view = shake = clock_running = BC7C = 0;
    clock_drawn /*B709*/ = 0xFF; radar_drawn = 0xFF; light_timer = 1; gear = 1 /*N*/; immunity = 0x32;
    BB35[0..3Fh] = 0; sprite_id[0..8] = 0; police_end_chases();
}
void car_place(void)                                     /* 0792:083c (game_flow; start and after a life) */
{
    B6D1 = 1; on_ground = 0; odo_acc /*1186*/ = 0;
    replay_clear(); shake = 1; screen_shake_step();
    if (!attract_mode && lives) music_for_state(radio_station);
    life_reset();
    engine_rpm = rpm_prev = 0; vel_y = 0; speed_long = 0; pitch = 0; need_place = 0;
    offroad_frames = 0; rpm_inertia = 0; steer_wheel = 0x10;
    u16 h = (u16)((u8)(obj_heading[0] + 0x40)) << 8;  view_heading = course = body_heading = h;
    pos_x = (u32)(u16)sprite_x[0] << 7;  pos_z = (u32)(u16)sprite_z[0] << 7;
    ground_y = sprite_y[0] - car_eye_height;  pos_y = (i32)(i16)ground_y << 7;
    compass_drawn = 0xFF; mouse_set(0xA0, 0x64);
}   /* not reset: position history, vel_x/vel_z, wind_speed, replay indices */
```

### 4.15 race_run: clock, zones, finish, pacing (simulation parts; game_flow owns the loop)

```c
void race_clock_hud(void)                                /* 0e12:0b1d, per frame unless attract mode */
{
    if (race_state != 1 || ext_view) { clock_frames = 0; return; }
    if (clock_running) {
        u8 f = ++clock_frames;  clock_sub = (u8)(f + (f >> 1)) >> 1;    /* 0..3, stored as "hundredths" */
        if (f < 5) goto draw;
        clock_sec++; clock_sub = 0;                        /* 5 frames = 1 second */
    }
    clock_frames = 0;
    if (clock_sec >= 60) { clock_sec = 0; if (++clock_min >= 60) clock_min--; }   /* minutes saturate at 59 */
draw:
    clock_draw(); compass_draw(); radar_detector();
}
```
`show_message` (`0000:179c`) sets `clock_frames = (u8)ticks` on exit (a quirk: usually one extra
clock second after a message). `B707` (`clock_running`) is 0 from `life_reset` until the first gear
shift, so the clock and the traffic wait for the player.

```c
/* in race_run, after race_input(2) */
random();                                                 /* 0000:0f58 */
if (surface != last_surface /*CC50*/) {
    if (!ext_view) switch (surface) {
    case 0x1C: if (wind_level == 0) zone_dir = 1; if (wind_level == 4) zone_dir = 0;
               wind_level += (zone_dir == 1) ? 2 : -2; break;                              /* 0 -> 2 -> 4 -> 2 -> 0 */
    case 0x1D: if (rain_level == 0) zone_dir = 1; if (rain_level == 8) zone_dir = 0;
               if (zone_dir == 1) { rain_level += 4; sky_alt = 1; } else if ((rain_level -= 4) == 0) sky_alt = 0;
               build_colour_remap(); break;
    case 0x1E: rain_level = 0; if (snow_level == 0) zone_dir = 1; if (snow_level == 8) zone_dir = 0;
               if (zone_dir == 1) { snow_level += 4; sky_alt = 1; } else if ((snow_level -= 4) == 0) sky_alt = 0;
               build_colour_remap(); break;
    case 0x1F: race_state = 3; break;                     /* LEG FINISHED: car on the gas-station face */
    }
    last_surface = surface;
}
if (!protection_ok && clock_min == 2) { show_message(0x1F); race_state = 3; }   /* "Your time is up": copy-protection only, off here */
while ((u16)(ticks - frame_start) < 5) random();           /* pacing; RNG advanced in the idle loop */
```
(`zone_dir` = `DS:E540`; byte writes to `BC75/77/79`.) Zone faces 1Dh/1Eh do not occur in the shipped
legs; 1Ch occurs in SCENE02 (tiles 75h, 61h).

## 5. Data usage

* Car `.LST` physics block `DS:1200–1244` and gearbox `DS:E564..` (descriptions.md), gauge clamp `CC76`,
  impact threshold `94BD`, eye height `94BF`, near-hit limits `94C1`/`94C3`.
* Leg file (`DS:9592..`, world.md): `9594` gravity/scale (600), `9597` opponent scripts, `95AF` traffic
  speeds, `95C9` route tiles, `95CC` shuttle period, `95CF` sprite hit distance, `95D1` vehicle collision
  window, `95D3` bridge clearance, `95D7` wind direction (word), map `9671`, object arrays `A473..`.
* `P.BIN` lanes (objects.md) through `lane_list_for_cell`; collision boxes of vehicle models.
* Tables to dump from the exe: CS `0e12:0FF2` sine (129 u16), `0e12:12F6`/`16F8` atan (2×513 u16),
  `0e12:0F81` shake pairs (32 bytes), `0e12:0000` key table (64 words); DS `B6EF` F-key table, `BCE1`
  pitch shear (32 bytes), `BD01` tilt codes (32 bytes), `B79F`/`B7BF` octant tables, `B7CF`/`B851` Q15 sin/cos
  (render). The ring buffer `0ab4:020F` is runtime storage only.

## 6. Hardware / DOS dependencies

| Original | Use | SDL3 port |
|---|---|---|
| INT 9 ISR `0c1c:0e98` → `915C` bits, `915B` ASCII, `E08C` codes | controls, keys | SDL key events → the same bit byte and code queue (platform spec) |
| joystick `0000:1e44` (port 201h) | `947B`, `00C2` | SDL gamepad → bits / analog x with the same calibration maths |
| mouse `16f1:000d`, `16ef:0009` (INT 33h) | F7 mouse steering | SDL relative mouse mapped to 0..319 / 0..199 |
| PIT ticks `DS:00A0` (145.65 Hz) | pacing, `B70E` | a 145.65 Hz tick counter from `SDL_GetTicksNS` |
| `1776:0008` display offset (CRTC) | screen shake | shift the presented image by (dx, dy) |
| BIOS tick waits (crash picture) | 40 × 1/18.2 s | real-time wait |

## 7. Timing

Everything in this spec runs **once per frame**; the frame is paced to **≥ 5 PIT ticks** (145.65 Hz),
i.e. at most 29.13 fps, longer when drawing is slow. Frame-counted quantities:

* car physics (speeds are units per frame, gravity 900/frame), steering ramp, throttle (±2 per poll, 3
  polls per frame), engine rpm, auto-shift;
* race clock: **5 frames = 1 clock second**; wreck animation 20 frames; immunity 50 frames; off-road
  hint 64 frames; screen shake ≤ 16 frames; radar blink 8/beep 16 frames; wheel centring every 2nd frame;
* traffic: heading slew 8/256 per frame, lights 90/9 frames, stop signs 16–47, crossing 5, overtaking
  40, opponent stuck 30 / recovery 46–62, pull-over 30, ticket cooldown 150, shuttle 20;
* real time only: the BROKE/WATER picture delay (BIOS ticks) and message waits.

**Frame-rate dependence.** Only the steering rate is scaled by the measured frame length
(`(B70E + 17)`, B70E ≥ 5), plus an extra joystick poll when a frame takes > 40 ticks. At the 5-tick
cap one clock second lasts 25 ticks = 0.17 s of real time, so the clock and the whole simulation
run faster than real time on a fast machine. The calibrations point to a design rate of about
5 fps: 1 clock second = 5 frames; the odometer (`0792:1952`, 50000h `|speed_long|` units per 1/60
mile) together with `car_speed`·1.64 mph gives ≈ 4.4 frames per game second. At ≈ 5 fps a frame is
≈ 29 ticks, which is what a 1990 PC would have drawn.

**Recommendation for the port.** Keep the fixed-step, one-update-per-frame structure and pace frames
with the original rule "wait until ≥ N ticks since the frame start", with `B70E` set to the number of
ticks that actually elapsed (capped at 255). N = 5 reproduces the original on an unlimited machine
(DOSBox at max cycles; everything 5.8× faster than intended). Offer N as an option with a default of
**29 ticks (≈ 5 fps game rate, clock ≈ real time)**, and interpolate the drawing between simulation
frames if smoothness is wanted (TD3 Enhanced). Do not scale the per-frame constants: that would change
the behaviour. Running the simulation at a different step count per frame is not faithful because
the steering, the throttle polls and all timers are per frame.

The RNG (`0000:0f58`) is also advanced inside the pacing idle loop, so random sequences depend on the
host speed; a port that paces by sleeping should call `random()` once per elapsed tick to stay close.

## 8. Differences from TD1/TD2

Not needed (new engine). Note only that TD2 ran its simulation at 10 Hz from the timer interrupt;
TD3 runs it once per rendered frame.

## 9. Open questions

1. **Pacing** (§7): which default tick count the port should use (29 ≈ design rate vs 5 = original cap).
2. **Deliberate-looking bugs to replicate** (all verified in the disassembly):
   `(u16)(rpm·rpm)/30` in the drive force (16-bit square: the torque curve is a sawtooth over
   rpm 256, 362, 443 …); `grip_yaw_limit` overflows for the CERV (`111·600` → 1064) so its yaw grip
   is ~50× lower than the other cars at the slide threshold; the pitch clamp that never clamps;
   `replay_start`'s `& 7FFh`; the opponent highway boost reading `9671 + cell` instead of `+ 2·cell`.
   Keep them (faithful) — confirm with play-testing in DOSBox.
3. `DS:B6D2` (R), `B6D8` (H), `BC7B` (W), `B6DB` (D): names from their readers (mirror, headlights,
   wipers, lever knob) are likely, not verified visually.
4. Class-40h sprites keep their collision class after a hit, so the hit repeats every frame while in
   range (sound, shake, damage roll). Check in DOSBox.
5. Where message 2Dh ("I clocked you at …") is posted; `BCA5` is written by the police code.
6. `DS:9486` bit 10h (brakes ×1) is tested but never set.
7. Non-crash replays cannot be left early (F5 is ignored while `crashed` = 0); intended?
8. `vehicle_follow_lane`'s lane search can loop 256 times or forever on bad data (not in shipped legs).
9. Exact physical meaning of the pitch shear codes and camera fields belongs to render3d.
