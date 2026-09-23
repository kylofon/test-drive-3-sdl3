#pragma once
/* VGA hardware model for mode 13h (platform.md): the 320x200 byte screen lives in mem[] at A000:0000
 * (page 0); the DAC holds 256 6-bit RGB entries; the CRTC start address shifts the displayed picture
 * (screen shake). The model composes the frame the host presents. Port code writes the DAC and CRTC
 * through these functions where the original writes ports 3C8h/3C9h and 3D4h/3D5h. */
#include "types.h"

void vga_init(void);                         /* installs the frame source; DAC black, start 0 */

void vga_dac_write(u8 index, u8 r, u8 g, u8 b);   /* 6-bit components, as port 3C9h */
void vga_dac_read(u8 index, u8 *r, u8 *g, u8 *b);

/* CRTC start address in bytes (mode 13h: register 0Ch/0Dh value x 4). The screen shows 64000 bytes
 * starting there, wrapping within the 64 KB plane. */
void vga_set_start(u16 byte_offset);
u16  vga_start(void);
