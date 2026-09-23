/* Platform: port set-up, timer ISR (INT 8), BIOS clock, misc 0c1c helpers — platform.md §4.4.2, §4.2.8. */
#include "platform/platform.h"
#include "platform/plat_priv.h"
#include "host.h"
#include "sound/sound.h"

/* Host-side machine state (PORT: models the INT 8 vector and the BIOS clock, not game state).
 *   timer_hooked  INT 8 = timer_isr (between timer_install and timer_restore)
 *   bios_ticks    BIOS clock 0040:006C, advanced at 18.2 Hz in both states
 *   bios_phase    8 host ticks (145.652 Hz) = one 18.2 Hz tick while the PIT runs with divisor 0 */
static bool timer_hooked;
static u32 bios_ticks;
static u8 bios_phase;

u16 plat_bios_ticks(void) { return (u16)bios_ticks; }

/* PORT: the old INT 8 handler (BIOS clock) that timer_isr chains to on every 8th tick. */
static void bios_int8(void) { bios_ticks++; }

/* 0c1c:10e8 timer_isr — platform.md §4.4.2 (snd_tick, DS:00A0++, every 8th tick the old INT 8, else EOI) */
void timer_isr(void)
{
    snd_tick();
    DSW(DS_tick_count)++;
    DSB(DS_timer_chain_cnt) = (u8)((DSB(DS_timer_chain_cnt) + 1) & 7);
    if (DSB(DS_timer_chain_cnt) == 0)
        bios_int8();                    /* jmp far CS:109B (the BIOS sends the EOI) */
    /* else: out 20h, 20h (EOI) — nothing to model */
}

/* PORT: host tick (145.652 Hz). While INT 8 is not hooked the PIT runs at 18.2 Hz (divisor 0 = 8 x 2000h),
 * so only the BIOS clock advances, every 8th host tick. */
static void host_tick(void)
{
    if (timer_hooked) {
        timer_isr();
        return;
    }
    bios_phase = (u8)((bios_phase + 1) & 7);
    if (bios_phase == 0) bios_int8();
}

/* Port set-up (no original address) — platform.h */
void platform_init(void)
{
    timer_hooked = false;
    bios_ticks = 0;
    bios_phase = 0;
    plat_heap_init();
    host_set_tick_handler(host_tick);
    host_set_kbd_handler(plat_kbd_byte);
    host_set_focus_lost_handler(plat_kbd_focus_lost);
}

/* 0c1c:109f timer_install — platform.md §4.4.2 */
void timer_install(void)
{
    /* Save INT 8 in CS:109B, INT 8 = timer_isr, PIT ch0 mode 3 divisor 2000h.
     * PORT: the `mov word [C5E4h], 2000h` executed with DS = CS patches code byte 185f:01b4 (Tandy path of
     * gfx_draw_bitmap, never executed in mode 13h) and not DS:C5E4 (platform.md, corrections). No code
     * patching in the port; DS:C5E4 stays 0 as in the original. */
    timer_hooked = true;
}

/* 0c1c:10cf timer_restore — platform.md §4.4.2 (PIT divisor 0, restore INT 8) */
void timer_restore(void)
{
    timer_hooked = false;
}

/* 16ff:000b bios_wait_ticks — platform.md §4.2.8 */
void bios_wait_ticks(s16 n)
{
    u16 t0 = plat_bios_ticks();
    if (n <= 0) return;
    for (;;) {
        u16 t = plat_bios_ticks();
        s16 d;
        if ((s16)t0 > (s16)t)                   /* cmp bx, dx / jg: signed */
            d = (s16)(u16)(0xFFFFu - t0 + t + 1);
        else
            d = (s16)(u16)(t - t0);
        if (d >= n) return;                     /* cmp dx, cx / jl: signed */
        host_pump();
    }
}

/* 0c1c:0662 enable_interrupts — platform.md §2.3 (sti; port: no-op) */
void enable_interrupts(void)
{
}

/* 0c1c:071c far_normalize — platform.md §2.3 (returns DX:AX = seg + off/16 : 0; no callers) */
u32 far_normalize(u16 off, u16 seg)
{
    u16 dx = (u16)(seg + (off >> 4));
    return (u32)dx << 16;
}
