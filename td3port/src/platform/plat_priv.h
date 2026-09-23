#pragma once
/* Private helpers shared by the platform module's .c files (not part of the cross-module interface).
 *
 * Host-side hardware state (interrupt vectors hooked, the BIOS clock, the INT 33h driver's pointer, DOS
 * file handles) lives in C statics inside the platform files: it models the machine, not the game
 * (PORT; game-visible state stays in mem[]). */
#include "mem.h"
#include "symbols.h"

/* File segment of the platform code segment (keyboard tables, font, DAC buffer). */
#define PLAT_CS 0x0C1C

/* The library's "y * 320" as the mode 13h paths compute it: xchg al,ah (y << 8 for 0 <= y < 256), then
 * + that value >> 2, all in 16 bits (1785:02c4, 1818:0227, 173c:0161, 185f:0453, 0c1c:0c45 ...). */
static inline u16 plat_row320(u16 y)
{
    u16 sw = (u16)(y << 8 | y >> 8);
    return (u16)(sw + (sw >> 2));
}

/* ---- timer.c ---- */
/* BIOS clock low word (INT 1Ah AH=0): 18.2 Hz in both timer states (platform.md 4.2.8). */
u16 plat_bios_ticks(void);

/* ---- dos.c ---- */
/* Creates the DOS memory arena (one free MCB from HEAP_BOTTOM to HEAP_TOP). Called by platform_init. */
void plat_heap_init(void);

/* ---- kbd.c ---- */
/* Host keyboard handler installed by platform_init (routes bytes to kbd_isr while INT 9 is hooked). */
void plat_kbd_byte(u8 b);
void plat_kbd_focus_lost(void);          /* PORT: host focus-lost hook (kbd.c) */
/* BIOS 0040:0017 as modelled (shift and lock bits), for tests. */
u8 plat_kbd_bda17(void);
