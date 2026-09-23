#pragma once
/* Host services on top of SDL3: window/present, the 145.65 Hz timer tick, VGA vertical retrace,
 * keyboard (XT byte stream for the game's own INT 9 handler), mouse, gamepad, audio (OPL2 through
 * Nuked-OPL3 and the PC speaker), game file lookup and fatal errors. No game logic lives here. */
#include "types.h"

#define PIT_HZ        1193182u
#define PIT_DIV_GAME  0x2000u        /* 8192 -> 145.652 Hz, timer_install 0c1c:109f (platform.md 4.4.2) */

bool host_init(const char *game_dir, int window_scale, bool fullscreen);
void host_shutdown(void);

/* Called once per 145.652 Hz tick from host_pump() (the port's replacement for INT 8). The platform
 * timer module installs the ISR body (snd_tick, tick counter DS:00A0, BIOS tick every 8th). */
void host_set_tick_handler(void (*handler)(void));

/* Source of the displayed image: fills a w x h XRGB8888 frame and returns true if it changed since the
 * last call. Installed by the VGA model (platform/vga.c, 320x200 mode 13h). Shown with 4:3 aspect. */
#define HOST_FRAME_MAX_W 320
#define HOST_FRAME_MAX_H 200
void host_set_frame_source(bool (*compose)(u32 *xrgb), int w, int h);

/* Runs due timer ticks (and their audio), handles window events and presents the screen when it
 * changed. Every busy-wait loop of the original (tick waits, key polls, delays) must call this.
 * Sleeps briefly when nothing was due, so tight polling loops do not spin the CPU. */
void host_pump(void);

/* Waits for the start of the next vertical retrace of the emulated VGA (mode 13h: 70.086 Hz), pumping
 * meanwhile, then presents. Replaces the port 3DAh polls (platform.md: race frame copy, fades). */
void host_wait_vretrace(void);

/* Race frame pacing (PLAN.md decision 4). race_run waits until at least this many timer ticks have
 * passed since the frame started; the original constant is 5 (engine cap, ~29 fps: the game runs
 * 5.8x faster than the race clock's design rate of 5 fps, 29 ticks); the port defaults to
 * HOST_DEFAULT_FRAME_TICKS = 23 (~6.3 fps), chosen by play-testing as the speed that feels right. */
#define HOST_DEFAULT_FRAME_TICKS 23
void host_set_frame_ticks(int ticks);
int  host_frame_ticks(void);

/* ---- Keyboard: the game's INT 9 handler (kbd_isr 0c1c:0e98). The handler receives the XT byte
 * sequence the keyboard would send, in event order (platform.md 4.4.1): normal keys sc / sc|80h, grey
 * keys E0 sc / E0 sc|80h, Pause E1 1D 45 E1 9D C5, Print Screen E0 2A E0 37 / E0 B7 E0 AA. Key repeats
 * feed the make code again. */
void host_set_kbd_handler(void (*handler)(u8 byte));
/* Called when the window loses keyboard focus (keys released outside it never send their break). */
void host_set_focus_lost_handler(void (*handler)(void));

/* ---- Mouse (INT 33h replacement): position in 320x200 screen pixels, buttons bit0 left, bit1 right.
 * Motion in mickeys since the last call (for the driver's relative reads). */
void host_mouse_read(s16 *x, s16 *y, u8 *buttons);
void host_mouse_motion(s16 *dx, s16 *dy);

/* ---- Joystick (port 201h replacement): first connected gamepad. Axes -32768..32767, buttons bit0 = A,
 * bit1 = B. */
bool host_joy_read(s16 *x, s16 *y, u8 *buttons);

/* ---- Audio. The sound driver writes OPL2 registers and the PC speaker as the original does; writes
 * take effect from the current tick onward (sound.md, port design). */
void host_opl_write(u8 reg, u8 value);
void host_speaker(u16 divisor, bool on);        /* PIT ch2 divisor (0 = 65536) and the port 61h gate */

/* ---- Game files: case-insensitive lookup inside the game directory. Returns a malloc'd path (free with
 * host_free) or NULL if the file does not exist. For new files (.HI, PLAYDISK.DAT) pass create = true. */
char *host_game_path(const char *name, bool create);
void  host_free(void *p);

/* ---- Errors: shows a message box, shuts down and exits with code 3. */
_Noreturn void host_fatal(const char *fmt, ...);

/* ---- Developer aids (environment variables):
 *   TD3_SNAPSHOT_DIR=dir   every presented frame >= 2 s after the previous one is saved as snapNNNN.bmp
 *                          (works with SDL_VIDEO_DRIVER=dummy)
 *   TD3_KEYS="<seconds>:<xt>[+<xt>...],..."  presses (in order) and releases (in reverse) the XT keys
 *                          at that many seconds after start-up; a "p" after the codes only presses
 *                          (held), an "r" only releases: "9:48p,20:48r" holds Up for 11 s. Grey keys
 *                          are written with their E0 prefix as e048. */
