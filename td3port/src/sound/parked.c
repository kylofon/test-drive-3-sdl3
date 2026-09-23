/* Parked devices (sound.md §2 "Parked devices"): MPU-401 / MT-32 (1ace:062c..07ac), Game Blaster /
 * CMS (1ace:0a18..0b91) and Tandy SN76489 (1ace:0c15..0ce8).
 * PORT: not ported (PORTING.md, PLAN.md TODO). TD3.CFG audio 1-3 are mapped to the AdLib routines in
 * music_init / sfx_init, so snd_select_routines never selects these columns; the functions exist only
 * because their addresses are stored in the device tables DS:C92E..C956 (registered as code pointers).
 * Probes report "not found", everything else does nothing. */
#include "sound/sound.h"
#include "sound/sound_int.h"

/* 1ace:062c mpu_reset — PORT: parked, no MPU-401 */
u16 mpu_reset(void) { return 0; }
/* 1ace:0637 mpu_program — PORT: parked */
void mpu_program(u16 bx, u16 si, u16 cx) { }
/* 1ace:064f mpu_note_on — PORT: parked */
void mpu_note_on(u16 bx, u16 si, u16 cx, u16 dx) { }
/* 1ace:06a3 mpu_set_freq — PORT: parked (pitch bend) */
void mpu_set_freq(u16 bx, u16 si, u16 cx) { }
/* 1ace:0703 mpu_note_off — PORT: parked */
void mpu_note_off(u16 bx, u16 si, u16 cx) { }
/* 1ace:073d mpu_command — PORT: parked (returns 0 = no acknowledge) */
u16 mpu_command(u8 ah) { return 0; }
/* 1ace:076d mpu_data — PORT: parked */
void mpu_data(u8 al) { }
/* 1ace:0787 mpu_all_off — PORT: parked */
void mpu_all_off(u16 bx) { }
/* 1ace:0792 mpu_load_bank — PORT: parked (DS:C974 = 0, returns 0) */
u16 mpu_load_bank(u16 ax) { DSB(DS_mpu_bank_loaded) = 0; return 0; }

/* 1ace:0a18 cms_detect — PORT: parked, no Game Blaster */
u16 cms_detect(void) { return 0; }
/* 1ace:0a59 cms_note_on — PORT: parked */
void cms_note_on(u16 bx, u16 si, u16 cx, u16 dx) { }
/* 1ace:0abf cms_set_freq — PORT: parked */
void cms_set_freq(u16 bx, u16 si, u16 cx) { }
/* 1ace:0b16 cms_note_off — PORT: parked */
void cms_note_off(u16 bx, u16 si, u16 cx) { }
/* 1ace:0b5f cms_reset — PORT: parked (also the CMS "all off" routine) */
void cms_reset(u16 bx) { }

/* 1ace:0c15 tandy_note_on — PORT: parked */
void tandy_note_on(u16 bx, u16 si, u16 cx, u16 dx) { }
/* 1ace:0c66 tandy_set_freq — PORT: parked */
void tandy_set_freq(u16 bx, u16 si, u16 cx) { }
/* 1ace:0ca0 tandy_note_off — PORT: parked */
void tandy_note_off(u16 bx, u16 si, u16 cx) { }
/* 1ace:0cd1 tandy_all_off — PORT: parked */
void tandy_all_off(u16 bx) { }
