#pragma once
/* Simulation module (simulation.md): car physics 0977:0008, replay 0ab4:120f/1220, the 0e12 driving,
 * controls, key handlers, traffic, police, collisions. Functions other modules call. */
#include "mem.h"

/* Registers the key handlers stored as near code pointers: the table 0e12:0000 (64 words, codes 40h-7Fh)
 * and DS:B6EF (10 words, F1-F10), all in segment 0e12 (simulation.md §4.7). key_dispatch calls them
 * through codeptr_lookup_near(0x0E12, off). Called by modules_init() (game/game.h). */
void sim_register_codeptrs(void);

/* 0977:0008 car_physics — simulation.md §4.2 (per frame from frame_update unless DS:948B) */
void car_physics(void);
/* 0ab4:120f replay_clear — simulation.md §4.11 (zero the ring buffer 0ab4:020F, 800h words) */
void replay_clear(void);
/* 0ab4:1220 replay_update — simulation.md §4.11 (per frame: record (0), play back (1), wreck animation (2)) */
void replay_update(void);
/* 0e12:0080 controls_poll_far — simulation.md §4.4 (call 0e12:0751; retf) */
void controls_poll_far(void);
/* 0e12:0084 key_dispatch — simulation.md §4.7 (DS:E08C: 40h-7Fh via 0e12:0000, 81h-8Ah via DS:B6EF when race_state < 2) */
void key_dispatch(void);
/* 0e12:044e life_lost — simulation.md §4.10 (lives message, F10 replay request, next life or game over; skip = 1 from F5) */
void life_lost(s16 skip);
/* 0e12:095a auto_shift — simulation.md §4.5 (near; automatic box and CERV in-range shifts) */
void auto_shift(void);
/* 0e12:0f31 crash_start — simulation.md §4.10 (near; latch DS:B78D, music stop, shake 10h, crash object) */
void crash_start(void);
/* 0e12:0fd6 bounce_back — simulation.md §4.3 (near; position of 3 frames ago DS:1282 -> DS:125E, shake 4) */
void bounce_back(void);
/* 0e12:2465 life_reset — simulation.md §4.14 (per-life reset, respawn, colour remap, detail_apply) */
void life_reset(void);
/* 0e12:33f7 sprite_collisions — simulation.md §4.9 (near; sprite collision classes) */
void sprite_collisions(void);
/* 0e12:34f1 near_face_hits — simulation.md §4.9 (near; faces near the car: solid, vehicle, signs, barriers) */
void near_face_hits(void);
/* 0e12:4c51 replay_start — simulation.md §4.10 (near; replay / crash camera, DS:948B = 1, DS:94B2 = 1, end marker) */
void replay_start(void);
/* 0e12:4d60 objects_update — simulation.md §4.12, render3d.md §4.14 (near; 4fc7, 509b, 5ad4, 5df9) */
void objects_update(void);
/* 0e12:5e70 crossing_gate_update — simulation.md §4.13 (near; train near the gate -> DS:BCD8, bells) */
void crossing_gate_update(void);
/* 0e12:5ffe police_update — simulation.md §4.13 (near; radar level, chase, pull-over, ticket, siren) */
void police_update(void);
/* 0e12:61fd opponent_times_finalize — render3d.md §4.14 (opponent times at leg end -> results records DS:E80F/E82A) */
void opponent_times_finalize(void);
/* 0e12:6e92 camera_pos_update — simulation.md §4.1/§4.11, render3d.md §4.12 (near; camera X/Z, probe point DS:94A1/94A3, sun sprite) */
void camera_pos_update(void);

/* ---- Simulation-internal functions shared between the sim*.c files (not called by other modules) ---- */
/* 0e12:0e74 overrev_damage — simulation.md §4.8 (far; every 4th over-rev event: engine or gear damage) */
void overrev_damage(void);
/* 0e12:0ec5 landing_damage — simulation.md §4.8 (far; 1/32 gear damage, else falls into random_damage) */
void landing_damage(void);
/* 0e12:0edb random_damage — simulation.md §4.8 (far; suspension/brakes 1/8, alignment 1/64) */
void random_damage(void);
/* 0e12:23b1 grip_yaw_limit — simulation.md §4.3 (far; max course change per frame) */
u16 grip_yaw_limit(u16 g);
/* 0e12:6686 pitch_shear — simulation.md §4.3 (far; pitch -> model shear code, AL) */
u8 pitch_shear(u16 p);
/* 0e12:61d2 police_pursuit_clear — simulation.md §4.13 (near; pull-over count 0, clear 4040h of all police) */
void police_pursuit_clear(void);
/* 0e12:04f1 panel_erase — simulation.md §4.10 (far; DX: replay panel erase before a message) */
void panel_erase(u16 dx);
/* 0e12:01c5 gear_lever_target — simulation.md §4.5 (far; DS:E5B6 = 0, DS:CC57 = DS:E565[gear]) */
void gear_lever_target(void);
