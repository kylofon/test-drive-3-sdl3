/* Platform: joystick (port 201h) and the graphics library's mouse module (INT 33h) —
 * platform.md §4.4.3, §4.2.7. */
#include "platform/platform.h"
#include "platform/plat_priv.h"
#include "host.h"

/* ---------------------------------------------------------------- joystick */

/* 0c1c:0664 joy_read_raw — platform.md §4.4.3
 * PORT: the port 201h one-shot timing is synthesised from the host gamepad on the default calibration
 * scale: count = 80h + axis * 80h / 32768 (0..100h), buttons A -> 10h, B -> 20h. Without a gamepad the
 * port reads as an unconnected game port: both axes time out (CX = SI = FFFFh -> -1 after sar) and no
 * button is pressed. */
u16 joy_read_raw(void)
{
    u16 t0 = DSW(DS_tick_count);
    s16 ax, ay;
    u8 b;
    u16 cx, si, port;
    if (host_joy_read(&ax, &ay, &b)) {
        s16 jx = (s16)(0x80 + (s32)ax * 0x80 / 32768);
        s16 jy = (s16)(0x80 + (s32)ay * 0x80 / 32768);
        cx = (u16)(jx << 3);                /* the loops count port reads x 8 (cx starts at -8) */
        si = (u16)(jy << 3);
        port = (u8)(0xFF & ~((b & 1) ? 0x10 : 0) & ~((b & 2) ? 0x20 : 0));
    } else {
        cx = 0xFFFF;                        /* overflow path 06ae */
        si = 0xFFFF;
        port = 0xFF;
    }
    cx = (u16)((s16)cx >> 3);               /* sar x3 */
    si = (u16)((s16)si >> 3);
    if (DSW(DS_tick_count) == t0) {         /* discard readings disturbed by the timer IRQ */
        DSW(DS_joy_x) = cx;
        DSW(DS_joy_y) = si;
    }
    return (u16)(~port & 0x30);
}

/* 0c1c:06d8 joy_read — platform.md §4.4.3 */
u16 joy_read(void)
{
    u16 b = joy_read_raw();
    u16 cx = (u16)(DSW(DS_joy_xcentre) << 2);
    if (cx < DSW(DS_joy_x)) DSW(DS_joy_x) = cx;     /* unsigned: a timeout (FFFFh) becomes 4 x centre */
    cx = (u16)(DSW(DS_joy_y_ctr) << 2);
    if (cx < DSW(DS_joy_y)) DSW(DS_joy_y) = cx;
    return b;
}

/* ---------------------------------------------------------------- mouse driver model */

/* PORT: the INT 33h driver's own state (host side): pointer position in driver coordinates (mode 13h:
 * x 0..639, y 0..199), its range, and the vertical mickey remainder. Motion comes from host mickeys:
 * 8 mickeys per 8 driver units horizontally, 16 per 8 vertically (DOS driver defaults), i.e. one game
 * pixel per 2 mickeys in x and in y (platform.md 4.2.7). */
static s16 drv_x = 320, drv_y = 100;
static s16 drv_xmin = 0, drv_xmax = 639, drv_ymin = 0, drv_ymax = 199;
static s16 drv_yrem;

static void drv_clamp(void)
{
    if (drv_x < drv_xmin) drv_x = drv_xmin;
    if (drv_x > drv_xmax) drv_x = drv_xmax;
    if (drv_y < drv_ymin) drv_y = drv_ymin;
    if (drv_y > drv_ymax) drv_y = drv_ymax;
}

/* INT 33h AX=0 (reset): pointer centred, full range, hidden; returns the button count in BX. */
static u16 int33_reset(void)
{
    drv_xmin = 0; drv_xmax = 639; drv_ymin = 0; drv_ymax = 199;
    drv_x = 320; drv_y = 100; drv_yrem = 0;
    host_mouse_motion(NULL, NULL);          /* discard motion so far */
    return 2;
}

/* INT 33h AX=3: position and buttons. */
static void int33_get(u16 *cx, u16 *dx, u16 *bx)
{
    s16 mx, my;
    u8 b;
    host_mouse_motion(&mx, &my);
    host_mouse_read(NULL, NULL, &b);
    drv_x = (s16)(drv_x + mx);
    s16 ty = (s16)(drv_yrem + my);
    drv_y = (s16)(drv_y + ty / 2);
    drv_yrem = (s16)(ty % 2);
    drv_clamp();
    *cx = (u16)drv_x;
    *dx = (u16)drv_y;
    *bx = b;
}

/* 16e6:000e mouse_init — platform.md §4.2.7 (PORT: a driver is always present, 2 buttons) */
s16 mouse_init(void)
{
    u16 bx = int33_reset();                 /* AX = FFFFh: driver installed */
    DSB(DS_mouse_visible) = 0;
    DSB(DS_mouse_present) = 1;
    if (DSW(DS_mode_row_bytes) != 0) {      /* graphics mode */
        u16 cx, dx, b;
        int33_get(&cx, &dx, &b);
        u16 w = DSW(DS_mode_width);
        u16 q = w ? (u16)(cx / w) : 0;      /* 320 / 320 = 1 in mode 13h */
        DSB(DS_mouse_shift_x) = (u8)q;
        DSB(DS_mouse_shift_y) = (u8)(q >> 8);
    } else {
        DSB(DS_mouse_shift_x) = (u8)(((DSW(DS_mode_text_cols) >> 3) ^ 1) & 7);
        DSB(DS_mouse_shift_y) = 3;
    }
    s16 b = (s16)bx;
    return b > 0 ? b : (s16)(b + 3);
}

/* 16ec:0004 mouse_set_range — platform.md §4.2.7 (INT 33h AX=7 / AX=8) */
void mouse_set_range(s16 x0, s16 x1, s16 y0, s16 y1)
{
    s16 a = (s16)(x0 << DSB(DS_mouse_shift_x)), b = (s16)(x1 << DSB(DS_mouse_shift_x));
    drv_xmin = a < b ? a : b; drv_xmax = a < b ? b : a;
    a = (s16)(y0 << DSB(DS_mouse_shift_y)); b = (s16)(y1 << DSB(DS_mouse_shift_y));
    drv_ymin = a < b ? a : b; drv_ymax = a < b ? b : a;
    drv_clamp();
}

/* 16ef:0009 mouse_set_pos — platform.md §4.2.7 (INT 33h AX=4) */
void mouse_set_pos(s16 x, s16 y)
{
    drv_x = (s16)(x << DSB(DS_mouse_shift_x));
    drv_y = (s16)(y << DSB(DS_mouse_shift_y));
    drv_yrem = 0;
    host_mouse_motion(NULL, NULL);          /* PORT: motion before the move is not applied after it */
    drv_clamp();
}

/* 16f1:000d mouse_get — platform.md §4.2.7 (INT 33h AX=3) */
void mouse_get(s16 *x, s16 *y, s16 *buttons)
{
    u16 cx, dx, bx;
    int33_get(&cx, &dx, &bx);
    *x = (s16)(cx >> DSB(DS_mouse_shift_x));
    *y = (s16)(dx >> DSB(DS_mouse_shift_y));
    if (DSW(DS_mode_row_bytes) != 0 && *x >= (s16)DSW(DS_mode_width))
        *x = (s16)(DSW(DS_mode_width) - 1);
    *buttons = (s16)bx;
}

/* 16f5:0008 mouse_show — platform.md §4.2.7 (PORT: the cursor is never drawn) */
void mouse_show(s16 on)
{
    on &= 1;
    if (on == DSB(DS_mouse_visible)) return;
    DSB(DS_mouse_visible) = (u8)on;
    /* INT 33h AX = 2 - on; EGA GC register restore on hide in modes 0Dh-12h: not ported */
}
