/* Platform: DAC uploads and palette fades (0c1c:0ae0-0c2a) — platform.md §4.3.1, §4.3.2.
 * The upload buffer is CS:0002[768] inside the code segment 0c1c (in mem[], as the original writes it);
 * the game palette is DS:0B6A[768] (6-bit RGB). */
#include "platform/platform.h"
#include "platform/plat_priv.h"
#include "platform/vga.h"
#include "host.h"

#define CS_DAC_BUF 0x0002

/* Upload of n colours from CS:0002 in batches of 32, each after a vertical-blank sync.
 * PORT: the DAC is written at once, then host_wait_vretrace() waits for the next 70.086 Hz retrace and
 * presents: one frame per upload, as the original's first batch waits for the next vertical blank and the
 * following batches fit in the same blank (platform.md 4.3.1, open question 3). */
static void dac_upload(u16 n)
{
    for (u16 c = 0; c < n; c++)
        vga_dac_write((u8)c, SEGB(PLAT_CS, CS_DAC_BUF + 3 * c), SEGB(PLAT_CS, CS_DAC_BUF + 3 * c + 1),
                      SEGB(PLAT_CS, CS_DAC_BUF + 3 * c + 2));
    host_wait_vretrace();
}

/* 0c1c:0ae0 dac_upload_256 — platform.md §4.3.1 */
static void dac_upload_256(void) { dac_upload(256); }

/* 0c1c:0b1d dac_upload_128 — platform.md §4.3.1 */
static void dac_upload_128(void) { dac_upload(128); }

/* CS:0002[i] = (palette[i] * level + 8) >> 4 for i < n (8x8-bit mul, byte store) */
static void pal_scale(u16 n, u8 level)
{
    for (u16 i = 0; i < n; i++) {
        u16 ax = (u16)(DSB((u16)(DS_palette + i)) * level);
        SEGB(PLAT_CS, (u16)(CS_DAC_BUF + i)) = (u8)((u16)(ax + 8) >> 4);
    }
}

/* 0c1c:0b5b pal_fade_out — platform.md §4.3.2 (levels 15..0) */
void pal_fade_out_0C1C_0B5B(void)
{
    for (s16 dx = 15; dx >= 0; dx--) {
        pal_scale(0x300, (u8)dx);
        dac_upload_256();
    }
}

/* 0c1c:0bb7 pal_fade_out_low — platform.md §4.3.2 (colours 0-127, levels 15..0) */
void pal_fade_out_low_0C1C_0BB7(void)
{
    for (s16 dx = 15; dx >= 0; dx--) {
        pal_scale(0x180, (u8)dx);
        dac_upload_128();
    }
}

/* 0c1c:0be5 pal_fade_in — platform.md §4.3.2 (levels 1..16) */
void pal_fade_in_0C1C_0BE5(void)
{
    for (u8 dl = 1; dl != 0x11; dl++) {
        pal_scale(0x300, dl);
        dac_upload_256();
    }
}

/* 0c1c:0c16 pal_black — platform.md §4.3.2 */
void pal_black_0C1C_0C16(void)
{
    for (u16 i = 0; i < 0x300; i++) SEGB(PLAT_CS, (u16)(CS_DAC_BUF + i)) = 0;
    dac_upload_256();
}

/* 0c1c:0c2a pal_set — platform.md §4.3.2 */
void pal_set(void)
{
    for (u16 i = 0; i < 0x300; i++) SEGB(PLAT_CS, (u16)(CS_DAC_BUF + i)) = DSB((u16)(DS_palette + i));
    dac_upload_256();
}
