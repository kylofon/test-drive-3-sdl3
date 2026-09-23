/* Platform: keyboard ISR (INT 9) with its translation tables — platform.md §4.4.1.
 * The tables are read from the loaded EXE in the code segment 0c1c (CS:0CAE E0 map, CS:0CC2/0D32/0DA2
 * ctrl/normal/shift, CS:0E12/0E1A/0E22 direction tables). */
#include "platform/platform.h"
#include "platform/plat_priv.h"

/* Host-side machine state (PORT):
 *   kbd_hooked      INT 9 = kbd_isr (kbd_install .. kbd_restore); bytes arriving otherwise go to the BIOS
 *                   handler, whose key buffer nobody reads in TD3 (dropped)
 *   bda17/18/97     BIOS data 0040:0017 (shift/lock state), 0040:0018 (lock keys held), 0040:0097 (LEDs)
 *   kbd_sent        the last byte kbd_isr wrote to port 60h (EDh LED command / LED bits), 0 = none */
static bool kbd_hooked;
static u8 bda17, bda18, bda97;
static s16 kbd_sent = -1;

#define CS_KBD_E0_MAP   0x0CAE
#define CS_KBD_TAB_CTRL 0x0CC2
#define CS_KBD_TAB_NORM 0x0D32
#define CS_KBD_TAB_SHFT 0x0DA2
#define CS_KBD_DIR_OR   0x0E12
#define CS_KBD_DIR_AND  0x0E1A
#define CS_KBD_KP_DIR   0x0E22

/* PORT: out 60h — a byte sent to the keyboard (LED command EDh, then the LED bits). */
static void send_to_kbd(u8 b) { kbd_sent = b; }

/* 0c1c:0e2d kbd_install — platform.md §4.4.1 */
void kbd_install(void)
{
    bda17 &= 0xF3;                          /* clear Ctrl/Alt */
    DSB(DS_kbd_prefix) = 0;
    DSB(DS_kbd_last_make) = 0;
    DSB(DS_kbd_last_scan) = 0;
    DSB(DS_kbd_key) = 0;
    DSB(DS_kbd_bits) = 0;
    /* INT 21h 3509h -> DS:9154 (PORT: no vector to save; the far pointer stays as it is), port 61h &= 7Fh,
     * INT 9 = kbd_isr */
    kbd_hooked = true;
    DSW(DS_kbd_installed) = 1;              /* word store: DS:009E (playdisk_ok) = 0 as well */
}

/* 0c1c:0e7d kbd_restore — platform.md §2.3 (if DS:009D: restore INT 9; DS:009D stays set) */
void kbd_restore(void)
{
    if (DSW(DS_kbd_installed) != 0)
        kbd_hooked = false;
}

/* 0c1c:0e98 kbd_isr — platform.md §4.4.1 (sc = the port 60h byte) */
void kbd_isr(u8 sc)
{
    u8 ah = sc, al, bl;

    /* port 61h bit 7 acknowledge pulse: nothing to model */
    if (DSB(DS_kbd_prefix) == 0xED) {       /* byte after the LED command (normally the ACK FAh) */
        al = (u8)((bda17 >> 4) & 7);
        bda97 = (u8)((bda97 & 0xF8) | al);
        send_to_kbd(al);
        DSB(DS_kbd_prefix) = 0;
    }
    al = ah;
    if (al >= 0xE0) {
        if (al == 0xE0 || al == 0xE1) DSB(DS_kbd_prefix) = al;
        return;                             /* FAh etc. ignored */
    }
    al &= 0x7F;
    if (al == 0x1D || al == 0x61)           /* Ctrl (tested before the prefix handling) */
        DSB(DS_kbd_ctrl) = (ah & 0x80) ? 0 : 1;
    if (al == 0x2A || al == 0x36) {         /* shift keys */
        u8 bit = (al == 0x2A) ? 2 : 1;
        if (DSB(DS_kbd_prefix) == 0xE0) {   /* fake shift of the grey keys */
            DSB(DS_kbd_prefix) = 0;
            return;
        }
        if (!(ah & 0x80)) bda17 |= bit;
        else bda17 &= (u8)~bit;
    }
    if (DSB(DS_kbd_prefix) == 0xE1) {       /* Pause: E1 1D 45 / E1 9D C5 */
        if (al == 0x1D) return;             /* prefix stays E1 */
        if (al != 0x45) { DSB(DS_kbd_prefix) = 0; return; }
        ah++; al++;                          /* 45h -> 46h, handled as E0 46h (Break) */
        DSB(DS_kbd_prefix)--;                /* E1h -> E0h */
    }
    if (DSB(DS_kbd_prefix) == 0xE0) {       /* grey keys -> pseudo scan codes */
        al = (u8)(al - 0x1C);
        if (al >= 2) {
            al = (u8)(al - 0x17);
            if (al >= 6) al = (u8)(al - 0x0D);
        }
        al = SEGB(PLAT_CS, CS_KBD_E0_MAP + al);     /* cs: xlat */
        if (al == 0) { DSB(DS_kbd_prefix) = 0; return; }
        ah = (u8)((ah & 0x80) | al);
        DSB(DS_kbd_prefix) = 0;
    }
    /* typematic filter */
    if (!(ah & 0x80)) {
        if (al == DSB(DS_kbd_last_make)) return;
        DSB(DS_kbd_last_make) = ah;
    } else if (al == DSB(DS_kbd_last_make)) {
        DSB(DS_kbd_last_make) = ah;
    }
    /* lock keys */
    {
        u8 lk = (al == 0x3A) ? 0x40 : (al == 0x45) ? 0x20 : (al == 0x46) ? 0x10 : 0;
        if (lk) {
            if (!(ah & 0x80)) {
                if (!(bda18 & lk)) {
                    bda18 |= lk;
                    bda17 ^= lk;
                    send_to_kbd(0xED);
                    DSB(DS_kbd_prefix) = 0xED;
                }
            } else {
                bda18 &= (u8)~lk;
            }
        }
    }
    if (!(ah & 0x80)) {                     /* translate make codes */
        u16 tab = CS_KBD_TAB_CTRL;
        if (DSB(DS_kbd_ctrl) == 0) tab = (bda17 & 3) ? CS_KBD_TAB_SHFT : CS_KBD_TAB_NORM;
        al = SEGB(PLAT_CS, (u16)(tab + ah));        /* cs: xlat */
        if ((bda17 & 0x20) && al >= 0x91) {         /* NumLock (sic: 91h..99h -> 21h..29h) */
            if (al <= 0x99) al = (u8)(al - 0x70);
            if (al == 0x1E) al = (u8)(al + 0x10);   /* dead: only reached for al >= 91h */
        }
        if ((bda17 & 0x40) && al >= 0x41 && al <= 0x7A && !(al >= 0x5B && al < 0x61))
            al ^= 0x20;                             /* CapsLock */
        DSB(DS_kbd_key) = al;
    }
    DSB(DS_kbd_last_scan) = ah;
    /* held direction bits */
    bl = (u8)(ah & 0x7F);
    al = DSB(DS_kbd_bits);
    if (bl == 0x60 || bl == 0x1C) {         /* Enter / KP Enter */
        al |= 0x10;
        if (ah & 0x80) al &= 0xEF;
    } else if (bl == 0x39) {                /* Space */
        al |= 0x20;
        if (ah & 0x80) al &= 0xDF;
    } else {
        if (bl == 0x29) bl = 0x48;          /* ` acts as Up */
        if (bl == 0x2B) bl = 0x4B;          /* \ acts as Left */
        bl = (u8)(bl - 0x47);
        if (bl & 0x80) return;              /* js */
        if (bl <= 10) bl = SEGB(PLAT_CS, CS_KBD_KP_DIR + bl);
        bl = (u8)(bl - 0x1F);
        if (bl & 0x80) return;              /* js */
        if (bl >= 8) return;                /* jae */
        al |= SEGB(PLAT_CS, CS_KBD_DIR_OR + bl);
        if (ah & 0x80) al &= SEGB(PLAT_CS, CS_KBD_DIR_AND + bl);
    }
    DSB(DS_kbd_bits) = al;
    /* out 20h, 20h (EOI) */
}

/* PORT: host keyboard handler (host_set_kbd_handler). Feeds the XT byte to kbd_isr while INT 9 is hooked.
 * The keyboard answers the LED command EDh and the LED byte with an ACK FAh each (platform.md 4.4.1):
 * the port feeds those synthetic bytes right after the byte whose ISR run sent the command. */
void plat_kbd_byte(u8 b)
{
    if (!kbd_hooked) return;
    kbd_sent = -1;
    kbd_isr(b);
    for (int guard = 0; kbd_sent >= 0 && guard < 4; guard++) {
        kbd_sent = -1;
        kbd_isr(0xFA);                      /* ACK: sends the LED bits after EDh; ignored otherwise */
    }
}

/* PORT: lock/shift state of the modelled BIOS data area, for platform tests. */
u8 plat_kbd_bda17(void) { return bda17; }

/* PORT: host focus-lost hook (platform.md 4.4.1): a Shift released outside the window would stick,
 * a case the original never had. Clears the shift bits of the modelled BIOS byte 0040:0017. */
void plat_kbd_focus_lost(void)
{
    bda17 &= (u8)~0x03;
}
