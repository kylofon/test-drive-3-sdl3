"""Render Test Drive III .LZ pictures to PNG (needs work/res from `td3res.py extract`).

.LZ = LZW (td3res.lzw_decode) of (colour, count) byte pairs; count 0 is never used. Runs fill a
`width`-pixel-wide rectangle from its bottom row upwards, left to right, wrapping into the next row
(0c1c:0c45, VGA path). The draw call adds DS:90F0 (0 or 80h) to every colour.

Palettes (*COLR.BIN, *COL.BIN, SCENExxn.COL, 337 bytes) are 112 6-bit RGB triples + FFh, loaded by
0000:0d62 at colour 16 + DS:90F0 of the palette buffer DS:0B6A (256 x RGB), i.e. colours 16..127 or
144..255; colours 0..15 (and anything not loaded) come from the executable's initial buffer.

usage: td3img.py [OUTDIR]     all pictures -> OUTDIR (default work/img) + contact sheet sheet.png
"""
import glob, os, struct, sys
from PIL import Image
import td3res

ROOT = td3res.ROOT
RES = os.path.join(ROOT, 'work', 'res')

# Widths passed to the draw call (01f4 call sites); others are found by fit_width().
WIDTHS = {'ACCO': 320, 'TITLE1': 160, 'TITLE2': 160, 'TITLEANI': 320, 'TITLELET': 240,
          'TITLEL2': 256, 'TITLECAR': 128, 'CREDITA': 320, 'CREDITB': 320, 'CREDITC': 320,
          'TOPSCORA': 320, 'TOPSCORB': 320, 'TOPSCORC': 320, 'SELECT': 320,
          'DIFFLEVA': 320, 'DIFFLEVB': 320, 'DIFFLEVC': 320}
# By extension (0792:0922: .ETC 38h wide; L.BOT / R.BOT A8h).
EXT_WIDTHS = {'.ETC': 0x38}
# Picture -> palette file (same directory unless a path is given).
PALETTES = [('ACCO', 'ACCOCOLR.BIN'), ('TITLE1', 'TITLCOLR.BIN'), ('TITLE2', 'TITLCOLR.BIN'),
            ('TITLE', 'TITLCOLR.BIN+TITL2COL.BIN'), ('CREDIT', 'CREDCOLR.BIN'), ('COP', 'COPCOLR.BIN'),
            ('KEYS', 'KEYCOLR.BIN'), ('SELECT', '../DATAC/SELCOLR.BIN'),
            ('DIFFLEV', '../DATAC/DIFFCOLR.BIN'), ('TOPSCOR', 'TOPCOLR.BIN'),
            ('SSBJ', '../DATAC/SELCOLR.BIN')]


def base_palette():
    img = td3res.image()
    p = td3res.DGROUP * 16 + 0x0B6A
    return list(img[p:p + 768])


def load_palette(path):
    """path: palette file, or 'A+B' = A at colour 16 and B at colour 144 (DS:90F0 = 80h);
    'F@64' loads F at colour 64 (the 32-colour *SIC.BIN, 0000:0df6 -> DS:0C2A = colour 64 of DS:0B6A)."""
    pal = base_palette()
    d0 = os.path.dirname(path)
    for k, name in enumerate(os.path.basename(path).split('+')):
        name, _, start = name.partition('@')
        f = os.path.join(d0, name)
        if os.path.exists(f):
            d = open(f, 'rb').read()[:336 if not start else 96]
            at = (int(start or 16) + 0x80 * k) * 3
            pal[at:at + len(d)] = d
    return [min(255, v * 255 // 63) for v in pal]


def palette_for(path):
    d, name = os.path.split(path)
    stem = os.path.splitext(name)[0]
    if name.endswith(('.ALZ', '.BLZ')):
        return os.path.join(d, stem + '.COL')
    if stem.endswith(('FL1', 'FL2')):
        return os.path.join(d, stem[:-3] + 'COL.BIN')
    car = os.path.basename(d)
    if car.startswith('C') and car in td3res.CARS:        # drawn at +80h into the car's palette
        return os.path.join(d, car + {'.ICN': 'SC.BIN', '.BIC': 'SC.BIN', '.SID': 'SC.BIN',
                                      '.SIC': 'SIC.BIN@64'}.get(os.path.splitext(name)[1], 'COL.BIN'))
    for prefix, pal in PALETTES:
        if stem.startswith(prefix):
            return os.path.normpath(os.path.join(d, pal))
    return os.path.join(RES, 'DATAC', 'OTWCOL.BIN')     # in-game pictures


def pixels(data):
    raw = td3res.lzw_decode(data)
    out = bytearray()
    for i in range(0, len(raw) - 1, 2):
        out += bytes([raw[i]]) * raw[i + 1]
    return out


# Widths the game passes to the draw call anywhere (01f4, 0792, 0e12 call sites).
CALL_WIDTHS = {0x38, 0x48, 0x70, 0x80, 0x98, 0xa0, 0xa8, 0xb8, 0xd0, 0xf0, 0xf8, 0x100, 0x140}


def fit_width(px):
    """Width with the most similar adjacent rows among the exact divisors of the pixel count
    (multiples of 4, 16..320); widths used by the game's draw calls win ties."""
    best = None
    for w in range(16, 321, 4):
        rows = len(px) // w
        if len(px) % w or rows < 2:
            continue
        idx = range(0, (rows - 1) * w, max(1, (rows - 1) * w // 4000))
        score = sum(px[i] != px[i + w] for i in idx) / len(idx) - (0.02 if w in CALL_WIDTHS else 0)
        if best is None or score < best[0]:
            best = (score, w)
    return best[1] if best else len(px)


def render(path):
    px = pixels(open(path, 'rb').read())
    stem = os.path.splitext(os.path.basename(path))[0]
    ext = os.path.splitext(path)[1]
    w = WIDTHS.get(stem) or (208 if stem.endswith(('FL1', 'FL2')) else 0) or EXT_WIDTHS.get(ext) or         (168 if stem.endswith(('L', 'R')) and ext == '.BOT' else 0) or (320 if len(px) % 320 == 0 and len(px) >= 320 * 10 else fit_width(px))
    h = len(px) // w
    im = Image.frombytes('P', (w, h), bytes(px[:w * h])).transpose(Image.FLIP_TOP_BOTTOM)
    im.putpalette(load_palette(palette_for(path)))
    return im


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'work', 'img')
    os.makedirs(out, exist_ok=True)
    ims = []
    for path in sorted(glob.glob(os.path.join(RES, '*', '*'))):
        if os.path.getsize(path) < 16:
            continue                                        # 7-byte placeholders
        try:
            im = render(path)                               # any LZW+RLE picture, whatever its name
        except (ValueError, KeyError, IndexError, ZeroDivisionError):
            continue
        if im.height < 4:
            continue                                        # data that happens to decode as LZW
        name = os.path.basename(os.path.dirname(path)) + '_' + os.path.basename(path).replace('.', '_') + '.png'
        im.save(os.path.join(out, name))
        ims.append((name, im.convert('RGB')))
        print('%-28s %3dx%3d' % (name, im.width, im.height))
    cols, cw, ch = 4, 330, 225
    sheet = Image.new('RGB', (cols * cw, ((len(ims) + cols - 1) // cols) * ch), (40, 40, 40))
    from PIL import ImageDraw
    dr = ImageDraw.Draw(sheet)
    for k, (name, im) in enumerate(ims):
        x, y = (k % cols) * cw + 5, (k // cols) * ch + 14
        sheet.paste(im, (x, y))
        dr.text((x, y - 12), '%s %dx%d' % (name, im.width, im.height), fill=(255, 255, 0))
    sheet.save(os.path.join(out, 'sheet.png'))


if __name__ == '__main__':
    main()
