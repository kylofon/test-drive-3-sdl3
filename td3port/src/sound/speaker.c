/* PC speaker routines (device index 0, C5E3 = 0): 1ace:0b92..0c14 — sound.md §4.9.
 * PIT channel 2 and port 61h go to host_speaker(divisor, on). */
#include "sound/sound.h"
#include "sound/sound_int.h"
#include "host.h"

/* PORT: shadow of the speaker hardware (PIT channel 2 reload value, port 61h bits 0-1). This is
 * hardware state, not game state: the original reads it back from the ports (`in al, 61h`). */
static struct { u16 divisor; u8 port61; } spk_hw = { 0, 0 };

static void spk_hw_update(void)
{
    host_speaker(spk_hw.divisor, (spk_hw.port61 & 3) == 3);
}

/* PORT: music_init's `out 43h, B6h` (channel 2, lo/hi byte, mode 3 square wave). */
void spk_pit_mode(void)
{
    spk_hw_update();
}

/* 1ace:0b92 spk_note_on — sound.md §4.9 */
void spk_note_on(u16 bx, u16 si, u16 cx, u16 dx)
{
    if ((u8)bx > 2) return;
    u8 ch = (u8)((cx >> 8) - 0x18);                    /* driver note = MIDI - 24 */
    while ((s8)ch < 0) ch = (u8)(ch + 12);
    cx = (u16)(ch << 8 | (cx & 0xFF));
    bx = (u16)(bx & 0xFF00);                           /* everything plays on voice 0 */
    si = 0;
    if (voice_alloc_check(bx, ch, (u8)(dx >> 8))) return;
    DSB(DS_voice_owner + bx) = DSB(DS_cur_owner);
    DSW(DS_voice_noteword) = cx;
    DSB(DS_voice_note) = ch;
    spk_set_freq(bx, si, cx);
    spk_hw.port61 |= 3;                                /* gate + speaker data on */
    spk_hw_update();
}

/* 1ace:0bc9 spk_set_freq — sound.md §4.9 */
void spk_set_freq(u16 bx, u16 si, u16 cx)
{
    cx = note_fold(cx, 0x6000, si);
    u16 ax = freq_lookup(&cx);
    ax = (u16)(ax >> (cx & 0xFF));                     /* shr ax, cl (octave) */
    spk_hw.divisor = ax;                               /* out 42h lo, hi */
    spk_hw_update();
}

/* 1ace:0bde spk_note_off — sound.md §4.9 */
void spk_note_off(u16 bx, u16 si, u16 cx)
{
    if ((u8)bx > 2) return;
    u8 ch = (u8)((cx >> 8) - 0x18);
    while ((s8)ch < 0) ch = (u8)(ch + 12);
    if (!(DSB(DS_force_off) & 1) && ch != DSB(DS_voice_note)) return;
    DSW(DS_voice_noteword) = 0;
    DSB(DS_voice_note) = 0;
    spk_hw.port61 &= 0xFC;
    spk_hw_update();
}

/* 1ace:0c0e spk_all_off — sound.md §4.9 */
void spk_all_off(u16 bx)
{
    spk_hw.port61 &= 0xFC;
    spk_hw_update();
}
