#pragma once
/* Render3d module (render3d.md): segment 0e12 world build, camera/projection, faces, sprites, mirror,
 * sky, view set-up and the frame_update / frame_draw roots. Functions other modules call.
 *
 * Register-argument routines take their registers as parameters named after them (arg_REG) and return
 * register results through the return value / host out-pointers (PORTING.md "Porting a function").
 * Object slot arguments in BX/SI are byte offsets into the word arrays (2 * slot), as in the original. */
#include "mem.h"

/* 0e12:03d4 detail_apply — render3d.md §4.14 (near; view window DS:B6DE by detail, sprite seed mask, traffic thinning by skill) */
void detail_apply(void);
/* 0e12:0fa1 screen_shake_step — render3d.md §4.12 (if DS:947C: DS:947C--, display offset from 0e12:0F81 unless DS:E776 == 13h) */
void screen_shake_step(void);
/* 0e12:22fe atan2_xz — render3d.md §4.14 (near; CX = x, DX = z -> AX = atan2(-z, x), 10000h per turn.
 * Also leaves CX = min(|x|,|z|) and DX = max(|x|,|z|), which vehicle_follow_lane 0e12:5466 uses;
 * cx_min / dx_max may be NULL) */
u16 atan2_xz(s16 x_cx, s16 z_dx, u16 *cx_min, u16 *dx_max);
/* 0e12:234b polar — render3d.md §4.14 (far cdecl; DS:9460 = len*sin(angle), DS:9462 = len*cos(angle), table 0e12:0FF2) */
void polar(u16 angle, u16 len);
/* 0e12:2537 frame_buffers_init — render3d.md §4.2 (DS:90D0 = normalised seg of far DS:CC5C, DS:90D2 = seg of DS:E7DE + 1) */
void frame_buffers_init(void);
/* 0e12:255e leg_state_reset — render3d.md §4.14 (seed -> DS:BA56/B6ED, 25f4, life_reset, zero leg state, car pos = sprite 0) */
void leg_state_reset(void);
/* 0e12:261c build_colour_remap — render3d.md §4.14 (32-byte colour-code remap at far DS:E5B0: day/night, rain, snow) */
void build_colour_remap(void);
/* 0e12:2789 sprites_prescale — render3d.md §4.13 (pre-scaled sprite cache in far DS:E53C from DS:2500; message 1Ah on overflow) */
void sprites_prescale(void);
/* 0e12:409c view_setup — render3d.md §4.2 (view size from DS:09C4/B6DC; the 24 patched clip immediates become mem[] variables) */
void view_setup(void);
/* 0e12:4590 plate_fold_down — render3d.md §4.14 (near; ES:BP = &face.w3 in the face buffer: fold a type-1Ah pair flat, sfx 4) */
void plate_fold_down(FarPtr w3_es_bp);
/* 0e12:47fc windscreen_splat — render3d.md §4.12 (near; AX = random word, DL = type 0 / C0h: drop into the 64-slot ring) */
void windscreen_splat(u16 rand_ax, u8 type_dl);
/* 0e12:4fc7 parked_vehicles_emit — render3d.md §4.14 (near; objects with flags 1000h set / 2000h clear, lightning) */
void parked_vehicles_emit(void);
/* 0e12:52a3 obj_emit_vehicle_moving — render3d.md §4.14 (near; BX = 2*slot, DX = distance -> CX = nf, ES:SI = the
 * model's face array for model_emit_faces; in the dead cache path CX = nf:nv and SI is not at the faces) */
u16 obj_emit_vehicle_moving(u16 slot2_bx, u16 dist_dx, FarPtr *faces_es_si);
/* 0e12:5df9 lighthouse_rotate — render3d.md §4.14 (near; beam vertices of the lighthouse's first face on a circle) */
void lighthouse_rotate(void);
/* 0e12:5f57 crossing_gate_arm_place — render3d.md §4.14 (near; SI = 2*gate slot: arm tip vertices from DS:BCD8, length DS:95D6) */
void crossing_gate_arm_place(u16 slot2_si);
/* 0e12:7487 model_emit_faces — render3d.md §4.4 (near; ES:SI = first face, CX = nf, DS:945E = vertex base: append
 * nf - DS:BD24 face records; returns SI after the faces read (unchanged on overflow), ES unchanged) */
u16 model_emit_faces(FarPtr faces_es_si, u16 nf_cx);
/* 0e12:7653 vertex_rotate — render3d.md §4.4 (near; ES:SI = &A[i], BX = byte stride between A, B, C (2*nv; 2 for
 * collision boxes), DS:BD38 shear, DS:946A rotation -> DX = A', AX = B', CX = C') */
void vertex_rotate(FarPtr a_es_si, u16 stride_bx, s16 *a_dx, s16 *b_ax, s16 *c_cx);
/* 0e12:76ec frame_update — render3d.md §4.3 (per frame: shake, simulation, camera, world build, sky/ground into V) */
void frame_update(void);
/* 0e12:77d5 frame_draw — render3d.md §4.12 (page 1; faces + sprites into V/M, overlays, mirror frame; page 0) */
void frame_draw(void);
/* 0e12:7c21 mirror_frame_draw — render3d.md §4.12 (mirror M grey when invalid/off, M border, lower rim into V) */
void mirror_frame_draw(void);
