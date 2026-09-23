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

/* ======================================================================================================
 * render3d module-internal interface (added by the render3d module; only the render*.c files define
 * RENDER3D_INTERNAL before including game.h). Register routines take byte offsets where the original
 * passes byte offsets: vertex arguments are 2*v (index into the DS:2502.. word arrays), as in the asm.
 * ====================================================================================================== */
#ifdef RENDER3D_INTERNAL

/* Segment 0e12 tables / patched immediates: read in place from mem[] (SEGW(0x0E12, off)). */
#define R3_CS 0x0E12
static inline u16  r3_cs16(u16 off) { return SEGW(R3_CS, off); }
static inline u8   r3_cs8(u16 off)  { return SEGB(R3_CS, off); }
static inline bool r3_neg(u16 v)    { return (s16)v < 0; }          /* NEG(): sign of a 16-bit result */

/* Vertex / projection arrays by BYTE offset bo = 2*v (DS:2502 ... DS:7C82, 640h words each). */
#define R3_VY(bo)    DSS(DS_vert_y      + (u16)(bo))
#define R3_VX(bo)    DSS(DS_vert_x      + (u16)(bo))
#define R3_VZ(bo)    DSS(DS_vert_z      + (u16)(bo))
#define R3_SX(bo)    DSW(DS_scr_x       + (u16)(bo))
#define R3_SY(bo)    DSS(DS_scr_y       + (u16)(bo))
#define R3_DEPTH(bo) DSW(DS_vert_depth  + (u16)(bo))
#define R3_DIST(bo)  DSW(DS_vert_dist   + (u16)(bo))
#define R3_MY(bo)    DSS(DS_mir_y       + (u16)(bo))

/* Far face buffer segment (DS:E5BA) and word access to it. */
static inline u16  r3_face_seg(void)             { return DSW(DS_face_block + 2); }
static inline u16  r3_f16(u16 off)               { return rd16(r3_face_seg(), off); }
static inline void r3_f16w(u16 off, u16 v)       { wr16(r3_face_seg(), off, v); }

/* ---- render.c: frame roots, setup, helpers ---- */
/* 0e12:25f4 view_row_tables_init — render3d.md §4.2 */
void view_row_tables_init(void);
/* 0e12:34c3 scene_prepare — render3d.md §4.14 */
void scene_prepare(void);
/* 0e12:6316 plane_height_at_camera — render3d.md §4.9 (BX,SI,DI vertex byte offsets -> AX; BCDF, BACF) */
u16 plane_height_at_camera(u16 b_bx, u16 s_si, u16 d_di);
/* 0e12:6421 ground_slope_update — render3d.md §4.12 */
void ground_slope_update(void);
/* 0e12:6604 rotate_by_heading — render3d.md §4.12 (AX, CX in/out; angle byte DS:946A) */
void rotate_by_heading(s16 *ax, s16 *cx);
/* 0e12:6666 slope_to_roll_code — render3d.md §4.12 (AL -> AL) */
u8 slope_to_roll_code(u8 al);

/* ---- render_world.c: world build, projection, face sort ---- */
/* 0e12:70b8 obj_ranges_clear */
void obj_ranges_clear(void);
/* 0e12:70cd world_build_visible — render3d.md §4.4 */
void world_build_visible(void);
/* 0e12:72f8 model_place — render3d.md §4.4 (AL = model, AH = 0 tile / 1 O object) */
void model_place(u8 model_al, u8 kind_ah);
/* 0e12:75c1 model_emit_vertices — render3d.md §4.4 (ES:SI = &A[0], CX = nv, DS:946B = nv; returns SI past C[],
 * unchanged on overflow (DS:BD22 = 1)) */
u16 model_emit_vertices(FarPtr a_es_si, u16 nv_cx);
/* 0e12:394c vertices_project — render3d.md §4.5 */
void vertices_project(void);
/* 0e12:39fd vertex_project_y — render3d.md §4.5 (BX = 2v) */
void vertex_project_y(u16 v_bx);
/* 0e12:35f9 face_order_reset / 361c faces_sort_keys / 7a18 / 7a35 / 7b06 — render3d.md §4.6
 * (lo/hi are byte offsets of key entries in the face segment) */
void face_order_reset(void);
void faces_sort_keys(void);
void faces_quicksort_all(void);
void faces_quicksort(u16 lo, u16 hi);
void faces_bubble_sort(u16 lo, u16 hi);

/* ---- render_raster.c: sky/ground, face drawing and the span engine (view V) ---- */
/* 0e12:373c sky_ground_draw — render3d.md §4.7 */
void sky_ground_draw(void);
/* 0e12:3a7c draw_face_dispatch — render3d.md §4.9 (BX = 2*v0, SI = 2*v1, DH = w0_hi & C0h, ES:BP = &face.w1) */
void draw_face_dispatch(u16 v0_bx, u16 v1_si, u16 w0_dx, FarPtr w1_es_bp);
/* 0e12:3a9e draw_face_polygon — render3d.md §4.9 (triangle BX, SI, DI) */
void draw_face_polygon(u16 a_bx, u16 b_si, u16 c_di);
/* 0e12:3ebf fill_around_camera, 3f6c fill_below_edge, 3fe5 fill_above_edge — render3d.md §4.7/4.9 */
void fill_around_camera(u16 b_bx, u16 s_si, u16 d_di);
void fill_below_edge(u16 x0_ax, u16 x1_cx, u16 a_bx, u16 b_si);
void fill_above_edge(u16 x0_ax, u16 x1_cx, u16 a_bx, u16 b_si);
/* 0e12:41d4 fill_spans — render3d.md §4.8 (SI = x left, BP = x right, 1/32 px; state in DS:BA9C..BACD) */
void fill_spans(u16 xl_si, u16 xr_bp);
/* 0e12:43db blob_spans, 44c2 line_thin_spans — render3d.md §4.10 */
void blob_spans(u16 xl_si, u16 xr_bp);
void line_thin_spans(u16 x_bp, u16 step_si);
/* 0e12:66ad point_draw (BX = 2v, SI = w0), 67af line_draw (BX, SI = 2v, AX, DX = their x, ES:BP = &w0) */
void point_draw(u16 v_bx, u16 w0_si);
void line_draw(u16 p_bx, u16 q_si, u16 xp_ax, u16 xq_dx, FarPtr w0_es_bp);
/* 0e12:682e tri_fill (+ entries 685c / 68b0 / 6909), 69da quad_fill (face record at ES:BAD0) — render3d.md §4.10 */
void tri_fill(u16 a_bx, u16 b_si, u16 c_di);
void tri_fill_row(u16 a_bx, u16 b_si, u16 c_di);
void tri_fill_flat_bottom(u16 a_bx, u16 b_si, u16 c_di);
void tri_fill_flat_top(u16 a_bx, u16 b_si, u16 c_di);
void quad_fill(u16 a_bx, u16 b_si, u16 c_di, u16 d_bp, u16 rec_es);

/* ---- render_mirror.c: the rear-view mirror pass (buffer M) ---- */
/* 0e12:8b5f vertex_project_y_mirror (BX = 2v) */
void vertex_project_y_mirror(u16 v_bx);
/* 0e12:7b9b mirror_sky_ground, 7cd9 / 7d94 fill edges, 7e11 / 8016 / 80fb spans — render3d.md §4.10/4.12 */
void mirror_sky_ground(void);
void mirror_fill_above_edge(u16 x0_ax, u16 x1_cx, u16 a_bx, u16 b_si);
void mirror_fill_below_edge(u16 x0_ax, u16 x1_cx, u16 a_bx, u16 b_si);
void fill_spans_mirror(u16 xl_si, u16 xr_bp);
void blob_spans_mirror(u16 xl_si, u16 xr_bp);
void line_thin_spans_mirror(u16 x_bp, u16 step_si);
void point_draw_mirror(u16 v_bx, u16 w0_si);
void line_draw_mirror(u16 p_bx, u16 q_si, u16 xp_ax, u16 xq_dx, FarPtr w0_es_bp);
void tri_fill_mirror(u16 a_bx, u16 b_si, u16 c_di);
void tri_fill_mirror_839c(u16 a_bx, u16 b_si, u16 c_di);
void tri_fill_mirror_8413(u16 a_bx, u16 b_si, u16 c_di);
void tri_fill_mirror_8490(u16 a_bx, u16 b_si, u16 c_di);
void quad_fill_mirror(u16 a_bx, u16 b_si, u16 c_di, u16 d_bp, u16 rec_es);

/* ---- render_sprite.c: sprites and the face/sprite merge loop ---- */
/* 0e12:323e draw_faces_and_sprites — render3d.md §4.13 */
void draw_faces_and_sprites(void);

/* ---- render_overlay.c: cockpit overlays ---- */
/* 0e12:46c0 cockpit_overlays — render3d.md §4.12 */
void cockpit_overlays(void);

/* ---- render_object.c: objects / vehicles ---- */
/* 0e12:4d6d obj_runtime_colours_all — render3d.md §4.14 */
void obj_runtime_colours_all(void);

/* ---- shared bodies of the view / mirror clones (render_raster.c) ---- */
s16  r3_idiv(s16 num, s16 den);                           /* slope idiv, PORT: clamps instead of INT 0 */
void r3_fill_spans(u16 seg, u16 rowtab, u16 rows, const u16 cput[5], const u16 cor[5], u16 xl_si, u16 xr_bp);
void r3_blob_spans(u16 seg, u16 rowtab, u16 rows, const u16 c[5], u16 xl_si, u16 xr_bp);
void r3_thin_spans(u16 seg, u16 rowtab, u16 rows, const u16 c[5], u16 x_bp, u16 step_si);
void r3_tri_fill(bool m, u16 a_bx, u16 b_si, u16 c_di);
void r3_tri_row(bool m, u16 a_bx, u16 b_si, u16 c_di);
void r3_tri_flat_bottom(bool m, u16 a_bx, u16 b_si, u16 c_di);
void r3_tri_flat_top(bool m, u16 a_bx, u16 b_si, u16 c_di);
void r3_quad_fill(bool m, u16 a_bx, u16 b_si, u16 c_di, u16 d_bp, u16 rec_es);
void r3_point_draw(bool m, u16 v_bx, u16 w0_si);
void r3_line_draw(bool m, u16 p_bx, u16 q_si, u16 xp_ax, u16 xq_dx, FarPtr w0_es_bp);
void r3_fill_below_edge(bool m, u16 rows, u16 x0_ax, u16 x1_cx, u16 a_bx, u16 b_si);
void r3_fill_above_edge(bool m, u16 x0_ax, u16 x1_cx, u16 a_bx, u16 b_si);

#endif /* RENDER3D_INTERNAL */
