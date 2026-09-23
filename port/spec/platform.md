# Platform layer (segment `0c1c`, graphics library `16cf`–`1937`, `0ab4:000a`–`01f0`, CRT `1940`)

Conventions: `port/RE_GUIDE.md`. `DS:xxxx` = DGROUP `1BE4` (image `0x1BE40 + xxxx`). `CS:xxxx` in a section about
segment `0c1c` = a variable or table inside that code segment (image `0xC1C0 + xxxx`). All functions are hand-written
assembly with the MS C far calling convention (arguments pushed right to left, first argument at `[bp+6]`, caller
cleans up, result in `AX`). Ghidra call sites show an extra first argument (the pushed CS): ignore it.
Scratch material (per-function listings, table dumps, helper scripts) was produced in the session scratchpad
and is not part of the repository; every table quoted here was dumped from `work/TDIII_unp.exe`.

**Corrections to RE_GUIDE.md / FORMATS.md found here**

* **The VGA build runs library mode 13h, not 14h.** `config_load` (`0000:092a`) maps the TD3.CFG video word through
  the byte table `DS:00EA = 13 0D 09` (0 VGA → 13h, 1 EGA → 0Dh, 2 Tandy → 09h) and calls `gfx_set_mode(DS:E338)`.
  The only other `gfx_set_mode` calls (`0000:07e4`, `0000:0874`) restore the start-up BIOS mode. Every game-side
  check is `DS:E338 == 0x13`. Mode 13h in this library is plain **BIOS mode 13h (chain-4, linear A000:0000,
  320 bytes per row)**. The unchained mode 14h path (`1905:0080`) exists in every primitive, but TD3 never selects it.
  The port therefore implements the **mode 13h** paths (section 4.2 lists what 14h would do).
* **Page 0 is the screen.** In mode 13h the library keeps up to 16 "pages": page 0 = A000h (VRAM, visible),
  the others are DOS blocks of 64000 bytes (`gfx_alloc_page`). TD3 allocates only page 1. `DS:90CC` is a 2-word
  array `{seg(page 0) = A000h, seg(page 1)}` (`DS:90CE` = its second element). `DS:009A` (0/1) selects the
  target of the game-side drawers, so `DS:90CC[DS:009A]` is VRAM when `DS:009A = 0`. Drawing to page 0 shows at once.
  No page flipping, no CRTC start-address flipping, no planar copy exists in the VGA build.
* **One mode for the whole run.** `gfx_set_mode` has exactly three call sites: `config_load` calls it with the BIOS mode
  found at start (`DS:E862`, before TD3.CFG is read) and then with `DS:E338` (13h for VGA). The quit paths
  `0000:07e4` and `0000:0874` call it with `DS:E862` again. Title, menus, race and results all run in
  library mode 13h, with no mode switch in between. Pages in mode 13h:

  | page | segment (`DS:BD8C[p]`, copied to `DS:90CC[p]`) | memory | used for |
  |---|---|---|---|
  | 0 | A000h | VGA memory, chain-4 = linear 320×200, **always the visible screen** | everything that must be seen: HUD, text, message boxes, `view_present`, dissolve target |
  | 1 | DOS block from `gfx_alloc_page(1)` (FA0h paragraphs) | conventional RAM, 64000 bytes linear | off-screen: pictures/backgrounds decoded there, then dissolved or rectangle-copied to page 0 |
  | 2–15 | A000h (never allocated) | – | unused |

  `DS:90CC[DS:009A]` is therefore VRAM when `DS:009A` = 0 and the RAM page when it is 1. Both pages use the same layout
  (`y·320 + x`), and a copy between them is a plain byte copy (`gfx_copy_rect` 1818, `dissolve_page1_to_0`,
  `gfx_copy_rect_from_copy_page` 17eb).
* **`170e:0002` is `gfx_set_copy_page`, not a display-page select** (hud.md calls it `gfx_show_page`). In mode 13h it only sets
  `DS:BD43` = p and `DS:BD86` = `BD8C[p]`, the **source** segment of `17eb` (copy page → draw page) and the destination of
  `17be`. It changes nothing on the screen. Its only callers are `config_load` (1) and the end of `message_box`
  (0000:179c, 1). The value is therefore always page 1, and the call in `message_box` is a redundant re-set. The display-page
  function is `171b:000a` (`gfx_set_visible_page`). In mode 13h it would exchange the memory of the two pages, but TD3 calls
  it only once, with 0 (a no-op).
* `DS:90D0` (render buffer for the 3D view) = paragraph-aligned segment of the `_fmalloc(0x7810)` block `DS:CC5C`
  = **320 × 96** bytes. `DS:90D2` (rear-view mirror buffer) = segment of the **LZW dictionary** block
  (`DS:E7DC`, 0x300 paragraphs). Every picture decompression overwrites the mirror image. The decoder sets
  `DS:BD3F = 1`, and `view_present` then skips the mirror blit until the renderer clears the flag.
* **`timer_install` writes into the graphics library code (bug, harmless for VGA).** It does `push ds / push cs / pop ds`
  (to save the old vector in CS:109B through `INT 21h AX=3508h` and set the new one), and executes
  `mov word [C5E4h], 2000h` (0c1c:10be) **before** `pop ds`. So DS = 0C1Ch, and the store lands at 0C1C:C5E4 = image
  187A4h = **`185f:01b4`**, replacing `8B C1` (`mov ax,cx`) with `00 20` (`add [bx+si],ah`). That instruction is in the
  **mode 09h (Tandy) handler** of `gfx_draw_bitmap` (handler 185f:011b–0259, main per-byte loop, second nibble word). Mode 13h
  uses 185f:0453, EGA 0Dh uses 0403, so the VGA and EGA builds never execute it. In the Tandy build every multi-byte
  `gfx_draw_bitmap` row would draw the second 4-pixel group from the wrong AX and increment a random DGROUP byte at
  `DS:[bx+si]`. DGROUP `C5E4` itself stays 0 and has no readers (sound.md), so the lost store changes nothing. Port:
  nothing to do (no code patching). Keep `pit_divisor` as a documented constant.
* Timer ISR order: `snd_tick` first, then `DS:00A0++`. The old INT 8 handler gets every **8th** tick
  (DS:915D, 0x10000/0x2000 = 8), so the BIOS clock and `bios_wait_ticks` (`16ff`) still run at 18.2 Hz.
* **There is a joystick** (port 201h, `0c1c:0664`/`06d8`), enabled at run time (key code 14h = Ctrl-J, off with 15h =
  Ctrl-K in `get_key 0000:0f80`), not via TD3.CFG. There is also **mouse steering** (INT 33h, `16e6`–`16f5`,
  used by `0e12:0751` when `DS:B6CA` ≠ 0).
* The screen shake (`0e12:0fa1`, countdown `DS:947C`, offset table `0e12:0f81`) uses the CRTC start address
  (`gfx_set_display_offset` `1776:0008`). The port must emulate it when presenting (section 4.2.9).
* The keyboard ISR does **not** chain to the BIOS. The BIOS key buffer stays empty during the game. The only key
  paths are the ISR outputs `DS:915B` (translated key), `DS:915C` (held direction bits) and `DS:9153` (Ctrl held).
  `get_key` (`0000:0f80`, game_flow) moves `DS:915B` to its caller (e.g. `DS:E08C` for `key_dispatch`).
* The picture RLE drawer `0c1c:0c45` has a sibling `0c1c:1759` that draws full-width (320) pictures **into the
  3D render buffer `DS:90D0`** with colour 0Fh transparent and no `DS:90F0` offset (caller `0792:1c00`, leg
  pictures).
* The fade-in/out routines take 16 palette uploads each. Every upload waits for the vertical blank, so a fade
  lasts about 16 frames at 70 Hz.
* 8×8 text uses a **proportional** font inside the code segment (`CS:0262 + 8·c`, widths `CS:02E2 + c`,
  characters 20h–7Fh, rows stored bottom row first).

## 1. Overview

The platform layer consists of three pieces of hand-written assembly and the C runtime:

1. **Graphics library (`16cf`–`1937`)**, a small multi-adapter library, one module per primitive. Each module
   jumps through a 21-entry table (modes 00h–14h) indexed by `DS:BD44` (= mode × 2). `gfx_set_mode` fills a
   per-mode parameter block from 9 tables at `DS:C128…C288`. The TD3 VGA build uses mode 13h: linear 320×200,
   colour = byte. Primitives: page segments, current colour, pen position, put pixel, line-to, filled rectangle,
   rectangle copies between pages, 1-bpp bitmap draw/read, CRTC display offset, EGA palette, text-screen clear,
   mouse, BIOS delay. Everything draws into the "draw page" `DS:BD88` (segment).
2. **Segment `0c1c`** (TD3-specific platform code):
   * picture drawers (RLE runs into a page `0c45`, into the 3D buffer `1759`; EGA/Tandy error-diffusion
     variant `1b8a`), a 64-step dissolve from page 1 to page 0 (`08c5`), page helpers (clear low colours,
     mirror left half, save/restore rectangles through the 3D buffer);
   * palette upload from `DS:0B6A` and fades (`0ae0`, `0b1d`, `0b5b`, `0bb7`, `0be5`, `0c16`, `0c2a`), synced to the
     vertical blank;
   * the per-frame **view presenter** `13d8` (3D buffer `DS:90D0` → screen, with a hole for the rear-view mirror,
     plus the mirror blit `17cd` after a vertical-retrace wait);
   * 8×8 proportional text (`0733`, `074c`, `076b`, `0784`) and BIOS text-mode printing (`1f32`);
   * keyboard ISR (INT 9) with its own translation tables, install/restore;
   * timer ISR (INT 8, PIT divisor 2000h = 145.65 Hz) that drives the sound driver and the tick counter
     `DS:00A0`;
   * joystick read (port 201h), DOS file wrappers, and `sfx_play` (see sound spec).
3. **`0ab4:000a`–`01f0`**: LZW dictionary allocation and the LZW decoder used for every `.LZ`-style picture.
4. **`1940`**: Microsoft C 5.1 runtime (identification only, section 2.6).

Joystick consumers (`0000:1e44` joy→key, `0000:1ea4` auto-calibration, `0e12:09d6` steering), the setup/config
screen (`0000:092a`), `get_key` (`0000:0f80`) and the message box (`0000:179c`) belong to game_flow/simulation.
They appear here only where they use platform globals.

```
_astart 1940:0018 → main 0000:0000
 ├─ getcwd 1940:07dc (drive letter for the data paths)
 ├─ config_load 0000:092a [game_flow]
 │   ├─ gfx_get_mode 16e5:000b (→ DS:E862, restored at exit)  ─ gfx_detect 16cf:0006 (→ DS:E776)
 │   ├─ gfx_set_mode 1905:000b(E862) … TD3.CFG read, or text-mode setup (print_text_bios 0c1c:1f32, getch 1940:07c4)
 │   ├─ gfx_set_mode(DS:E338 = 13h) ─ gfx_set_visible_page 171b(0) ─ gfx_alloc_page 172d(1) (fatal 1 on DOS error 8)
 │   └─ gfx_set_copy_page 170e(1) ─ gfx_set_draw_page 1714(1) → DS:90CE ─ gfx_set_draw_page(0) → DS:90CC
 ├─ kbd_install 0c1c:0e2d (INT 9 → kbd_isr 0c1c:0e98)
 ├─ snd_init 01f4:1cf0 [sound] ─ timer_install 0c1c:109f (INT 8 → timer_isr 0c1c:10e8 → snd_tick 1ace:03d6)
 ├─ mem_alloc_all 0000:11d2 [game_flow] ─ lzw_alloc 0ab4:000f, _fmalloc 1940:0681 …
 ├─ _harderr 1940:08a6(0000:081c)
 ├─ mouse_init 16e6:000e ─ mouse_show 16f5(0) ─ mouse_set_range 16ec(2,317,2,198)
 └─ state machine … (game_flow)

screens:  lzw_decode 0ab4:0047 → rle_draw_page 0c1c:0c45 (via 01f4:59ac) → page 1
          pal_* 0c1c:0c2a/0b5b/0be5… (via 01f4:1c48…1c8e) ─ dissolve 0c1c:08c5 (page 1 → 0)
          gfx_copy_rect 1818 ─ gfx_fill_rect 1785 ─ text 0c1c:0733/074c/076b/0784 (→ 16fb, 1703, 185f)
race frame: frame_update / frame_draw (render into DS:90D0, mirror into DS:90D2) → HUD (1818, 185f, 16d9, 1785)
          → view_present 0c1c:13d8 ─ mirror_present 0c1c:17cd (waits for vertical retrace) → screen
          shake: 0e12:0fa1 → gfx_set_display_offset 1776:0008
INT 8  → timer_isr 0c1c:10e8 ─ snd_tick 1ace:03d6 ─ DS:00A0++ ─ every 8th: old INT 8 (BIOS clock)
INT 9  → kbd_isr 0c1c:0e98 ─ DS:915B / 915C / 9153 (+ BIOS 0040:0017/0018/0097)
exit:  0000:07e4 / 0000:0874 → snd_shutdown (timer_restore 0c1c:10cf) ─ gfx_free_page 1736(1)
          ─ gfx_set_mode(E862) ─ text_exit_clear 16fc:000b ─ kbd_restore 0c1c:0e7d ─ [print_text_bios] ─ exit
```

## 2. Function table

### 2.1 Graphics library (`16cf`–`1937`)

Mode 13h handler = jump-table entry 19 (0x13), mode 14h = entry 20. "all" = no per-mode dispatch.

| address | proposed name | signature | one-line purpose | confidence |
|---|---|---|---|---|
| 16cf:0006 | gfx_detect | `int (void)` | Adapter detection: INT 10h AX=1A00h → 11h mono VGA/MCGA, 12h VGA colour, 13h MCGA colour; else INT 10h AH=12h BL=10h EGA → 0Dh / 0Fh (mono) / 10h (ECD switch 9); else mono (0040:0010 bits 4–5 = 30h) → 07h MDA / 0Bh Hercules (3BAh bit 7 toggling); else FC00:0000 = 21h → 09h Tandy, else 04h CGA | verified |
| 16d9:0002 | gfx_line_to | `int (int x, int y)` | Line from the pen to (x, y), both ends inclusive; horizontal/vertical → `gfx_fill_rect_clipped`, else Bresenham with `gfx_put_pixel`; pen := (x, y) | verified |
| 16e5:0007 | gfx_get_draw_seg | `u16 (void)` | Returns `DS:BD88` (segment of the draw page). Ghidra: `FUN_16d9_00c7` | verified |
| 16e5:000b | gfx_get_mode | `int (void)` | `DS:BD49` if ≥ 0, else the BIOS mode (INT 10h AH=0Fh) | verified |
| 16e6:000e | mouse_init | `int (void)` | INT 33h AX=0; 0 if no driver; else BD46 = 1, BD47 = 0, coordinate shifts BD4F/BD50 (4.2.12), returns button count (2 if BX = FFFFh) | verified |
| 16ec:0004 | mouse_set_range | `int (int x0, int x1, int y0, int y1)` | INT 33h AX=7 (x << BD4F), AX=8 (y << BD50) | verified |
| 16ef:0009 | mouse_set_pos | `int (int x, int y)` | INT 33h AX=4 with x << BD4F, y << BD50 | verified |
| 16f1:000d | mouse_get | `int (int *x, int *y, int *buttons)` | INT 33h AX=3; x = CX >> BD4F (clamped to width−1 in graphics), y = DX >> BD50, buttons = BX | verified |
| 16f5:0008 | mouse_show | `int (int on)` | INT 33h AX=1/2 when the state changes; on hide in EGA modes 0Dh–12h restores GC regs 0/1/2 | verified |
| 16fb:0008 | gfx_move_to | `int (int x, int y)` | Pen `DS:BD4B` = x, `DS:BD4D` = y | verified |
| 16fc:000b | text_exit_clear | `int (void)` | Only in a text mode (`C1D8` = 0): DOS print `ESC[2J$` (CS:0036), INT 10h AH=3; if the cursor is not at 0,0 → `gfx_clear_page` | verified |
| 16ff:000b | bios_wait_ticks | `int (int n)` | Busy-waits until INT 1Ah AH=0 low word advanced by ≥ n (18.2 Hz), 16-bit wrap handled | verified |
| 1703:0001 | gfx_set_colour | `int (int c)` | 13h/14h: `BD42` = `BD41` = c. Other modes map through `BDAC[c]` and mask to 2/4/1 bits | verified |
| 170e:0002 | gfx_set_copy_page | `int (int p)` | 13h: `BD43` = p, `BD86` = `BD8C[p & 15]`. 14h: `BD86` = A000h + p·400h | verified |
| 1714:000e | gfx_set_draw_page | `int (int p)` | 13h: `BD40` = p, `BD88` = `BD8C[p & 15]`. 14h: A000h + p·400h | verified |
| 171b:000a | gfx_set_visible_page | `int (int p)` | No-op if p = `BD4A`. 13h: swaps `BD8C[old]`/`BD8C[p]` and exchanges the 64000 bytes of the two blocks (the visible page stays at A000h), re-reads `BD88`. 14h: CRTC start = p·4000h after vretrace. TD3: called once with 0 (no-op) | verified |
| 172d:000e | gfx_alloc_page | `int (int p)` | 13h: 1 ≤ p ≤ 15: DOS 48h (page bytes / 16 = FA0h paragraphs) → `BD8C[p]`, zero-fill; returns 0, or the DOS error 7/8, or 1 for a bad p | verified |
| 1736:0004 | gfx_free_page | `int (int p)` | 13h: DOS 49h on `BD8C[p]`, then `BD8C[p]` = A000h | verified |
| 173c:0005 | gfx_put_pixel | `int (int x, int y)` | Clipped to `BD53..BD51` × `BD57..BD55`; 13h: `draw[y·320+x] = BD41` | verified |
| 173c:01ca | gfx_set_pal_reg | `(int idx, int r?, int g?, int b?)` | EGA/VGA attribute-controller palette via 3DAh/3C0h or INT 10h AX=1000h. Its jump table is addressed for CS = 1758h; no callers (dead) | likely |
| 1776:0008 | gfx_set_display_offset | `int (int x, int y)` | 13h/14h: CRTC start = y·80 + x/4 (written after a vertical-retrace edge); no pel panning (CL = FFh). Screen shake | verified |
| 1785:000b | gfx_fill_rect | `int (int x0, int x1, int y0, int y1)` | Inclusive, unclipped, colour `BD41`; 13h fills rows y1 up to y0 | verified |
| 17be:0009 | gfx_copy_rect_to_copy_page | `int (int x0, int x1, int y0, int y1)` | Same rectangle, draw page → copy page (`BD88` → `BD86`). No callers | verified |
| 17eb:0006 | gfx_copy_rect_from_copy_page | `int (int x0, int x1, int y0, int y1)` | Same rectangle, copy page → draw page (`BD86` → `BD88`); used by `scene_select_draw` (full screen) | verified |
| 1818:0003 | gfx_copy_rect | `int (int x0, int x1, int y0, int y1, int dx, int dy_bottom, int src_page, int dst_page)` | Rectangle copy between any two pages; destination given by its left x and **bottom** row | verified |
| 185f:000b | gfx_draw_bitmap | `int (u8 *bits, int bytes_per_row, int rows)` | 1-bpp, MSB = leftmost; set bits → colour `BD41`, clear bits untouched; first row at the pen y, then upward | verified |
| 18b3:0004 | gfx_read_bitmap | `int (u8 *dst, int bytes_per_row, int rows)` | Inverse: at the pen, bit = (pixel == `BD41`); rows upward | verified |
| 18f3:0002 | gfx_set_ega_palette | `int (u16 pal[16])` | EGA/Tandy 16-colour palette (INT 10h 1002h / 1012h / attribute regs). The VGA build never calls it (wrappers `01f4:1c00/1c34/1c9c` skip it for 13h) | verified |
| 1905:000b | gfx_set_mode | `int (int mode)` | Mode 00h–14h, < 0 = adopt the current BIOS mode. INT 10h / mode-14h register setup + parameter block (4.2.1) | verified |
| 192e:0005 | gfx_fill_rect_clipped | `int (int x0, int x1, int y0, int y1)` | Clips to the clip box (returns if fully outside) and calls `gfx_fill_rect` | verified |
| 1937:0003 | gfx_clear_page | `int (void)` | Clears the draw page: text modes 0720h, 13h zero-fill of FA00h bytes | verified |

### 2.2 Segment `0c1c`: graphics, palette, text

| address | proposed name | signature | one-line purpose | confidence |
|---|---|---|---|---|
| 0c1c:0733 | text_set_colours | `void (int fg, int bg)` | `DS:90E3` = fg << 8, `DS:90E1` = bg << 8 | verified |
| 0c1c:074c | text_goto_cell | `void (int row, int col)` | `DS:90E7` (u8) = row·8, `DS:90E5` = col·8 | verified |
| 0c1c:076b | text_goto | `void (int y, int col)` | `DS:90E7` = y (pixels), `DS:90E5` = col·8 | verified |
| 0c1c:0784 | text_draw_char | `void (char *c)` | One proportional 8×8 glyph at (90E5, 90E7), fg + optional opaque bg, advance x by the width | verified |
| 0c1c:08c5 | dissolve_page1_to_0 | `void (void)` | 64-step 8×8 ordered dissolve, one step per timer tick, abortable (4.3.5) | verified |
| 0c1c:0aa5 | dissolve_poll | near | `rand()` (0000:0f58), then if `DS:009F`: `get_key(&DS:90DA)` (may set skip flag `DS:008C`), else `DS:008C` = 0 | verified |
| 0c1c:0ae0 | dac_upload_256 | near | Uploads CS:0002[768] to DAC 0–255, 32 colours per vertical-blank sync | verified |
| 0c1c:0b1d | dac_upload_128 | near | Same for colours 0–127 | verified |
| 0c1c:0b5b | pal_fade_out | `void (void)` | 16 uploads of `DS:0B6A` scaled by 15…0 | verified |
| 0c1c:0b89 | page_clear_low_colours | `void (void)` | On page `DS:009A`: every byte < 80h := 0 | verified |
| 0c1c:0bb7 | pal_fade_out_low | `void (void)` | Fade out colours 0–127 only (levels 15…0) | verified |
| 0c1c:0be5 | pal_fade_in | `void (void)` | 16 uploads with levels 1…16 (16 = exact palette) | verified |
| 0c1c:0c16 | pal_black | `void (void)` | Uploads 768 zeros | verified |
| 0c1c:0c2a | pal_set | `void (void)` | Uploads `DS:0B6A` unchanged | verified |
| 0c1c:0c45 | rle_draw_page | `void (u8 *pairs, int npairs, int width, int x, int y_bottom)` | (colour + `DS:90F0`, count) runs into page `DS:009A`, bottom-up | verified |
| 0c1c:11f7 | rect_save | `void (int x0, int x1, int y0, int y1)` | 13h: copies the rectangle of page `DS:009A` into `DS:90D0:0000`, packed rows, top-down | verified |
| 0c1c:1303 | rect_restore | `void (int x0, int x1, int y0, int y1)` | Inverse of `rect_save` | verified |
| 0c1c:13d8 | view_present | `void (void)` | Per race frame: `mirror_present`, then the 3D buffer `DS:90D0` → screen page 0 (4.3.8) | verified |
| 0c1c:15c7 | page_mirror_left_half | `void (void)` | On page `DS:009A`: pixel (319−x, y) := (x, y) for x < 160 | verified |
| 0c1c:16f8 | ega_shift_copy | near | EGA/Tandy helper (nibble shift copy) | verified |
| 0c1c:170a | ega_restore_rows | near | EGA path of `rect_restore` | verified |
| 0c1c:1759 | rle_draw_viewbuf | `void (u8 *pairs, int npairs, int y_bottom)` | Full-width (320) runs into `DS:90D0`, colour 0Fh = transparent, no `DS:90F0` | verified |
| 0c1c:17cd | mirror_present | near | Waits for vertical retrace; if enabled copies the mirror image `DS:90D2` to the screen (4.3.8) | verified |
| 0c1c:1930 | view_present_ega | near | EGA path of `view_present` | likely |
| 0c1c:19e4 | ega_copy_planes | near | EGA helper | likely |
| 0c1c:1b6f | ega_gc_setup | near | EGA helper (graphics controller setup) | likely |
| 0c1c:1b8a | rle_draw_dither | `void (u8 *pairs, int npairs, int width, int x, int y, int cont)` | EGA/Tandy picture draw: maps each 256-colour run to the nearest of 16 colours with error diffusion (buffer `DS:CEBC`). Not used by VGA | likely |
| 0c1c:1e87 | dither_put | near | EGA/Tandy pixel writer of `rle_draw_dither` | likely |
| 0c1c:1f32 | print_text_bios | `void (char *s, int col, int row)` | INT 10h AH=2 cursor, AH=9 char with attribute 0Fh per character (setup screen, fatal messages) | verified |

### 2.3 Segment `0c1c`: input, timer, files, misc

| address | proposed name | signature | one-line purpose | confidence |
|---|---|---|---|---|
| 0c1c:0662 | enable_interrupts | `void (void)` | `sti` (called from the INT 24h handler 0000:081c) | verified |
| 0c1c:0664 | joy_read_raw | `u16 (void)` | Port 201h timing of axes 0/1 → `DS:00C2` (x), `DS:00CC` (y) unless a timer tick occurred; returns (~port) & 30h (buttons) | verified |
| 0c1c:06d8 | joy_read | `u16 (void)` | `joy_read_raw`, then x ≤ 4·`DS:00BE`, y ≤ 4·`DS:00C8` | verified |
| 0c1c:0702 | pit_random | `int (int add)` | Sum of 2 reads of PIT ch0 and 2 of ch2 in AH, + add. No callers | verified |
| 0c1c:071c | far_normalize | `u32 (u16 off, u16 seg)` | Returns seg + off/16 : 0. No callers | verified |
| 0c1c:084e | dos_seek | `int (int fh, u16 lo, u16 hi)` | INT 21h AX=4200h; 1 = ok, 0 = error | verified |
| 0c1c:0868 | dos_open_read | `int (char *name)` | INT 21h AX=3D00h; handle or −1 | verified |
| 0c1c:087a | dos_file_size | `u16 (int fh)` | Seek end (AX=4202h) → low word of the size, seek back to 0 | verified |
| 0c1c:08a0 | dos_read | `u16 (void far *buf, u16 n, int fh)` | INT 21h AH=3Fh (DS = buf segment); returns AX (count; error not checked) | verified |
| 0c1c:08b9 | dos_close | `void (int fh)` | INT 21h AH=3Eh | verified |
| 0c1c:0e2d | kbd_install | `void (void)` | Clears Ctrl/Alt bits of 0040:0017 and the ISR state, saves INT 9, clears port 61h bit 7, sets INT 9 = 0c1c:0e98, `DS:009D` = 1 | verified |
| 0c1c:0e7d | kbd_restore | `void (void)` | If `DS:009D`: restore INT 9 | verified |
| 0c1c:0e98 | kbd_isr | interrupt | INT 9: scan code → `DS:915B`/`915C`/`9153`, lock keys and LEDs, no BIOS chain (4.4.1) | verified |
| 0c1c:109f | timer_install | `void (void)` | Saves INT 8 at CS:109B, INT 8 = 0c1c:10e8, PIT ch0 mode 3 divisor 2000h; the intended `DS:C5E4` = 2000h store goes to 0C1C:C5E4 = code byte 185f:01b4 (DS = CS, bug; see the corrections) | verified |
| 0c1c:10cf | timer_restore | `void (void)` | PIT divisor 0 (65536), restores INT 8 | verified |
| 0c1c:10e8 | timer_isr | interrupt | `snd_tick`, `DS:00A0++`, every 8th tick `jmp far` old INT 8, else EOI | verified |
| 0c1c:1110 | sfx_play | `void (int id)` | See sound spec | verified |
| 0c1c:111d | sfx_play_ax | AX = id | See sound spec | verified |

### 2.4 LZW helpers (`0ab4`)

| address | proposed name | signature | one-line purpose | confidence |
|---|---|---|---|---|
| 0ab4:000a | nop_far | `void (void)` | Empty (`push bp / pop bp / retf`), called by `get_key` 0000:0f80 | verified |
| 0ab4:000f | lzw_alloc | `int (void)` | DOS 48h 300h paragraphs → `DS:16B8`, far ptr `DS:E7DC` = seg:0; 1 = ok, 0 = fail | verified |
| 0ab4:0034 | lzw_free | `void (void)` | DOS 49h `DS:16B8` | verified |
| 0ab4:0047 | lzw_decode | `void (void far *src, void far *dst)` | Resets the decoder state (and `DS:BD3F` = 1), then falls into the body `006a` | verified |
| 0ab4:006a | lzw_decode_body | same | Body without reset (no direct callers; entered only by fall-through) | verified |
| 0ab4:0140 | lzw_fill_input | near | Copies 400h source bytes to `DS:12B4` and advances the source pointer | verified |
| 0ab4:015c | lzw_get_code | near | Next `nbits` code, refilling the input window (4.5) | verified |
| 0ab4:01ca | lzw_reset_width | near | nbits = 9, maxcode = 200h, next = 102h | verified |
| 0ab4:01dd | lzw_out | near, AL | `*dst++ = AL` (offset only, 64 KB wrap) | verified |
| 0ab4:01e9 | lzw_idx3 | near, BX | BX := BX·3 (dictionary entry offset) | verified |
| 0ab4:01f0 | lzw_add_entry | near | dict[next] = {prefix = old, ch = firstchar}; next++ | verified |

### 2.5 Related functions outside the scope (listed for the port)

| address | name | owner | platform relevance |
|---|---|---|---|
| 0000:092a | config_load | game_flow | Video mode selection, page setup (4.6) |
| 0000:07e4 / 0000:0874 | quit / fatal(code) | game_flow | Restore order: snd_shutdown, frees, `gfx_set_mode(E862)`, `text_exit_clear`, `kbd_restore`, messages 1–5 |
| 0000:081c | crit_err_handler | game_flow | INT 24h handler via `_harderr`: `sti`, message 23h/24h, `_hardresume(1)`; code ≠ 0/2 → fatal(5) |
| 0000:0f58 | rand | game_flow | LCG ×41C64E6Dh + 3039h, returns bits 16–30. Also advanced by `dissolve_poll` |
| 0000:0f80 | get_key | game_flow | Reads/clears `DS:915B`, hotkeys, joystick → key codes (via 0000:1e44) |
| 0000:1e44 / 1ea4 | joy_to_key / joy_autocal | game_flow | Uses `joy_read`; direction bits → key codes via `DS:00AA` |
| 0000:0d62 / 0df6 | palette_load / sic_palette_load | game_flow | Use the DOS wrappers; `0d62` calls `pal_set` if VGA and not racing |
| 01f4:1c00…1c9c | pal wrappers | game_flow | `1c48` → pal_set, `1c56` → pal_black, `1c64` → fade_in, `1c72` → fade_out, `1c80` → fade_out_low, `1c8e` → page_clear_low_colours (all only if E338 = 13h); `1c00/1c34/1c9c` EGA/Tandy palette |
| 01f4:1be2 | dissolve wrapper | game_flow | Calls `dissolve_page1_to_0` |
| 01f4:59ac | picture_draw | game_flow | E338 = 13h → `rle_draw_page`, else `rle_draw_dither` |
| 0e12:0fa1 | screen_shake | simulation/render3d | Countdown `DS:947C`, offsets `0e12:0f81` → `gfx_set_display_offset`; skipped on MCGA (`DS:E776` = 13h) |
| 0e12:0751 | read_controls | simulation | `DS:915C` direction bits, mouse (`mouse_get`), joystick |
| 0e12:2537 | render_init | render3d | Sets `DS:90D0`/`DS:90D2` from the `_fmalloc` blocks |

### 2.6 Microsoft C 5.1 runtime (`1940`, identification only)

| address | function | libc / port equivalent |
|---|---|---|
| 1940:0018 | `_astart` | C start-up → `main()` |
| 1940:00de | `_cinit` | – |
| 1940:01a2 / 01b9 | `exit` / `_exit` | `exit` |
| 1940:0200 / 022d | `_ctermsub` / `_initterm` | – |
| 1940:0240 | `fclose` | `fclose` / `SDL_CloseIO` |
| 1940:0308 | `fopen` | `SDL_IOFromFile` (case-insensitive lookup) |
| 1940:0334 | `fread` | `SDL_ReadIO` |
| 1940:0526 | `fwrite` | `SDL_WriteIO` |
| 1940:066c | `_ffree` (marks a far-heap block free) | `free` |
| 1940:0681 | `_fmalloc` (+ helpers 06c0, 072e) | `malloc` |
| 1940:0746 / 0778 / 07a4 / 1424 | `strcpy` / `strcmp` / `strlen` / `strcat` | same |
| 1940:07c4 | `getch` (DOS 08h, with an unget slot `DS:C484`) | setup screen only; port: none |
| 1940:07dc / 07f2 | `getcwd` / `_getdcwd` | `SDL_GetBasePath` or the data directory |
| 1940:08a6 | `_harderr` (INT 24h hook; stub 08c9) | drop |
| 1940:08f3 | `_hardresume` | drop |
| 1940:08fc / 0998 / 09cc / 09f0 / 0a14 / 0a76 | `_aFldiv` / `_aFlmul` / `_aFNaldiv` / `_aFNauldiv` / `_aFuldiv` / `_aFulrem` | `int32_t` / `uint32_t` `/` `*` `%` (C semantics: truncation toward zero) |
| 1940:0ae2 / 0b0c / 0b32 / 0b5d | `_FF_MSGBANNER` / `_nullcheck` / `_NMSG_TEXT` / `_NMSG_WRITE` | – |
| 1940:0b88 / 0c4a / 156a | `_filbuf` / `_flsbuf` / `_getbuf` | – |
| 1940:0da8 / 0dd8 / 0ed2 / 0f42 / 18b2 | `_freebuf` / `_openfile` / `fflush` / `_getstream` / `flushall` | – |
| 1940:0f7c / 0f9c / 107a (+1122) / 15d8 / 1652 (+17e5) | `close` / `read` / `write` / `lseek` / `open` | SDL_IO |
| 1940:11a4 / 11b6 (+11ff…133e) / 1360 (+13ce) | `free` / `malloc` / `_brkctl` | `free` / `malloc` |
| 1940:1464 | `itoa` | `snprintf` |
| 1940:1480 | `bdos` (INT 21h AH, DX, AL) | – |
| 1940:1492 / 1536 / 153c | `_dos_getdir` internals / `_dosret` / `_dosmaperr` | – |
| 1940:14dc | `memcpy` (near) | `memcpy` |
| 1940:1508 | `remove` / `unlink` (INT 21h 41h) | `SDL_RemovePath` |
| 1940:17f6 / 180a | `stackavail` / `memset` | – / `memset` |
| 1940:188d | unreached | – |

## 3. Globals table

### 3.1 Graphics library state

| DS offset | proposed name | type/size | meaning | written by | read by |
|---|---|---|---|---|---|
| BD3F | lzw_mirror_dirty | u8 | 1 after `lzw_decode` (mirror buffer = dictionary overwritten); `mirror_present` skips while set | 0ab4:0047, 0e12:7b9b, 0e12:7c21 | 0c1c:17cd |
| BD40 | gfx_draw_page | u8 | Draw page number | 1714, 1905 | 171b |
| BD41 | gfx_colour | u8 | Current (mapped) colour | 1703, 1905 | all drawers |
| BD42 | gfx_colour_raw | u8 | Requested colour | 1703, 1905 | – |
| BD43 | gfx_copy_page | u8 | Copy page number | 170e, 1905 | – |
| BD44 | gfx_mode_x2 | u16 | Mode × 2 = jump-table index | 1905 | every module |
| BD46 | mouse_present | u8 | 1 after `mouse_init` found a driver | 16e6 | – |
| BD47 | mouse_visible | u8 | Cursor shown | 16e6, 16f5 | 16f5 |
| BD48 | gfx_tmp48 | u8 | Blitter temporary | 185f, 18b3 | 185f, 18b3 |
| BD49 | gfx_mode | s8 | Current mode (FFh initially = "none") | 1905 | 16e5, 1818, 16f5, 16fc |
| BD4A | gfx_visible_page | u8 | Visible page | 171b, 1905 | 171b, 16fc, 1776 |
| BD4B / BD4D | gfx_pen_x / gfx_pen_y | s16 | Pen (move_to, line_to, bitmaps) | 16fb, 16d9, 1905 | 16d9, 185f, 18b3 |
| BD4F / BD50 | mouse_shift_x / _y | u8 | INT 33h coordinate shifts (VGA 13h: 1 / 0) | 16e6 | 16ec, 16ef, 16f1 |
| BD51 / BD53 | clip_x_max / clip_x_min | s16 | Clip box (0…width−1) | 1905 | 173c, 192e |
| BD55 / BD57 | clip_y_max / clip_y_min | s16 | Clip box (0…height−1) | 1905 | 173c, 192e |
| BD59…BD5F | view_box | 4×s16 | Second box = full screen (Hercules variant differs); unused by TD3 | 1905 | – |
| BD71…BD84 | gfx_tmp | bytes | Temporaries of fill/copy/blit | 1785, 17be, 17eb, 1818, 185f, 18b3 | same |
| BD86 | gfx_copy_seg | u16 | Segment of the copy page | 170e, 1905 | 17be, 17eb |
| BD88 | gfx_draw_seg | u16 | Segment of the draw page | 1714, 171b, 1905 | all drawers, 16e5:0007 |
| BD8A | gfx_visible_seg | u16 | Visible page segment (text/CGA/14h) | 171b, 1905 | – |
| BD8C | gfx_page_seg | u16[16] | Page segments; all = video segment after set_mode; `gfx_alloc_page` fills RAM pages | 1905, 172d, 1736, 171b | 170e, 1714, 171b, 1736, 1818 |
| BDAC | gfx_colour_map | u8[256] | i mod colours (identity for 256-colour modes) | 1905 | 1703 (not in 13h) |
| BEAC | gfx_colour_expand | u16[256] | Copies of the mode's expansion table `C128` (garbage in 13h, unused) | 1905 | CGA/Tandy paths |
| C128…C288 | mode_tables | 9 × u16[22] | Per-mode parameters, entry for mode m at +2+2m; the current values are copied to the slot at +0 (4.2.1) | 1905 | all |
| C302…C30C | line_vars | 6×s16 | Bresenham steps and error terms | 16d9 | 16d9 |
| C380 | ega_levels | u8[4] | 00 2A 15 3F (EGA 2-bit → 6-bit) | – | 18f3 |
| C391 / C3AF | herc_bda / herc_crtc | | Hercules set-up data | – | 1905 |
| C3C1 | gfx_c3c1 | u16 | Cleared by set_mode | 1905 | – |
| C3C3 | gfx_pattern | u16 | Colour pattern of 2-bpp bitmap paths | 185f, 18b3 | 185f, 18b3 |
| C3D6 | text_rows | u8 | 19h (25), 1Eh for 480-line modes | 1905 | – |

Per-mode table values (entry for mode m; the 9 tables are 2Ch bytes apart):

| mode | C128 exp. table | C154 interleave | C180 colours | C1AC page bytes | C1D8 bytes/row | C204 text cols | C230 segment | C25C width | C288 height | primitives' handler |
|---|---|---|---|---|---|---|---|---|---|---|
| 00–03, 07 | 0 | 0 | 0 | 800h/1000h | 0 | 28h/50h | B800h/B000h | 28h/50h | 19h | text |
| 04, 05 / 06 | C0B0 / C0AC | 2000h | 4 / 2 | 4000h | 50h | 28h/50h | B800h | 140h/280h | C8h | CGA |
| 08 / 09 / 0A | C0B8 / C0B8 / C0B0 | 2000h/6000h | 10h/10h/4 | 4000h/8000h | 50h/A0h | 14h/28h/50h | B800h | A0h/140h/280h | C8h | PCjr/Tandy (09h = TD3 Tandy build, parked) |
| 0B / 0C | C0AC / C0B0 | 6000h | 2 / 4 | 8000h | 5Ah | 50h/28h | B000h | 2D0h/140h | 15Ch/C8h | Hercules |
| 0D / 0E | C0B8 | 0 | 10h | 2000h/4000h | 28h/50h | 28h/50h | A000h | 140h/280h | C8h | EGA (0Dh = TD3 EGA build, parked) |
| 0F–12 | C0B8/C0AC | 0 | 10h | 8000h/9600h | 50h | 50h | A000h | 280h | 15Eh/1E0h | EGA/VGA 640-wide |
| **13** | 0 | 0 | 100h | **FA00h** | **140h** | 28h | **A000h** | **140h** | **C8h** | **VGA build** |
| 14 | 0 | 0 | 100h | 4000h | 50h | 28h | A000h | 140h | C8h | unchained, unused |

Jump-table coverage (handler offset in the module; "=" means the same handler as another mode):

| module | 13h | 14h | modes with their own code |
|---|---|---|---|
| 1703 set_colour | 007c (store) | = 13h | text, CGA 4-colour, CGA 2-colour, Tandy/PCjr 16, EGA (GC set/reset) |
| 170e / 1714 pages | 001a / 0026 (RAM table) | 002c / 0038 (VRAM p·4000h) | Hercules (0016/0022), 11h/12h (0040/004c no-op) |
| 171b visible page | 006e (swap) | 00c1 (CRTC) | text (INT 10h AH=5), Hercules (3B8h) |
| 172d / 1736 alloc/free | 0027 / 0015 | no-op | CGA/Tandy/Hercules (RAM pages) |
| 173c put_pixel | 0161 | 0176 | every graphics mode |
| 1776 display offset | 0080 | = 13h | CGA/Tandy/Hercules/EGA variants |
| 1785 / 17be / 17eb / 1818 / 185f / 18b3 | 02c4 / 01f2 / 01ef / 0227 / 0453 / 030e | 02f8 / 0225 / 0222 / 0367 / 0488 / 0347 | text, CGA, Tandy, Hercules, EGA |
| 18f3 EGA palette | 004e (INT 10h 1012h) | = 13h | Tandy (0013), EGA (0032) |
| 1905 set_mode | 00e5 (BIOS) | 0080 (unchain) | Hercules (0033), CGA/Tandy (00d3: INT 43h font) |
| 1937 clear | 0027 (zero) | 005d (map mask 0Fh) | text (0720h), EGA (write mode 2) |

### 3.2 Segment `0c1c` state and tables

| address | proposed name | type/size | meaning | written by | read by |
|---|---|---|---|---|---|
| DS:0092 | joy_enabled | u16 | Joystick mode (Ctrl-J sets, Ctrl-K clears) | 0000:0f80 | 0000:1e44, 0e12:0751, 09d6, race_input |
| DS:0096 | snd_device_cfg | u16 | TD3.CFG word 1 (sound spec) | config_load | sound |
| DS:0098 | midi_flag | u16 | TD3.CFG word 2 | config_load | sound |
| DS:009A | page_cur | u16 | Page (0 = screen, 1 = RAM) for the `0c1c` drawers | game code (41 functions) | 0c1c:0b89, 0c45, 11f7, 1303, 15c7, 1e87 |
| DS:009D | kbd_installed | u8 | INT 9 hooked (`kbd_install` writes a word: 9E = 0 as well) | 0c1c:0e2d, main | 0c1c:0e7d, playdisk_load |
| DS:009F | skip_enabled | u8 | `dissolve_poll` reads keys | game_flow screens | 0c1c:0aa5 |
| DS:008C | skip_flag | u8 | Set by a key press during a dissolve/animation; aborts `dissolve_page1_to_0` | 0c1c:0aa5, get_key, screens | 0c1c:08c5, screens |
| DS:00A0 | tick_count | u16 | +1 per timer tick (145.65 Hz), wraps | timer_isr, 01f4:0070 | everything that waits; joy_read_raw |
| DS:00AA | joy_dir_keys | u8[16] | Joystick direction bits → key code (00 92 98 00 94 91 97 00 96 93 91 …) | – | 0000:1e44 |
| DS:00BC/BE/C0 | joy_x_min/ctr/max | u16 | X calibration (initial 70h/80h/90h) | 0000:179c, 1ea4 | 06d8, 1ea4, 0e12:09d6 |
| DS:00C2 / 00C4 | joy_x / joy_x_prev | u16 | X reading / previous | 0664, 06d8 / 1e44 | 1e44, 1ea4, 09d6 |
| DS:00C6/C8/CA | joy_y_min/ctr/max | u16 | Y calibration (70h/80h/90h) | 179c, 1ea4 | 06d8, 1ea4 |
| DS:00CC / 00CE | joy_y / joy_y_prev | u16 | Y reading / previous | 0664, 06d8 / 1e44 | 1e44, 1ea4 |
| DS:00EA | cfg_video_modes | u8[3] | 13h, 0Dh, 09h | – | config_load |
| DS:00EE | cfg_default_video | u8[22] | Default setup choice per `gfx_detect` result: 12h–15h → 0 (VGA), 0Dh–11h → 1 (EGA), 09h → 2 (Tandy), others → 3 (exit) | – | config_load |
| DS:0B6A | palette | u8[768] | Game palette, 6-bit RGB. Initial: EGA 16 colours at 0–15 and 128–143, rest 0 (table in 4.3.2) | palette loaders | 0c1c:0b5b…0c2a, 1b8a |
| DS:90CC | page_seg | u16[2] | `{A000h, seg(page 1)}` | config_load | 0c1c drawers, 08c5, 13d8, 17cd |
| DS:90D0 | viewbuf_seg | u16 | 320×96 3D render buffer | 0e12:2537 | 13d8, 1759, 11f7, 1303, 0e12 renderer |
| DS:90D2 | mirrorbuf_seg | u16 | Mirror image buffer (= LZW dictionary block) | 0e12:2537 | 17cd, 0e12 renderer |
| DS:90D4/D6/D8 | dissolve_saved | u16×3 | SI, DI, ES around `dissolve_poll` | 08c5 | 0aa5 |
| DS:90DA | scratch_key / dither_ptr | u16 | Key target of `dissolve_poll`; draw pointer of `rle_draw_dither` | 0aa5, 1b8a, 1e87 | same |
| DS:90DC | view_top_row | u16 | Screen row of the 3D view (initial 10h) | 01f4:144c, race_run | 13d8 |
| DS:90E0 | text_transparent | u8 | ≠ 0: no background cell | main, top_scores_screen, 0792:0602 | 0784 |
| DS:90E1 / 90E3 | text_bg / text_fg | u16 | Colour << 8 | 0733 | 0784 |
| DS:90E5 / 90E7 | text_x / text_y | u16 / u8 | Next glyph position (y = top row) | 074c, 076b, 0784, top_score_enter | 0784 |
| DS:90E8 | text_glyph | u8[8] | Glyph scratch copy | 0784 | 0784 |
| DS:90F0 | colour_offset | u8 | 0 or 80h, added to picture colours | screens, race_run | 0c45, 1b8a, palette loaders |
| DS:90F1 | dissolve_cols | u8[64] | Column phase per step: 1 0 5 4 2 6 3 7 3 2 7 6 1 5 0 4 6 5 2 0 4 3 7 1 4 1 6 3 0 7 5 2 0 6 1 7 5 2 4 3 2 7 3 5 6 4 1 0 7 4 0 2 3 1 6 5 5 3 4 1 7 0 2 6 | – | 08c5 |
| DS:9131 / 9139 / 914B | dissolve_ega_tabs | u8[8] / u8[2] / u8[8] | EGA/Tandy dissolve masks | – | 08c5 |
| DS:913B | dissolve_rows | u16[8] | Row phase × 40: 0, 120, 200, 40, 240, 160, 80, 280 (rows 0 3 5 1 6 4 2 7) | – | 08c5 |
| DS:9153 | kbd_ctrl | u8 | Ctrl held (scan 1Dh incl. E0 1Dh) | kbd_isr | kbd_isr |
| DS:9154 | kbd_old_int9 | far ptr | Saved INT 9 | kbd_install | kbd_restore |
| DS:9158 | kbd_prefix | u8 | 0, E0h, E1h (prefix pending) or EDh (LED command sent) | kbd_isr, kbd_install | kbd_isr |
| DS:9159 | kbd_last_make | u8 | Typematic filter: last make scan code (or its break code) | kbd_isr | kbd_isr |
| DS:915A | kbd_last_scan | u8 | Last accepted (pseudo) scan code incl. break bit; nobody reads it | kbd_isr | – |
| DS:915B | kbd_key | u8 | Last translated key code; consumers read and clear it | kbd_isr, readers | get_key and 15 screen/race functions |
| DS:915C | kbd_dirs | u8 | Held bits: 1 up, 2 down, 4 left, 8 right, 10h Enter, 20h Space | kbd_isr | 0e12:0751, car_select |
| DS:915D | timer_chain_cnt | u8 | Tick mod 8 | timer_isr | timer_isr |
| DS:C5E4 | pit_divisor | u16 | Meant to be 2000h; never written (the store goes to 0C1C:C5E4 = 185f:01b4) and never read; stays 0 | – (timer_install, wrong segment) | – |
| DS:E338 | video_mode | u16 | Library mode: 13h VGA, 0Dh EGA, 09h Tandy | config_load | 36 functions |
| DS:E776 | adapter | u16 | `gfx_detect` result | config_load | 0e12:0fa1 (MCGA check) |
| DS:E862 | bios_mode_at_start | u16 | Restored at exit | config_load | 0000:07e4, 0874 |
| CS:0002 (0c1c) | dac_buf | u8[768] | Upload buffer inside the code segment | fades, pal_set | 0ae0, 0b1d |
| CS:0262 (0c1c) | font_glyphs | u8[8] per char 20h–7Fh (at CS:0362…) | Bottom row first | – | 0784 |
| CS:02E2 (0c1c) | font_widths | u8 per char (chars < 20h: 0) | Advance in pixels | – | 0784 |
| CS:0CAE (0c1c) | kbd_e0_map | u8[20] | E0-prefixed scan → pseudo scan 55h/56h/60h–6Dh | – | kbd_isr |
| CS:0CC2 / 0D32 / 0DA2 (0c1c) | kbd_tab_ctrl / _normal / _shift | u8[70h] each | (pseudo) scan → key code | – | kbd_isr |
| CS:0E12 / 0E1A / 0E22 (0c1c) | kbd_dir_or / kbd_dir_and / kbd_kp_dir | u8[8], u8[8], u8[11] | Direction bit tables | – | kbd_isr |
| CS:109B (0c1c) | timer_old_int8 | far ptr | Saved INT 8 | timer_install | timer_isr, timer_restore |

### 3.3 LZW (`0ab4`)

| DS offset | proposed name | type | meaning |
|---|---|---|---|
| 12AC / 12AE | lzw_src | far ptr | Source read pointer (offset advances, no normalisation) |
| 12B0 / 12B2 | lzw_dst | far ptr | Output pointer (offset only, wraps at 64 KB) |
| 12B4 | lzw_inbuf | u8[400h] | Input window |
| 16B4 / 16B6 | lzw_inbuf_ptr | far ptr | = 1BE4:12B4 (static) |
| 16B8 | lzw_dict_seg | u16 | Dictionary segment (3 bytes per code: u16 prefix, u8 char) |
| 16BA | lzw_cur | u16 | Code being expanded |
| 16BC | lzw_old | u16 | Previous code |
| 16BE | lzw_incode | u16 | Code as read |
| 16C0 | lzw_next | u16 | Next free code (102h after a clear) |
| 16C2 | lzw_stack_n | u16 | Characters pushed on the CPU stack |
| 16C4 | lzw_nbits | u16 | 9…12 |
| 16C6 | lzw_maxcode | u16 | 1 << nbits |
| 16C8 / 16C9 | lzw_finchar / lzw_firstchar | u8 | First character of the previous / current string |
| 16CA | lzw_masks | u16[4] | 1FFh, 3FFh, 7FFh, FFFh |
| 16D2 | lzw_bitpos | u16 | Bit position in the input window |
| E7DC / E7DE | lzw_dict_ptr | far ptr | seg:0 of the dictionary (also the mirror buffer) |

## 4. Pseudocode

Port model used below:

```c
uint8_t  page[2][64000];          /* page 0 = VGA A000:0000 (shown), page 1 = RAM page */
uint8_t  viewbuf[320*96];         /* DS:90D0 */
uint8_t  mirrorbuf[0x3000];       /* DS:90D2 = LZW dictionary memory (4096 x 3 bytes) */
uint8_t  dac[768];                /* what the VGA DAC holds (6-bit) */
uint16_t crtc_start;              /* CRTC start address (screen shake) */
/* library: draw page / copy page are indices into page[] (BD8C holds only A000h and page 1) */
```

### 4.1 Mode 13h memory model

* Pixel (x, y) of a page = byte `y*320 + x`. Colour = byte (palette index). `DS:90F0` (0 or 80h) is added by the
  picture drawers only.
* Page 0 = VRAM, page 1 = the block from `gfx_alloc_page(1)`. `gfx_set_draw_page(p)` selects the target of all
  library drawers, `DS:009A` the target of the `0c1c` drawers. The game sets both together, e.g.
  `DS:009A = 0; gfx_set_draw_page(0)` (0e12:0084).
* The picture seen on the monitor = page 0 through the DAC, starting at linear offset `4·crtc_start`
  (4.2.9).

### 4.2 Graphics library

#### 4.2.1 gfx_set_mode (1905:000b)

```c
int gfx_set_mode(int mode) {
    if (mode > 0x14) return 0;
    gfx_mode = (s8)mode;                       /* BD49 */
    if (mode < 0) mode = bios_get_mode();      /* INT 10h AH=0Fh; BD49 keeps the negative value */
    switch (mode) {                            /* jump table 1905:026b */
    case 0x14: bios_set_mode(0x13); unchain(); break;   /* 0080, then common part (BD49 = 14h) */
    case 0x0b: case 0x0c: hercules_setup(); break;     /* 0033 */
    case 4: case 5: case 6: case 8: case 9: case 0x0a: set_int43(0xF000FA6E); /* fall into 00e5 */
    default: {                                  /* 00e5 */
        s8 req = gfx_mode; gfx_mode = mode;
        if (req >= 0) bios_set_mode(mode);      /* INT 10h AH=0 */
    }}
    /* common part 00f4 */
    BD44 = mode*2; BD40 = BD43 = BD4A = BD41 = BD42 = 0; C3C1 = 0; pen_x = pen_y = 0;
    colours = C180[mode];
    if (colours) { C128 = C128[mode]; for (i=0;i<256;i++) BDAC[i] = i % colours;
                   for (i=0;i<256;i++) BEAC[i] = ((u16*)C128)[i % colours]; }
    C1D8 = C1D8[mode]; C204 = C204[mode]; C3D6 = 0x19; C1AC = C1AC[mode];
    C230 = C230[mode]; BD88 = BD86 = BD8A = C230; for (i=0;i<16;i++) BD8C[i] = C230;
    C154 = C154[mode]; C25C = C25C[mode]; clip_x_min = 0; clip_x_max = C25C-1;   /* also BD59/5B */
    C288 = C288[mode]; clip_y_min = 0; clip_y_max = C288-1;                       /* also BD5D/5F */
    /* Hercules 0Ch: B800h in BD8C[1] and a different BD59..5F box; EGA 0Dh..12h: GC reg 1 = 0Fh;
       11h/12h: 30 text rows, INT 10h 1000h/1002h/1012h palette set-up. Nothing more for 13h/14h. */
    return 0;
}
```

`unchain()` (mode 14h only, never used by TD3): sequencer reg 4 bit 3 off and bit 2 on, GC reg 5 bit 4 off,
GC reg 6 bit 1 off, map mask 0Fh, zero 64 KB at A000h (all planes), CRTC reg 14h bit 6 off, reg 17h bit 6 on
("mode Y": pixel (x, y) of page p at plane x & 3, offset p·4000h + y·80 + x/4).

Port: `gfx_set_mode(0x13)` creates the window/texture and clears page 0 (BIOS mode set clears VRAM) and the DAC
to the BIOS mode-13h default palette. The game sets its own palette before anything is shown, so a black DAC
also works. `gfx_set_mode(E862)` at exit destroys the window. The port can omit the tables for other modes and keep only
the mode 13h values (width 320, height 200, row 320, page 64000, clip 0..319 × 0..199).

#### 4.2.2 Pages (170e, 1714, 171b, 172d, 1736, 16e5:0007)

```c
int gfx_set_draw_page(int p) { p &= 15; BD40 = p; BD88 = BD8C[p]; return 0; }   /* 13h path 0026 */
int gfx_set_copy_page(int p) { p &= 15; BD43 = p; BD86 = BD8C[p]; return 0; }   /* 13h path 001a */
int gfx_alloc_page(int p) {                               /* 172d, 13h path 0027 */
    if (p <= 0 || p > 15) return 1;
    seg = dos_alloc(C1AC >> 4);  if (error 7 or 8) return error;
    BD8C[p] = seg; memset(seg:0, 0, C1AC); return 0;
}
int gfx_free_page(int p) { dos_free(BD8C[p]); BD8C[p] = C230; return 0; }   /* 1736 (unless DOS error 7/9) */
int gfx_set_visible_page(int p) {                         /* 171b, 13h path 006e */
    p &= 15; if (BD4A == p) return 0;
    old = BD4A; BD4A = p; swap(BD8C[p], BD8C[old]);
    BD88 = BD8C[BD40];
    swap the C1AC bytes of the two blocks;                /* word by word */
    return 0;
}
```

TD3 sequence (config_load): `gfx_set_visible_page(0)` (no-op), `gfx_alloc_page(1)` (fatal 1 if it returns 8),
`gfx_set_copy_page(1)`, `gfx_set_draw_page(1)`, `DS:90CE = BD88`, `gfx_set_draw_page(0)`, `DS:90CC = BD88`.
At exit `gfx_free_page(1)` (0000:07e4/0874 call `1736:0004(1)` before the mode reset).

Port: `page[0]`, `page[1]` static; draw page / copy page are indices; `DS:90CC[i]` becomes `page[i]`.

#### 4.2.3 Colour, pen, pixel, line (1703, 16fb, 173c, 16d9)

```c
int gfx_set_colour(int c) { BD42 = c; BD41 = c; return 0; }          /* 13h: no mapping */
int gfx_move_to(int x, int y) { pen_x = x; pen_y = y; return 0; }
int gfx_put_pixel(int x, int y) {                                  /* signed compares */
    if (x < clip_x_min || x > clip_x_max || y < clip_y_min || y > clip_y_max) return 0;
    draw[y*320 + x] = BD41; return 0;
}
int gfx_line_to(int x, int y) {
    int x0 = pen_x, y0 = pen_y;
    if (x == x0) { pen_y = y; gfx_fill_rect_clipped(x0, x, min(y,y0), max(y,y0)); return 0; }
    if (y == y0) { pen_x = x; gfx_fill_rect_clipped(min(x,x0), max(x,x0), y, y0); return 0; }
    int ady = y - y0, sy = 1;  if (ady < 0) { sy = -1; ady = -ady; }
    int adx = x - x0, sx = 1;  if (adx < 0) { sx = -1; adx = -adx; }
    int major = adx, minor = ady, stx = sx, sty = 0;      /* straight step */
    if (adx < ady) { major = ady; minor = adx; stx = 0; sty = sy; }
    int inc_s = 2*minor, d = 2*minor - major, inc_d = 2*minor - 2*major;
    int cx = x0, cy = y0;
    for (int n = major + 1; ; ) {
        gfx_put_pixel(cx, cy);
        if (--n == 0) break;
        if (d < 0) { cx += stx; cy += sty; d += inc_s; }
        else       { cx += sx;  cy += sy;  d += inc_d; }
    }
    pen_x = cx; pen_y = cy; return 0;
}
```

The fill-rect branch passes (x0, x1, y0, y1) as (pen x, new x, min, max) for vertical lines and (min, max, new y,
pen y) for horizontal lines. `gfx_fill_rect_clipped` needs x0 ≤ x1 and y0 ≤ y1 (see below). For horizontal lines
new y = pen y, so the order does not matter.

#### 4.2.4 Filled rectangles and clear (1785, 192e, 1937)

```c
int gfx_fill_rect(int x0, int x1, int y0, int y1) {     /* 13h path 02c4; unclipped */
    int w = x1 + 1 - x0, rows = y1 + 1 - y0;             /* rows ≥ 1 assumed (loop count) */
    uint8_t *p = draw + y1*320 + x0;
    while (rows--) { memset(p, BD41, w); p -= 320; }     /* bottom row first */
    return 0;
}
int gfx_fill_rect_clipped(int x0, int x1, int y0, int y1) {   /* 192e */
    if (x0 > clip_x_max) return 0;  if (x0 < clip_x_min) x0 = clip_x_min;
    if (x1 < clip_x_min) return 0;  if (x1 > clip_x_max) x1 = clip_x_max;
    if (y0 > clip_y_max) return 0;  if (y0 < clip_y_min) y0 = clip_y_min;
    if (y1 < clip_y_min) return 0;  if (y1 > clip_y_max) y1 = clip_y_max;
    return gfx_fill_rect(x0, x1, y0, y1);
}
int gfx_clear_page(void) { memset(draw, 0, 0xFA00); return 0; }   /* 1937, 13h path */
```

`rows` = 0 or negative would make `loop` run 65536 times in the original. No TD3 caller does that. Port: treat
`rows <= 0` or `w <= 0` as nothing.

#### 4.2.5 Rectangle copies (1818, 17eb, 17be)

```c
int gfx_copy_rect(int x0, int x1, int y0, int y1, int dx, int dy_bottom, int src, int dst) {
    int w = x1 + 1 - x0, rows = y1 + 1 - y0;
    uint8_t *s = page[src] + y1*320 + x0;
    uint8_t *d = page[dst] + dy_bottom*320 + dx;
    while (rows--) { memcpy(d, s, w); s -= 320; d -= 320; }   /* forward byte copy per row */
    return 0;
}
int gfx_copy_rect_from_copy_page(int x0, int x1, int y0, int y1) {   /* 17eb: BD86 → BD88 */
    for (y = y1; y >= y0; y--) memcpy(draw + y*320 + x0, copyp + y*320 + x0, x1 + 1 - x0);
}
/* 17be: the same with the direction BD88 → BD86 (no callers) */
```

Copies go bottom row first, each row forward. With `src == dst` and overlapping rectangles the original byte
order matters: `memcpy` must be replaced by a forward byte loop (`memmove` differs when d > s within a row).
TD3 calls it with src 1 → dst 0 or within HUD areas.

#### 4.2.6 1-bpp bitmaps (185f, 18b3)

```c
int gfx_draw_bitmap(const uint8_t *bits, int bpr, int rows) {   /* 185f 13h path 0453; unclipped */
    uint8_t *row = draw + pen_y*320 + pen_x;
    while (rows--) {
        uint8_t *p = row;
        for (int i = 0; i < bpr; i++) {
            uint8_t b = *bits++;
            for (uint8_t m = 0x80; m; m >>= 1, p++) if (b & m) *p = BD41;
        }
        row -= 320;                                     /* next source row is drawn one line higher */
    }
    return 0;
}
int gfx_read_bitmap(uint8_t *dst, int bpr, int rows) {         /* 18b3 13h path 030e */
    uint8_t *row = draw + pen_y*320 + pen_x;
    while (rows--) {
        uint8_t *p = row;
        for (int i = 0; i < bpr; i++) {
            uint8_t b = 0;
            for (uint8_t m = 0x80; m; m >>= 1, p++) if (*p == BD41) b |= m;
            *dst++ = b;
        }
        row -= 320;
    }
    return 0;
}
```

The pen is not moved.

#### 4.2.7 Mouse (16e6, 16ec, 16ef, 16f1, 16f5)

```c
int mouse_init(void) {
    if (int33(0).ax == 0) return 0;              /* no driver */
    BD47 = 0; BD46 = 1;
    if (C1D8 != 0) {                             /* graphics */
        u16 q = int33(3).cx / C25C;              /* driver resets the pointer to the centre: 320/320 = 1 */
        BD4F = q & 0xff; BD50 = q >> 8;          /* x shift 1, y shift 0 in mode 13h */
    } else { BD4F = ((C204 >> 3) ^ 1) & 7; BD50 = 3; }
    int b = (s16)int33(0).bx;  return b > 0 ? b : b + 3;   /* FFFFh → 2 buttons */
}
int mouse_set_range(x0,x1,y0,y1) { int33(7, x0<<BD4F, x1<<BD4F); int33(8, y0<<BD50, y1<<BD50); }
int mouse_set_pos(x,y)           { int33(4, x<<BD4F, y<<BD50); }
int mouse_get(int *x, int *y, int *b) {
    r = int33(3); *x = r.cx >> BD4F; *y = r.dx >> BD50;
    if (C1D8 && *x >= C25C) *x = C25C - 1;     *b = r.bx;  return 0;
}
int mouse_show(int on) { on &= 1; if (on == BD47) return 0; BD47 = on; int33(2 - on); /* EGA GC restore */ }
```

(`int33(0)` is called once in the original; the button count is the BX of that call.)

TD3: `main` calls `mouse_init()`, `mouse_show(0)`, `mouse_set_range(2, 0x13d, 2, 0xc6)`. The race code centres the
pointer with `mouse_set_pos(0xa0, 100)` (0792:083c, 0792:1c66, 0e12:00d9) and reads it with
`mouse_get(&DS:E864, &DS:E86E, &DS:0104)` when `DS:B6CA` ≠ 0 (0e12:0751). The steering logic is in the
simulation spec.

Port: keep a virtual pointer in 320×200 units (x 2..317, y 2..198 after `set_range`). Update it from
`SDL_EVENT_MOUSE_MOTION` relative motion (the DOS driver's default 8 mickeys per 8 virtual pixels
horizontally, 16 per 8 vertically: 1 game pixel per 2 mickeys in x and per 2 mickeys in y) or from absolute
window coordinates mapped with `SDL_RenderCoordinatesFromWindow`. Relative mode (`SDL_SetWindowRelativeMouseMode`)
is recommended while mouse steering is active. Buttons: bit 0 left, bit 1 right.
`mouse_show` is a no-op (the cursor is never shown).

#### 4.2.8 Delays and exit helpers (16ff, 16fc)

```c
int bios_wait_ticks(int n) {                   /* n in 18.2 Hz BIOS ticks */
    u16 t0 = bios_ticks_lo();
    if (n <= 0) return 0;                      /* signed test */
    for (;;) {
        u16 t = bios_ticks_lo();
        s16 d = (t0 > t) ? (u16)(0xffff - t0 + t + 1) : (u16)(t - t0);   /* original compares signed (jg) */
        if (d >= n) return 0;                  /* signed compare jl */
    }
}
```

With TD3's timer the BIOS count advances on every 8th game tick (4.4.2), so the port defines
`bios_ticks = tick_total >> 3` (same phase as `DS:915D`). Both branches of the original compute `(t − t0) mod 65536`
(the "wrap" formula `FFFFh − t0 + t + 1` is the same value), so only the signed final compare matters, and that only
for waits longer than 32767 BIOS ticks. The port uses the u16 difference.

`text_exit_clear` (16fc): after the mode reset at exit, if the mode is a text mode, print `ESC[2J` through DOS
(needs ANSI.SYS) and clear the text page if the cursor is not at 0,0. Port: nothing.

#### 4.2.9 CRTC display offset / screen shake (1776:0008)

```c
int gfx_set_display_offset(int x, int y) {     /* 13h/14h path 0080 */
    crtc_start = y*80 + (x >> 2);              /* x unsigned >> 2 */
    wait_vretrace_end(); write CRTC 0Ch/0Dh = crtc_start;   /* no pel panning in 13h/14h */
    return 0;
}
```

In mode 13h the CRTC scans in double-word mode, so the visible picture starts at linear byte `4·crtc_start` =
`y·320 + (x & ~3)`. Pixel (px, py) on the monitor shows linear byte `4·crtc_start + py·320 + px`. Bytes 64000–65535 of the
A000 window are zero (cleared by the mode set, never written). TD3 offsets are y ≤ 8, x ≤ 8 (table `0e12:0f81`: pairs (y, x) =
(0,0) (1,1) (1,2) (3,0) (0,4) (2,2) (4,1) (5,5) (1,4) (7,2) (3,6) (4,3) (0,7) (6,1) (2,5) (8,8), indexed by the countdown),
so the largest offset is 8·320 + 8 = 2568 bytes and never goes past 65535. The shake is skipped when `DS:E776` = 13h (MCGA) and ends with (0, 0).

Port: store `crtc_start`; the presenter shows `page[0]` from offset `4·crtc_start`, bytes ≥ 64000 as colour 0
(an approximation: on real VGA, bytes past the A000 window's first 64000 bytes are VRAM that TD3 never writes,
i.e. zero after the mode set). The vertical-retrace wait is replaced by the next present.

#### 4.2.10 EGA palette, detection (18f3, 16cf) – not in the VGA build

`gfx_set_ega_palette` 13h path: converts 16 EGA colour words `rgbRGB` to 6-bit RGB through `C380 = {0, 2Ah, 15h, 3Fh}` and calls
INT 10h AX=1012h for DAC 0–15. TD3 calls it only when E338 ≠ 13h. `gfx_detect` is used only for the setup
default (`DS:00EE`) and the MCGA shake exclusion. Port: `adapter = 0x12` (VGA colour).

### 4.3 Segment `0c1c` graphics

#### 4.3.1 DAC upload (0ae0, 0b1d)

```c
static void dac_upload(int ncolours) {        /* 0ae0: 256, 0b1d: 128; source CS:0002 */
    for (int c = 0; c < ncolours; c += 32) {
        wait_vblank_start();                  /* see below */
        outb(0x3c8, c);                       /* 32 colours, R G B each */
        for (int i = c*3; i < (c+32)*3; i++) outb(0x3c9, dac_buf[i]);
    }
}
```

`wait_vblank_start`: loop { wait while 3DAh bit 3 (vertical retrace) is set; wait until 3DAh bit 0 (display
disabled) is set; CLI; re-read up to 6 more times while bit 0 stays set }. If bit 0 cleared within those reads, it
was a horizontal blank: retry. Otherwise it is the vertical blank: STI and write. So the first batch waits for the next vertical blank.
The following batches usually fit in the same blank (8 × 96 port writes ≈ 1 ms), so one upload costs about one
frame at 70 Hz.

Port: `dac_upload(n)` copies `dac_buf[0..3n)` into `dac[]`, then **waits for the next 70.086 Hz frame
boundary and presents** (`plat_vblank()`, section 6). That keeps fade durations.

#### 4.3.2 Palette functions (0b5b, 0bb7, 0be5, 0c16, 0c2a)

```c
void pal_set(void)      { memcpy(dac_buf, palette /*DS:0B6A*/, 768); dac_upload(256); }
void pal_black(void)    { memset(dac_buf, 0, 768); dac_upload(256); }
void pal_fade_out(void) { for (int l = 15; l >= 0; l--) { scale(768, l); dac_upload(256); } }
void pal_fade_out_low(void) { for (int l = 15; l >= 0; l--) { scale(384, l); dac_upload(128); } }
void pal_fade_in(void)  { for (int l = 1; l <= 16; l++) { scale(768, l); dac_upload(256); } }
static void scale(int n, int l) { for (int i = 0; i < n; i++) dac_buf[i] = (uint8_t)((palette[i]*l + 8) >> 4); }
```

`palette[i]*l` is an 8×8-bit `mul` (AX), so no overflow. Level 16 reproduces the palette exactly (values ≤ 3Fh). None of
them change `DS:0B6A`. 16 uploads each, so a fade takes about 16 frames (≈ 0.23 s).

Initial `DS:0B6A` (6-bit RGB), colours 0–15 and again 128–143, all others 0:
`000000 000028 002800 002828 280000 280028 281400 282828 141414 14143C 143C14 143C3C 3C1414 3C143C 3C3C14 3C3C3C`.
The port must embed this initial buffer (it comes from the EXE, not from a data file).

#### 4.3.3 Picture drawers (0c45, 1759)

```c
void rle_draw_page(const uint8_t *pr, int npairs, int width, int x, int y) {   /* 0c45 */
    uint8_t *p = page[DS_009A] + y*320 + x;      /* y = bottom row; y*256 + y*64 */
    unsigned acc = 0;
    while (npairs--) {
        uint8_t c = (uint8_t)(pr[0] + DS_90F0); unsigned n = pr[1]; pr += 2;
        acc += n;
        while (acc > (unsigned)width) {           /* run crosses the row end */
            acc -= width;  unsigned k = n - acc;  /* pixels left in this row */
            memset(p, c, k); p += k;
            p -= 320 + width;                     /* start of the row above */
            n = acc;
        }
        memset(p, c, n); p += n;                  /* acc == width leaves p at the row end; the next run wraps lazily */
    }
}
```

Counts are bytes (0–255). A count of 0 draws nothing. The wrap is lazy: a run that ends exactly at the row end leaves `acc =
width` and `p` at the end of the row, and the next run does the wrap with k = 0. No clipping. Rows above y = 0 would wrap in the
64 KB segment, but no picture does that.

```c
void rle_draw_viewbuf(const uint8_t *pr, int npairs, int y) {   /* 1759: x = 0, width 320, target DS:90D0 */
    uint8_t *p = viewbuf + y*320;  unsigned acc = 0;
    while (npairs--) {
        uint8_t c = pr[0]; unsigned n = pr[1]; pr += 2;  acc += n;
        if (c == 0x0f) {                                /* transparent: skip */
            if (acc > 320) { acc -= 320; p += n - acc; p -= 640; n = acc; }
            p += n;
        } else {
            if (acc > 320) { acc -= 320; unsigned k = n - acc; memset(p, c, k); p += k; p -= 640; n = acc; }
            memset(p, c, n); p += n;
        }
    }
}
```

`1759` handles only one wrap per run (`if`, not `while`), which is enough for a 320-wide picture (count ≤ 255). `p -= 640` moves from
the end of the row to the start of the row above. Caller `0792:1c00` (leg pictures; see render3d/hud).

`rle_draw_dither` (1b8a) and `dither_put` (1e87) are the EGA/Tandy versions (error diffusion to 16 colours using
`DS:0B6A`, error rows at `DS:CEBC`, 3C0h bytes each). Not needed for VGA.

#### 4.3.4 Page helpers (0b89, 15c7, 11f7, 1303)

```c
void page_clear_low_colours(void) { uint8_t *p = page[DS_009A]; for (i = 0; i < 64000; i++) if (p[i] < 0x80) p[i] = 0; }
void page_mirror_left_half(void) {                       /* 15c7, 13h path */
    uint8_t *p = page[DS_009A];
    for (y = 0; y < 200; y++) for (x = 0; x < 160; x++) p[y*320 + 319 - x] = p[y*320 + x];
}
void rect_save(int x0, int x1, int y0, int y1) {         /* 11f7, 13h path: top-down, packed */
    int w = x1 + 1 - x0; uint8_t *d = viewbuf;
    for (y = y0; y <= y1; y++) { memcpy(d, page[DS_009A] + y*320 + x0, w); d += w; }
}
void rect_restore(int x0, int x1, int y0, int y1) { /* 1303: inverse, same layout */ }
```

`page_mirror_left_half` copies word by word with a byte swap, which gives the per-pixel mirror above.
`rect_save` stores its data in the 3D render buffer, so the saved area is lost once the renderer draws again. The only caller is the message box
`0000:179c`, which restores it before the next frame.

#### 4.3.5 Dissolve page 1 → page 0 (08c5)

```c
void dissolve_page1_to_0(void) {
    dissolve_poll();                          /* first poll before step 0 */
    if (DS_008C) return;
    for (int step = 0; step < 64; step++) {   /* VGA path 08f6 */
        int col = DS_90F1[step];
        int row = DS_913B[step & 7] / 40;     /* 0 3 5 1 6 4 2 7 */
        u16 t0 = tick_count;
        for (int y = row; y < 200; y += 8)
            for (int x = col; x < 320; x += 8)
                page[0][y*320 + x] = page[1][y*320 + x];
        dissolve_poll();
        if (DS_008C) return;                  /* key pressed: abort, rest not copied */
        while (tick_count == t0) plat_idle(); /* one step per timer tick */
    }
}
```

Each step copies 1/64 of the pixels. The (column, row) phases cover all 64 combinations. The whole dissolve takes 64
ticks ≈ 0.44 s. The wait is done after the step and its poll. `dissolve_poll` (0aa5) calls `rand()` (0000:0f58, the value is
discarded but the LCG advances, so random sequences depend on it); then if `DS:009F` = 0 it sets `DS:008C` = 0, otherwise it calls
`get_key(&DS:90DA)`, which may set `DS:008C`.

#### 4.3.6 Text (0733, 074c, 076b, 0784)

```c
void text_set_colours(int fg, int bg) { text_fg = fg; text_bg = bg; }         /* stored << 8 */
void text_goto_cell(int row, int col) { text_y = (uint8_t)(row*8); text_x = col*8; }
void text_goto(int y, int col)        { text_y = (uint8_t)y;       text_x = col*8; }
void text_draw_char(const char *s) {                                           /* draws s[0] only */
    uint8_t c = (uint8_t)s[0];
    if (c >= 0x20) {
        memcpy(text_glyph, font_glyphs + 8*(c - 0x20), 8);      /* CS:0262 + 8c; byte 0 = bottom row */
        gfx_move_to(text_x, text_y + 7);
        gfx_set_colour(text_fg); gfx_draw_bitmap(text_glyph, 1, 8);
        gfx_set_colour(text_bg);
        if (!text_transparent) { for (i = 0; i < 8; i++) text_glyph[i] ^= 0xff; gfx_draw_bitmap(text_glyph, 1, 8); }
    }
    text_x += font_widths[c];                                    /* CS:02E2 + c; 0 for c < 20h */
}
```

The background cell is always 8 pixels wide. With a proportional advance the next glyph overwrites the right part of the
previous cell, so drawing order matters. After the call the library colour stays at `bg`. Characters ≥ 80h would read past
the tables (code bytes), and the game never prints them.

Font (96 glyphs, 20h–7Fh): 8 bytes each, MSB = left pixel, byte 0 = bottom row. Widths for 20h…7Fh:
`8 4 7 8 8 8 8 6 6 6 8 8 5 7 4 8 | 7 6 7 6 8 7 7 6 7 7 4 5 8 8 8 7 | 8 8 8 7 8 6 6 8 8 4 5 8 6 8 8 8 |
8 8 8 7 8 8 8 8 8 8 8 8 8 8 8 8 | 8 7 7 7 7 7 6 7 7 4 5 8 5 8 7 7 | 7 8 6 6 6 7 7 8 8 7 8 8 4 8 8 8`.
The glyphs are part of the EXE (image `0xC522`–`0xC821`, widths `0xC4C2`–`0xC521`). The port embeds them as a generated table
(dump from `work/TDIII_unp.exe`, segment 0c1c offsets 0362–0661 and 0302–0361).

`print_text_bios(s, col, row)` (1f32): INT 10h AH=2 (page 0, row, col), then for each character AH=9 attribute 0Fh count 1 and
col + 1. It is used only in text mode (setup screen, fatal messages). Port: `SDL_Log`/stderr and `SDL_ShowSimpleMessageBox` for fatal
errors. The setup dialog is replaced by defaults (section 4.6).

#### 4.3.7 View present and mirror (13d8, 17cd) – mode 13h path

```c
void view_present(void) {                              /* 0c1c:13d8 */
    mirror_present();
    int words = (((DS_BA95 >> 5) + 1) >> 1);          /* words per row (view width/2) */
    int xoff  = 0xa0 - words;                          /* centring, bytes */
    int skip  = 2*xoff;                                /* bytes skipped per row in dst and src */
    int row0  = DS_90DC;                               /* screen row of the view */
    int rows  = DS_BA91;                               /* view height */
    int vc    = (0x60 - rows) >> 1;                    /* unsigned >> 1 of a 16-bit difference */
    if (DS_09C4) row0 += vc;                           /* vertical centring */
    int top = 0x15;
    if (DS_BA82 == 0) { top = min(DS_BAD4, DS_BAD6); DS_BAD6 = DS_BAD4; }   /* signed min */
    int si = 0;
    top -= vc;
    if (top >= 0) {                                     /* skip unchanged top rows */
        rows -= top; if (rows <= 0) return;
        row0 += top; si = top*320;
    }
    uint8_t *d = page[0] + row0*320 + xoff;           /* always page 0 (DS:90CC[0]) */
    const uint8_t *s = viewbuf + si;
    int remaining = rows;
    if (DS_CC92 == 0 && (DS_BA91 - remaining) < 14) {  /* rows under the mirror */
        int n = 14 - (DS_BA91 - remaining);
        while (n--) {                                  /* full-width rows with an 88-pixel hole */
            if (DS_B6DC) { cpy(d,s,128); cpy(d+216,s+216,24); }          /* hole 128..215, 240..319 not copied */
            else         { cpy(d,s,168); cpy(d+256,s+256,64); }          /* hole 168..255 */
            d += 320; s += 320; remaining--;
        }
    }
    while (remaining--) { memcpy(d, s, 2*words); d += 2*words + skip; s += 2*words + skip; }
}
```

The mirror rows ignore `xoff` in their row length (they assume a full-width view). The source rows of the render buffer are
320 bytes apart, and the view is left-aligned there. The globals `DS:BA95` (view width × 32), `DS:BA91`, `DS:BAD4/BAD6`,
`DS:BA82`, `DS:09C4`, `DS:CC92`, `DS:B6DC` belong to render3d/hud, which also define their meaning. The hole rows
count down both the hole counter (DH) and the remaining rows (DL), as in the original. If DL reached 0 inside the hole
loop, the final `dec dl / jne` loop would run 255 more rows. That cannot happen while the view is taller than 14 rows.
The port can guard it.

```c
static void mirror_present(void) {                     /* 0c1c:17cd */
    wait_vretrace();                                   /* until 3DAh bit 3 set (returns at once if already in retrace) */
    if (DS_BD3F || DS_CC92) return;
    if (!DS_B6D2) { if (!DS_B6D1) return; DS_B6D1 = 0; }
    /* 13h path 182b: trapezoid, source rows 88 bytes apart */
    static const struct { int dst, src, n; } top5[5] = {
        {11*320+192, 0x18,  40}, {12*320+176, 0x60,  72}, {13*320+174, 0xB6,  76},
        {14*320+173, 0x10D, 78}, {15*320+172, 0x164, 80} };
    for (i = 0; i < 5; i++) memcpy(page[0] + top5[i].dst, mirrorbuf + top5[i].src, top5[i].n);
    for (r = 0; r < 14; r++) memcpy(page[0] + (16+r)*320 + 168, mirrorbuf + 0x1b8 + r*88, 88);
}
```

The mirror occupies screen x 168–255, rows 11–29. The source offsets are copied as the original computes them
(`si` advances by the copy length plus 20h, 0Eh, 0Bh, 09h). They correspond to an 88-byte-wide image of 19 rows with a
slanted top.

Port: `wait_vretrace()` = `plat_vblank()` (wait for the next 70 Hz boundary and present). The race loop
presents once per frame at this point. The original writes to VRAM while the beam runs, and TD3 only paces with the retrace
wait, so tearing is not reproduced.

### 4.4 Input and timer

#### 4.4.1 Keyboard ISR (0c1c:0e98)

BIOS data used: `bda17` = 0040:0017 (bit 0 RShift, 1 LShift, 4 ScrollLock, 5 NumLock, 6 CapsLock), `bda18` = 0040:0018 (lock keys held),
`bda97` = 0040:0097 (LED bits). The port keeps these three bytes in the platform state, initialised to 0.

```c
void kbd_install(void) {                 /* 0e2d */
    bda17 &= 0xf3;                        /* clear Ctrl/Alt */
    kbd_prefix = kbd_last_make = kbd_last_scan = kbd_key = kbd_dirs = 0;
    save INT 9; port61 &= 0x7f; set INT 9 = kbd_isr; kbd_installed = 1;
}

void kbd_isr(uint8_t sc) {               /* sc = port 60h byte */
    /* ack: port 61h bit 7 pulse (port: nothing) */
    if (kbd_prefix == 0xED) {             /* byte after the LED command (normally ACK FAh) */
        uint8_t led = (bda17 >> 4) & 7;  bda97 = (bda97 & 0xf8) | led;
        send_to_kbd(led);  kbd_prefix = 0;  /* then continue with this byte */
    }
    uint8_t ah = sc, al = sc;
    if (al >= 0xE0) { if (al == 0xE0 || al == 0xE1) kbd_prefix = al; goto eoi; }   /* FAh etc. ignored */
    al &= 0x7f;
    if (al == 0x1d || al == 0x61) kbd_ctrl = (ah & 0x80) ? 0 : 1;     /* before prefix handling */
    if (al == 0x2a || al == 0x36) {       /* shift keys */
        if (kbd_prefix == 0xE0) { kbd_prefix = 0; goto eoi; }            /* fake shift */
        uint8_t bit = (al == 0x2a) ? 2 : 1;
        if (ah & 0x80) bda17 &= ~bit; else bda17 |= bit;
    }
    if (kbd_prefix == 0xE1) {             /* Pause: E1 1D 45 / E1 9D C5 */
        if (al == 0x1d) goto eoi;         /* prefix stays E1 */
        if (al != 0x45) { kbd_prefix = 0; goto eoi; }
        ah++; al++;  kbd_prefix = 0xE0;   /* 45h → 46h, then handled as E0 46h */
    }
    if (kbd_prefix == 0xE0) {             /* grey keys → pseudo scan codes */
        uint8_t i = al - 0x1c;
        if (i >= 2) { i -= 0x17; if (i >= 6) i -= 0x0d; }   /* 8-bit unsigned arithmetic */
        al = kbd_e0_map[i];                /* CS:0CAE, index < 20 for real E0 codes */
        if (al == 0) { kbd_prefix = 0; goto eoi; }
        ah = (ah & 0x80) | al;  kbd_prefix = 0;
    }
    /* typematic filter */
    if (!(ah & 0x80)) { if (al == kbd_last_make) goto eoi; kbd_last_make = ah; }
    else if (al == kbd_last_make) kbd_last_make = ah;
    /* lock keys */
    uint8_t lk = (al == 0x3a) ? 0x40 : (al == 0x45) ? 0x20 : (al == 0x46) ? 0x10 : 0;
    if (lk) {
        if (!(ah & 0x80)) { if (!(bda18 & lk)) { bda18 |= lk; bda17 ^= lk; send_to_kbd(0xED); kbd_prefix = 0xED; } }
        else bda18 &= ~lk;
    }
    if (!(ah & 0x80)) {                   /* translate make codes */
        const uint8_t *t = kbd_ctrl ? kbd_tab_ctrl : (bda17 & 3) ? kbd_tab_shift : kbd_tab_normal;
        uint8_t k = t[ah];                /* ah < 70h */
        if ((bda17 & 0x20) && k >= 0x91 && k <= 0x99)  /* NumLock */
            k -= 0x70;                    /* → 21h..29h ('!'..')'), sic. The following "k == 1Eh → +10h"
                                             (Del → '.') is only reached for k ≥ 91h, so it is dead code */
        if ((bda17 & 0x40) && ((k >= 'A' && k <= 'Z') || (k >= 'a' && k <= 'z'))) k ^= 0x20;   /* CapsLock */
        kbd_key = k;
    }
    kbd_last_scan = ah;
    /* held direction bits */
    uint8_t b = ah & 0x7f, d = kbd_dirs;
    if (b == 0x60 || b == 0x1c)      { d = (ah & 0x80) ? d & ~0x10 : d | 0x10; }   /* Enter / KP Enter */
    else if (b == 0x39)              { d = (ah & 0x80) ? d & ~0x20 : d | 0x20; }   /* Space */
    else {
        if (b == 0x29) b = 0x48;       /* ` acts as Up   */
        if (b == 0x2b) b = 0x4b;       /* \ acts as Left */
        uint8_t i = b - 0x47;  if (i & 0x80) goto eoi;     /* js: no store */
        if (i <= 10) i = kbd_kp_dir[i];                    /* keypad 47h..51h → 1Fh..26h (4Ch → B9h: none) */
        i -= 0x1f;  if ((i & 0x80) || i >= 8) goto eoi;   /* js, then unsigned jae */
        d = (ah & 0x80) ? d & kbd_dir_and[i] : d | kbd_dir_or[i];
    }
    kbd_dirs = d;
eoi: /* out 20h, 20h */;
}
```

The typematic filter drops repeated make codes of the key pressed last, so a held key produces one `kbd_key`. The direction bits
are updated for every accepted make/break. The `bda17` shift bits are updated before the filter.

Direction tables: `kbd_kp_dir` (index scan−47h) = `1F 20 21 25 22 B9 23 23 24 25 26`, `kbd_dir_or` = `05 01 09 04 08 06 02 0A`,
`kbd_dir_and` = `FA FE F6 FB F7 F9 FD F5`. Index 0…7 = Home, Up, PgUp, Left, Right, End, Down, PgDn (grey keys 66h–6Dh map directly).
Keypad 47h–51h: 7 = up-left, 8 = up, 9 = up-right, − = **down**, 4 = left, 5 = none, 6 = right, + = **right**, 1 = down-left,
2 = down, 3 = down-right. Bits: 1 up, 2 down, 4 left, 8 right, 10h Enter, 20h Space.

E0 pseudo scan codes: E0 1C (KP Enter) → 60h, E0 1D (R Ctrl) → 61h, E0 35 (KP /) → 62h, E0 37 (PrtSc) → 63h, E0 38 (R Alt) → 64h,
E0 46 (Break / Pause) → 65h, E0 47–49 → 66h–68h, E0 4B → 69h, E0 4D → 6Ah, E0 4F–51 → 6Bh–6Dh, E0 52 (Ins) → 55h, E0 53 (Del) → 56h.

Key codes (`DS:915B`, normal / shift / ctrl where they differ):

| scan | normal | shift | ctrl | scan | normal | shift | ctrl |
|---|---|---|---|---|---|---|---|
| 01 Esc | 80 | 80 | 80 | 02–0B | '1'…'0' | `!@#$%^&*()` | digits |
| 0C 0D | `-` `=` | `_` `+` | `-` `=` | 0E Bksp / 0F Tab | 08 / 09 | same | same |
| 10–19 | qwertyuiop | QWERTYUIOP | q→11, e→16, p→12, others lower case | 1A 1B | `[` `]` | `{` `}` | `[` `]` |
| 1C Enter | 0D | 0D | 0D | 1E–26 | asdfghjkl | ASDFGHJKL | a→17, s→13, d→18, j→14, k→15 |
| 27 28 | `;` `'` | `:` `"` | `;` `'` | 29 \` | 92 | `~` | 92 |
| 2A LShift | 01 | 01 | 01 | 2B \\ | 94 | `\|` | 94 |
| 2C–32 | zxcvbnm | ZXCVBNM | lower case | 33 34 35 | `,` `.` `/` | `<` `>` `?` | `,` `.` `/` |
| 36 RShift | 05 | 05 | 05 | 37 KP* | 1A | 1A | 1A |
| 38 Alt | 02 | 02 | 02 | 39 Space | 20 | 20 | 20 |
| 3A Caps | 0C | 0C | 0C | 3B–44 F1–F10 | 81–8A | same | same |
| 45 NumLk / 46 ScrLk | 0B / 0A | same | same | 47 48 49 | 91 92 93 | same | same |
| 4A KP− | 98 | 1D | 98 | 4B 4C 4D | 94 95 96 | same | same |
| 4E KP+ | 96 | 1B | 96 | 4F 50 51 | 97 98 99 | same | same |
| 52 Ins / 53 Del | 10 / 1E | same | same | 54 SysRq / 55 / 56 | 0F / 90 / 9E | same | same |
| 57 F11 / 58 F12 | 8B / 8C | same | same | 59 / 5A | 8D / 8E | same | same |
| 60 KP Enter | 0D | | | 61 R Ctrl | 00 | | |
| 62 KP / | 1F | | | 63 PrtSc | 0E | | |
| 64 R Alt | 06 | | | 65 Pause | 8F | | |
| 66–6D grey nav | 91 92 93 94 96 97 98 99 | | | 55 / 56 grey Ins / Del | 90 / 9E | | |

(`kbd_ctrl` also applies to the pseudo codes; the three tables are identical for scans ≥ 36h except 4Ah/4Eh.) So the game's
arrow codes are 91h–99h in keypad layout (92h up, 98h down, 94h left, 96h right). Esc = 80h, F-keys 81h–8Ch, Ctrl-J = 14h,
Ctrl-K = 15h, Ctrl-Q = 11h, Ctrl-S = 13h, Ctrl-P = 12h, Ctrl-A = 17h, Ctrl-D = 18h, Ctrl-E = 16h (meanings: game_flow
`get_key`, simulation `key_dispatch`).

Port: map `SDL_EVENT_KEY_DOWN`/`UP` (`event.key.scancode`) to the XT byte sequence the keyboard would send and feed each byte
to `kbd_isr()` in event order: normal keys `sc` / `sc|80h`; grey keys (arrows, Home, End, PgUp, PgDn, Ins, Del, KP Enter, KP /,
R Ctrl, R Alt) `E0 sc` / `E0 sc|80h`; Pause (down only) `E1 1D 45 E1 9D C5`; Print Screen `E0 2A E0 37` / `E0 B7 E0 AA` (the
fake shift bytes are dropped by the E0 shift rule). SDL key-repeat events feed the make code again (dropped by the typematic
filter, as on real hardware). After `kbd_isr` "sends" EDh, feed a synthetic `FA` byte before the next real byte. Lock
state starts at 0. Scan-code table: as in `../TestDrive2/port/spec/platform.md` section 6 (XT set 1), plus the E0 keys above.
Clear `bda17` bits 0–1 when the window loses focus (otherwise a shift released outside the window sticks; the original has no
such case).

#### 4.4.2 Timer (0c1c:109f, 10cf, 10e8)

```c
void timer_install(void) { save INT 8 in CS:109B; INT 8 = timer_isr; PIT 43h = 36h;
                           /* *(u16 far *)MK_FP(0x0c1c, 0xc5e4) = 0x2000;  DS = CS: patches 185f:01b4, see corrections */
                           40h = 00h, 20h; }
void timer_restore(void) { PIT 43h = 36h; 40h = 0, 0; restore INT 8; }
void timer_isr(void) {                    /* 145.65 Hz = 1193182 / 8192 */
    snd_tick();                           /* 1ace:03d6, sound spec; runs with interrupts off */
    tick_count++;                         /* DS:00A0, u16 */
    timer_chain_cnt = (timer_chain_cnt + 1) & 7;
    if (timer_chain_cnt == 0) jump_far(old_int8);   /* BIOS clock at 18.2 Hz; it sends the EOI */
    else outb(0x20, 0x20);
}
```

`timer_install` is called from `snd_init` (01f4:1cf0) after the sound device set-up, and `timer_restore` from `snd_shutdown`
(01f4:1e9a).

Port: no interrupts. A tick accumulator driven by `SDL_GetTicksNS()`:
`due = elapsed_ns · 1193182 / (8192 · 10⁹)`. Each tick runs `snd_tick()` and `tick_count++` (and `bios_ticks` every 8th).
Ticks are advanced in `plat_idle()`/`plat_vblank()` and in every busy-wait (see 6). The sound spec may move `snd_tick` into the
audio callback for sample-exact timing. Then the tick counter must be advanced by the same clock, so the game logic and music
stay in step.

#### 4.4.3 Joystick (0c1c:0664, 06d8)

```c
u16 joy_read_raw(void) {
    u16 t0 = tick_count;
    outb(0x201, any);                    /* fire the one-shots */
    int cx = -8, si;  u8 mask = 3, v;
    do { v = inb(0x201) & 3; cx += 8; if (overflow) { cx = si = -1; goto done; } } while (v == 3);
    si = cx;
    if (v == 0) goto done;                /* both axes ended together */
    if (v & 2) {                          /* X ended (cx), Y still running in si */
        do { v = inb(0x201) & 2; si += 8; if (overflow) { si = -1; goto done; } } while (v);
    } else {                              /* Y ended (si), X still running in cx */
        do { v = inb(0x201) & 1; cx += 8; if (overflow) { cx = -1; goto done; } } while (v);
    }
done:
    cx >>= 3; si >>= 3;                   /* arithmetic shifts: count of port reads - 1 */
    if (tick_count == t0) { joy_x = cx; joy_y = si; }   /* discard readings disturbed by the timer IRQ */
    return (~inb(0x201)) & 0x30;          /* button 1 = 10h, button 2 = 20h */
}
u16 joy_read(void) {
    u16 b = joy_read_raw();
    if (joy_x > 4*joy_x_ctr) joy_x = 4*joy_x_ctr;      /* unsigned; a timeout (FFFFh) becomes 4·centre */
    if (joy_y > 4*joy_y_ctr) joy_y = 4*joy_y_ctr;
    return b;
}
```

The counts depend on CPU speed and port timing. The consumers auto-calibrate (min/centre/max, `0000:1ea4`) and compare against
fractions of the ranges.

Port: `SDL_Gamepad` (or `SDL_Joystick`). Synthesise counts on the default calibration scale:
`joy_x = 0x80 + axis·0x80/32768` (range 0…0x100, centre 0x80, then clamped by `joy_read` to 4·centre), same for y, buttons South
→ 10h, East → 20h. The game's min/max tracking then settles on 0…0x100. With `joy_enabled` = 0 (default) the joystick is not read.

### 4.5 LZW decoder (0ab4:0047)

```c
void lzw_decode(const uint8_t far *src, uint8_t far *dst) {
    BD3F = 1;                                             /* mirror buffer (= dictionary) is now garbage */
    next = 0x102; stack_n = 0; nbits = 9; maxcode = 0x200; bitpos = 0;
    lzw_src = src; lzw_dst = dst; lzw_fill_input();        /* first 400h bytes */
    for (;;) {
        u16 code = lzw_get_code();
        if (code == 0x101) return;                         /* end */
        if (code == 0x100) {                               /* clear */
            nbits = 9; maxcode = 0x200; next = 0x102;
            code = lzw_get_code();
            cur = old = code; finchar = firstchar = (u8)code;
            lzw_out((u8)code);                             /* literal, no dictionary entry */
            continue;
        }
        cur = incode = code;
        if ((s16)code >= (s16)next) { cur = old; push(finchar); }   /* KwKwK */
        while (cur > 0xff) { push(dict[cur].ch); cur = dict[cur].prefix; }
        finchar = firstchar = (u8)cur; push((u8)cur);
        while (stack_n) lzw_out(pop());                   /* stack_n reset to 0 */
        dict[next].ch = firstchar; dict[next].prefix = old; next++;
        old = incode;
        if (next >= maxcode && nbits != 12) { nbits++; maxcode <<= 1; }
    }
}
u16 lzw_get_code(void) {
    u16 pos = bitpos; bitpos += nbits;
    u16 byte = pos >> 3, bit = pos & 7;
    if (byte >= 0x3fd) {                                  /* keep the tail, append new source bytes */
        memmove(inbuf, inbuf + byte, 0x400 - byte);
        copy `byte` bytes from lzw_src to inbuf + 0x400 - byte; lzw_src += byte;
        bitpos = bit + nbits; byte = 0;
    }
    u32 w = inbuf[byte] | inbuf[byte+1] << 8 | (u32)inbuf[byte+2] << 16;
    return (u16)(w >> bit) & lzw_masks[nbits - 9];
}
```

* Codes LSB first, 9–12 bits. The width grows right after the entry that makes `next == maxcode` is added (as in FORMATS.md).
  At 12 bits `next` keeps growing past FFFh: the original then writes dictionary entries after the 12288-byte block
  (memory corruption). FORMATS.md's decoder verified every shipped picture, so this never happens there. Port: stop adding at 4096.
* The dictionary entry is 3 bytes {u16 prefix, u8 char} at `dict_seg:code·3`. Output pointer and source pointer advance
  by offset only (a 64 KB wrap would stay in the segment; no picture reaches 64 KB of output).
* The decoder reads the source in 1 KB blocks and can read up to 1 KB past the end of the compressed data (harmless in DOS). The port
  should read bits directly from the source array and return 0 bits past its end.
* `lzw_alloc` (0ab4:000f) is called once from `mem_alloc_all` (0000:11d2). The block is also the mirror image buffer
  `DS:90D2`.

### 4.6 Configuration and start-up (config_load 0000:092a, owner game_flow)

1. `DS:E862 = gfx_get_mode()` (BIOS mode at start), `DS:E776 = gfx_detect()`, default choice = `DS:00EE[E776]`,
   `gfx_set_mode(E862)`.
2. `fopen("TD3.CFG", "rb")`. If found: video word → `DS:E338 = DS:00EA[video]` (13h/0Dh/09h), audio word → `DS:0096`,
   MIDI word → `DS:0098`. Format in `port/formats/descriptions.md`.
3. If missing: text-mode dialog with `print_text_bios` and `getch`: graphics choice 1–4 (4 = exit to DOS), sound choice 1–5
   (default Tandy 3-voice when Tandy graphics was chosen, else PC speaker; MIDI → `DS:0098` = 1 plus a warning), "Shall I save
   these settings for next time?" Y/N (default N) → `fopen("TD3.CFG", "wb+")`, three `fwrite`s of 2 bytes.
4. Audio 3 (MT-32) → `sfx_off` = 1, device = 81h (sound spec). `DS:0090` = 0.
5. `gfx_set_mode(E338)`, pages (4.2.2).

Port: read TD3.CFG if present. A missing file or a non-VGA / non-AdLib choice → VGA and AdLib (4) with a log line. No
text dialog, no write-back (or an optional command-line setup). `DS:E776` = 12h. Keyboard installation, `snd_init`
(timer), `mem_alloc_all`, `render_init` follow in `main`.

## 5. File formats

* **TD3.CFG**: see `port/formats/descriptions.md` (3 × u16 LE). Mapping of the video word: 0 → mode 13h, 1 → 0Dh,
  2 → 09h (`DS:00EA`). Read with `fread(…, 2, 1, f)` per word; a short file leaves the rest unchanged.
* **Pictures**: LZW (4.5) + RLE pairs (4.3.3). Formats in FORMATS.md.
* **Palettes** (`*COL*.BIN`): read by game_flow with the DOS wrappers into `DS:0B6A + 3·(16 + DS:90F0)`, 150h bytes, then
  `pal_set` (FORMATS.md).
* **Embedded data the port must carry** (from the EXE, not from files): font glyphs and widths (4.3.6), the initial palette
  `DS:0B6A` (4.3.2), keyboard tables (4.4.1), dissolve tables (3.2), mirror copy geometry (4.3.7).

DOS wrapper semantics (used by the archive loader `0000:151c`, `0000:0d62/0df6/0e74/0ee0`, game_flow):

| wrapper | DOS | port |
|---|---|---|
| `dos_open_read(name)` | 3D00h → handle / −1 | `SDL_IOFromFile(path, "rb")`, case-insensitive name lookup in the game directory; drive prefix `A:` of the built paths is ignored |
| `dos_seek(fh, lo, hi)` | 4200h → 1 / 0 | `SDL_SeekIO(io, (hi<<16)\|lo, SDL_IO_SEEK_SET)` |
| `dos_file_size(fh)` | 4202h then 4200h 0 → low 16 bits of the size | `(u16)SDL_GetIOSize(io)`, position 0 |
| `dos_read(buf, n, fh)` | 3Fh → count | `SDL_ReadIO` |
| `dos_close(fh)` | 3Eh | `SDL_CloseIO` |

## 6. Hardware / DOS dependencies and SDL3 replacements

| Original | Where | SDL3 / portable replacement |
|---|---|---|
| INT 10h AH=0 mode 13h, AH=0Fh | 1905, 16e5 | Create window + renderer + 320×200 streaming texture (`SDL_PIXELFORMAT_XRGB8888`); `SDL_SetRenderLogicalPresentation(320, 240 or 200, SDL_LOGICAL_PRESENTATION_LETTERBOX)` (4:3 aspect with 240, as TD2 port) |
| VRAM A000h (page 0) | all drawers | `uint8_t page[0][64000]`; presented by converting through `dac[]` (6-bit → 8-bit: `v<<2 \| v>>4`) |
| DOS 48h/49h RAM page | 172d, 1736 | static `page[1]` |
| DAC 3C8h/3C9h + 3DAh blank sync | 0ae0, 0b1d | copy into `dac[]`, then `plat_vblank()` |
| 3DAh vertical-retrace wait | 17cd, 1776, 171b | `plat_vblank()`: advance ticks, pump events, wait until the next 1/70.086 s boundary, present |
| CRTC 0Ch/0Dh start address | 1776 | `crtc_start` offset applied by the presenter (4.2.9) |
| Sequencer/GC/CRTC registers of mode 14h, EGA paths | library, 0c1c EGA code | not ported (mode 14h unused; EGA/Tandy parked) |
| INT 9 hook, port 60h/61h, PIC EOI, BIOS 0040:0017/0018/0097, LED command EDh | 0e2d, 0e7d, 0e98 | SDL key events → XT byte sequences → `kbd_isr()` (4.4.1); LEDs dropped |
| INT 8 hook, PIT 43h/40h divisor 2000h | 109f, 10cf, 10e8 | tick accumulator at 1193182/8192 Hz (4.4.2) |
| Chain to the BIOS INT 8 every 8th tick | 10e8 | `bios_ticks = tick_total >> 3` |
| INT 1Ah AH=0 | 16ff | `bios_ticks` |
| Port 201h | 0664 | `SDL_Gamepad` (4.4.3) |
| INT 33h AX=0/1/2/3/4/7/8 | 16e6–16f5 | virtual pointer from SDL mouse events (4.2.7) |
| INT 21h 3Dh/3Eh/3Fh/42h | 084e–08b9 | SDL_IO (section 5) |
| INT 21h 25h/35h (vectors 8, 9, 24h) | 0e2d, 109f, 1940:08a6 | drop |
| INT 10h AH=2/9 text output, DOS 09h `ESC[2J`, `getch` | 1f32, 16fc, 1940:07c4 | `SDL_Log`; fatal → `SDL_ShowSimpleMessageBox` + exit code |
| INT 10h AX=1A00h, AH=12h, BIOS 0040:0010/0487, FC00:0000, 3BAh | 16cf | constant 12h (VGA) |
| PIT ch0/ch2 reads for a random seed | 0702 (no callers) | drop |
| Code-segment variables (CS:0002 DAC buffer, CS:109B old INT 8) | 0c1c | ordinary statics |
| Busy-wait loops on `DS:00A0` (dissolve, race frame pacing, menus) and `bios_wait_ticks` | 0c1c, game code | every iteration calls `plat_idle()`: advance due ticks, pump SDL events into `kbd_isr`/mouse/gamepad, present page 0, and sleep ~1 ms when nothing is due. Never spin |

Platform API proposed for the port (names only; the game code calls these where the original waits or touches hardware):

```c
void     plat_idle(void);        /* ticks + events + present if page 0 or dac changed */
void     plat_vblank(void);      /* as plat_idle, then wait for the next 70.086 Hz boundary and present */
uint16_t plat_ticks(void);       /* DS:00A0 */
void     plat_present(void);     /* page[0] from 4*crtc_start through dac[] */
```

The original has no single "present" call: page 0 is the screen. Presenting from `plat_idle` and `plat_vblank`
covers every visible change, because the game always waits (ticks, retrace, key) before the picture has to be seen. An
optional "present at most once per 1/70 s" throttle in `plat_idle` saves work.

## 7. Timing

* **Tick = 1193182 / 8192 = 145.652 Hz** (6.866 ms), set by `snd_init` and kept for the whole run. `DS:00A0` is a u16
  (wraps after 7.5 min), and all waits compare differences.
* Per tick: `snd_tick` (music, effects, sound spec), `tick_count++`, every 8th tick the BIOS clock (18.2065 Hz).
* **Race frames**: in each frame, `view_present` → `mirror_present` waits for a vertical retrace (70.086 Hz,
  ≤ 14.3 ms) before copying, and at the end of the frame `race_run` waits until ≥ 5 ticks (34.3 ms) have passed since the frame
  started (RE_GUIDE). If the work plus the retrace wait fits in 5 ticks, the period is 5 ticks (29.1 fps), and the copy to the
  screen falls on the first retrace after the drawing, so the present times jitter between 2 and 3 retrace periods apart. On slow
  machines the period grows. The port does the same: `plat_vblank()` inside `view_present`, then the tick wait. Simulation
  and sound are in ticks or frames (see simulation spec).
* Palette uploads: one vertical blank each; fades = 16 uploads ≈ 16 frames = 0.23 s. `pal_set`/`pal_black` ≈ 1 frame.
* Dissolve: 64 steps, one per tick = 0.44 s, abortable by a key when `DS:009F` is set.
* Screen shake: one table entry per call of `0e12:0fa1` (per frame), each with a retrace wait (a second frame sync while
  shaking, since `gfx_set_display_offset` waits for the end of a retrace edge). Port: apply the offset at the next present
  and do not add an extra frame wait unless measured otherwise (open question 9).
* `bios_wait_ticks(n)`: n × 8 game ticks (−7…0 ticks, depending on the phase).
* Keyboard: asynchronous in the original. `kbd_key` holds only the **last** make code since it was last read.
  `kbd_dirs` is sampled once per frame by `0e12:0751`. The port feeds SDL events in order at each `plat_idle()`, so a press
  and release within one frame still sets and clears the bits in the right order (a tap shorter than a frame is lost for
  direction bits, as in the original).
* Joystick reading time grows with the stick position (up to ~2 ms on the original). Irrelevant in the port.

## 8. Differences from Test Drive (1987) / Test Drive II

TD3 shares no library code with TD1/TD2 (RE_GUIDE), so none of the TD1 platform port can be reused as is. What carries over
is the port skeleton (window, texture presentation, tick accumulator, key-event → scan-code feeding, gamepad, file lookup,
headless snapshot mode).

| Topic | TD1 / TD2 | TD3 |
|---|---|---|
| Video | EGA mode 0Dh, planar, 16 colours, sprite blitters with plane maps | VGA mode 13h, linear 256 colours; pictures are LZW + RLE runs; 3D view rendered into a RAM buffer and copied |
| Palette | fixed EGA palette | 256-colour DAC, fades by scaling, `DS:90F0` 128-colour halves |
| Pages | one screen + RAM buffers (descriptors) | page 0 = VRAM, page 1 RAM, 3D buffer 320×96, mirror buffer |
| Timer | 99.9985 Hz (TD2), routine list, BIOS every 5 ticks | 145.65 Hz, fixed ISR (snd_tick only), BIOS every 8 ticks |
| Keyboard | INT 9 + INT 16h hook, key-down table, one-key buffer | INT 9 only, one-key buffer `DS:915B` + held-direction bits `DS:915C`, own lock/LED handling, typematic filter |
| Joystick | adaptive `joy_read`, calibration screen | simple port-201h timing, discards readings disturbed by the timer, auto-calibration in game_flow |
| Mouse | none | INT 33h steering |
| Text | 8×8 fixed | 8×8 proportional font in the code segment |
| Unpacker | Huffman + RLE `.PES` | LZW 9–12 bits (+ RLE runs) |

## 9. Open questions

1. `view_present` semantics of `DS:BA95/BA91/BAD4/BAD6/BA82/09C4/CC92/B6DC` and why the `B6DC` variant leaves x = 240–319 of
   the top 14 view rows uncopied while the mirror is always drawn at x = 168: render3d/hud must confirm (cockpit variants?).
2. Exact frame pacing of `gfx_set_display_offset` during the shake (retrace-end wait + `mirror_present`'s retrace wait can cost
   an extra frame). Measure in DOSBox if frame-exact shake timing matters.
3. `dac_upload` batch timing depends on the VGA's blank length and I/O speed. One frame per upload is assumed.
4. NumLock translation (91h–99h → 21h–29h) looks like a bug (digits would be 31h–39h). It is faithful to keep it. With lock
   state initialised to 0 the port never triggers it unless the player presses NumLock.
5. `DS:00AA` joystick direction table: index 0Ah (down+right) gives 91h (Home/up-left) instead of 99h. Data bug in game_flow's
   joystick-to-key path. Keep (faithful), or flag in game_flow.
6. `gfx_set_pal_reg` (173c:01ca) and its module linked at segment 1758 are dead. Not needed.
7. `rle_draw_dither` (EGA/Tandy) was identified only at the structural level (parked with EGA/Tandy).
8. Scan code 61h in the Ctrl test of `kbd_isr` never comes from a real keyboard (the pseudo code 61h is produced only after the
   test). Harmless.
