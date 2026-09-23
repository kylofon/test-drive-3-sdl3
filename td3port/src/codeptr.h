#pragma once
/* Far code pointers stored in game memory.
 *
 * The original keeps far pointers to code in its data (the key handler table at 0e12:0000, sound
 * device routines, ...). The port stores the same seg:off values in mem[] and maps them to C
 * functions here. A module registers each function that can be stored this way, using the FN_ constant
 * from symbols.h (file segment << 16 | offset):
 *
 *     codeptr_register(FN_snd_tick, snd_tick);
 *     wr16(...) / ds_far_wr(DS_x, codeptr_far(FN_snd_tick));     // store it like the original
 *     void (*f)(void) = codeptr_lookup(ds_far(DS_x));              // call it later
 *
 * Functions with other signatures are cast to CodeFn at registration and back at the call site.
 */
#include "mem.h"

typedef void (*CodeFn)(void);

void   codeptr_register(u32 fn_addr, CodeFn fn);
FarPtr codeptr_far(u32 fn_addr);            /* relocated far pointer for FN_x (segment + LOAD_SEG) */
CodeFn codeptr_lookup(FarPtr p);            /* NULL for a null pointer; fatal for an unregistered one */

/* Near code pointers (the key handler table CS:0000 of 0e12, the sound device routine tables in
 * DGROUP): an offset into a known code segment, given as a file segment (0x0E12, 0x1ACE). */
CodeFn codeptr_lookup_near(u16 file_seg, u16 off);   /* fatal for an unregistered one */
