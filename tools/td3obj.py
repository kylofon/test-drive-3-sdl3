"""Parse and render Test Drive III polygon objects (see port/formats/objects.md).

Object sets (all little-endian):
  SCENETTO.BIN  (scene "O" set, DS:E54C)  64-entry word table of offsets, then objects. Index = the
                object type (A477 & 3Fh): 0..3 are cars (.POB), 4 unused, 5..63 roadside objects
                (4-byte header, drawn by 0e12:72f8) and traffic vehicles (8-byte header with a far
                LOD, drawn by 0e12:539d / 52a3).
  C<car>.POB    one vehicle object with 8-byte header (player car at DS:CEA4, opponents DS:CEBC / D7A4).
  SCENETTT.BIN / <scene>T.BIN  road tiles (4-byte header, children = sprite placements).
  SCENETTP.BIN  (scene "P" set, DS:CE9E) traffic lane paths per tile type (not polygons; `paths`).

Object = header, vertex arrays A[n] (height, up), B[n], C[n] (horizontal; B = along a car, C = across),
16-bit signed; then f faces of 4 words: vertex index in bits 0-10 of each word,
w0 bits 14-15 = vertex count - 1, w0 bit 13 = sort by farthest vertex, w0 bits 11-12 = point size,
w1/w2 bits 11-15 = colour codes c1/c2 (dither pair), w3 bits 11-15 = face type (lights, animation).
Colour pair: codes < 16 -> word table DS:B4B9[(c2 << 4) | c1] (leg .DAT offset 1F27h), codes >= 16 ->
DS:B6B9[c & 15] (leg .DAT offset 2127h); low byte on even pixels, high byte on odd (checkerboard).
Pixel values index the palette buffer: 0..15 from the executable, 16..127 = OTWCOL.BIN.

usage:
  python tools/td3obj.py                 render everything -> work/obj/ (sheets + one PNG per object)
  python tools/td3obj.py dump FILE [IDX]  print the parsed object(s) of FILE (.BIN set or .POB)
  python tools/td3obj.py stats           face-field statistics over all sets
  python tools/td3obj.py paths           dump SCENETTP.BIN lane paths
options: --dat PATH  leg .DAT for the colour tables (default work/res/SCENE01/SCENE01A.DAT)
"""
import collections, math, os, struct, sys
import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import td3res, td3img

ROOT = td3res.ROOT
RES = os.path.join(ROOT, 'work', 'res')
OUT = os.path.join(ROOT, 'work', 'obj')
GAME = os.path.join(ROOT, 'Game')

POINT_SIZE = (13, 2, 9, 8)          # DS:95D9, indexed by w0 bits 11-12 (0e12:66ad)
TYPE_NAMES = {0: 'or-mode?', 2: 'normal', 4: 'lamp(red/white)', 5: 'lamp(white/off)',
              6: 'lamp(yellow)', 7: 'blink', 8: 'strobe', 9: 'brake light', 10: 'siren',
              11: 'traffic light', 12: 'flasher', 13: 'anim v0/v1'}


# ----------------------------------------------------------------------------------- parsing
class Obj:
    pass


def parse(d, o, hdr):
    """Parse the object at offset o; hdr = 4 (roadside object / tile) or 8 (vehicle)."""
    ob = Obj()
    ob.off, ob.hdr = o, hdr
    ob.nf, ob.nv, ob.nc, ob.nk = d[o], d[o + 1], d[o + 2], d[o + 3]
    ob.lod = None
    p = o + hdr
    ob.verts = read_verts(d, p, ob.nv)
    p += 6 * ob.nv
    ob.faces = [struct.unpack_from('<4H', d, p + 8 * i) for i in range(ob.nf)]
    p += 8 * ob.nf
    ob.children = [struct.unpack_from('<H3h', d, p + 8 * i) for i in range(ob.nc)]
    p += 8 * ob.nc
    ob.coll, ob.anim = [], []
    ob.end = p
    if hdr == 8:
        ob.coll = [struct.unpack_from('<3h2B', d, p + 8 * i) for i in range(ob.nk)]
        p += 8 * ob.nk
        lf, ln, loff = d[o + 4], d[o + 5], struct.unpack_from('<H', d, o + 6)[0]
        lp = o + 8 + loff
        nanim = (lp - p) // 12 if lp > p else 0
        ob.anim = [struct.unpack_from('<6h', d, p + 12 * i) for i in range(nanim)]
        ob.lod_info = (lf, ln, loff)
        if loff:
            lod = Obj()
            lod.nf, lod.nv, lod.nc, lod.nk = lf, ln, 0, 0
            lod.verts = read_verts(d, lp, ln)
            lod.faces = [struct.unpack_from('<4H', d, lp + 6 * ln + 8 * i) for i in range(lf)]
            lod.children = lod.coll = lod.anim = []
            ob.lod = lod
            ob.end = lp + 6 * ln + 8 * lf
        else:                                               # far LOD = first lf faces / ln vertices
            ob.end = p + 12 * nanim
    return ob


def read_verts(d, p, n):
    a = struct.unpack_from('<%dh' % n, d, p)
    b = struct.unpack_from('<%dh' % n, d, p + 2 * n)
    c = struct.unpack_from('<%dh' % n, d, p + 4 * n)
    return list(zip(a, b, c))


def size4(d, o):
    return 4 + 6 * d[o + 1] + 8 * d[o] + 8 * d[o + 2]


def size8(d, o):
    lf, ln, loff = d[o + 4], d[o + 5], struct.unpack_from('<H', d, o + 6)[0]
    if loff == 0:
        return 8 + 6 * d[o + 1] + 8 * (d[o] + d[o + 2] + d[o + 3])
    return 8 + loff + 6 * ln + 8 * lf


def load_set(path):
    """Word offset table + objects. Header kind is not stored: the game knows it from how the
    object is used; here it is the one whose size reaches the next object exactly."""
    d = open(path, 'rb').read()
    tab, first = [], len(d)
    while 2 * len(tab) < first:                             # table ends where the first object starts
        v = struct.unpack_from('<H', d, 2 * len(tab))[0]
        tab.append(v)
        if v >= 0x11:
            first = min(first, v)
    offs = sorted(set(v for v in tab if v >= 0x11))
    ends = dict(zip(offs, offs[1:] + [len(d)]))
    objs = {}
    for o in offs:
        gap = ends[o] - o
        if size8(d, o) == gap and not size4(d, o) == gap:
            hdr = 8
        elif size4(d, o) in (gap, gap - 1):                 # last object: trailing FFh
            hdr = 4
        else:
            hdr = 8 if size8(d, o) in (gap, gap - 1) else 4
        objs[o] = parse(d, o, hdr)
    return d, tab, objs


def load_pob(path):
    d = open(path, 'rb').read()
    return d, parse(d, 0, 8)


# ----------------------------------------------------------------------------------- colours
def colour_tables(dat):
    d = open(dat, 'rb').read()
    return list(struct.unpack_from('<256H', d, 0x1F27)), list(d[0x2127:0x2137])


def face_pair(w, pair_tab, direct):
    """(even pixel, odd pixel) palette indices of a face (0e12:7487, VGA branch; E5B0 = identity)."""
    c1, c2 = w[1] >> 11, w[2] >> 11
    if not (c1 & 16) and not (c2 & 16):
        v = pair_tab[(c2 << 4) | c1]
        return v & 255, v >> 8
    lo = direct[c1 & 15] if c1 & 16 else c1
    hi = direct[c2 & 15] if c2 & 16 else c2
    return lo, hi


# ----------------------------------------------------------------------------------- rendering
VIEWS = {'3/4': (35, 25), 'side': (90, 0), 'front': (0, 0), 'top': (90, 90)}


def project(v, yaw, pitch):
    """Object (A=up, B, C) -> screen (x, y, depth). yaw 0 looks along -B (front view)."""
    a, b, c = v
    ya, pa = math.radians(yaw), math.radians(pitch)
    x = b * math.sin(ya) - c * math.cos(ya)          # +C is to the viewer's left (sign text reads right)
    z = c * math.sin(ya) + b * math.cos(ya)          # towards viewer
    y = a * math.cos(pa) - z * math.sin(pa)
    dz = a * math.sin(pa) + z * math.cos(pa)
    return x, -y, dz


def render(ob, size, pal, tabs, yaw, pitch, bounds=None):
    pair_tab, direct = tabs
    pts = [project(v, yaw, pitch) for v in ob.verts]
    if not pts:
        return Image.new('RGB', (size, size), (24, 24, 32))
    used = set()
    for w in ob.faces:
        if w[3] >> 11 == 0 and len(ob.faces) > 1:
            continue                                        # OR-mode beams: keep out of the framing
        for k in range((w[0] >> 14) + 1):
            used.add(w[k] & 0x7FF)
    use = [pts[i] for i in used if i < len(pts)] or pts
    if bounds is None:
        xs, ys = [p[0] for p in use], [p[1] for p in use]
        bounds = (min(xs), min(ys), max(xs), max(ys))
    x0, y0, x1, y1 = bounds
    s = (size - 12) / max(x1 - x0, y1 - y0, 1)
    ox, oy = size / 2 - (x0 + x1) / 2 * s, size / 2 - (y0 + y1) / 2 * s
    even = Image.new('P', (size, size), 0)
    odd = Image.new('P', (size, size), 0)
    de, do = ImageDraw.Draw(even), ImageDraw.Draw(odd)
    mask = Image.new('L', (size, size), 0)
    dm = ImageDraw.Draw(mask)
    order = []
    for w in ob.faces:
        nv = (w[0] >> 14) + 1
        idx = [w[k] & 0x7FF for k in range(nv)]
        if any(i >= len(pts) for i in idx):
            continue
        ds = [pts[i][2] for i in idx]
        depth = min(ds) if w[0] & 0x2000 else sum(ds) / nv   # 0x2000: farthest vertex (0e12:361c)
        order.append((depth, w, idx))
    order.sort(key=lambda t: t[0])
    beams = [t for t in order if t[1][3] >> 11 == 0]
    order = [t for t in order if t[1][3] >> 11 != 0]
    for depth, w, idx in order:
        lo, hi = face_pair(w, pair_tab, direct)
        xy = [(ox + pts[i][0] * s, oy + pts[i][1] * s) for i in idx]
        if len(xy) == 1:
            r = max(1.0, POINT_SIZE[(w[0] >> 11) & 3] * s / 2)
            box = [xy[0][0] - r, xy[0][1] - r, xy[0][0] + r, xy[0][1] + r]
            for dr, c in ((de, lo), (do, hi), (dm, 255)):
                dr.ellipse(box, fill=c)
        elif len(xy) == 2:
            for dr, c in ((de, lo), (do, hi), (dm, 255)):
                dr.line(xy, fill=c, width=1 + ((w[0] >> 11) & 1))
        else:
            for dr, c in ((de, lo), (do, hi), (dm, 255)):
                dr.polygon(xy, fill=c)
    e, o_ = np.array(even), np.array(odd)
    yy, xx = np.indices(e.shape)
    px = np.where((xx + yy) & 1, o_, e).astype(np.uint8)
    rgb = np.array(pal, dtype=np.uint8).reshape(256, 3)[px]
    bg = np.array([24, 24, 32], dtype=np.uint8)
    rgb[np.array(mask) == 0] = bg
    if beams:        # face type 0: OR-ed into the frame (41d4), skipped for colour 707h by day
        bm = Image.new('L', (size, size), 0)
        db = ImageDraw.Draw(bm)
        for depth, w, idx in beams:
            xy = [(ox + pts[i][0] * s, oy + pts[i][1] * s) for i in idx]
            if len(xy) >= 3:
                db.polygon(xy, fill=90)
            elif len(xy) == 2:
                db.line(xy, fill=90)
        a = np.array(bm, dtype=np.uint16)[..., None]
        rgb = ((rgb.astype(np.uint16) * (255 - a) + 255 * a) // 255).astype(np.uint8)
    return Image.fromarray(rgb, 'RGB')


def obj_panel(ob, pal, tabs, label, big=220, small=108):
    """3/4 view large + side/front/top (+ far LOD 3/4 if present)."""
    w = big + small * 2 + 6
    im = Image.new('RGB', (w, big + 16), (40, 40, 40))
    im.paste(render(ob, big, pal, tabs, *VIEWS['3/4']), (0, 16))
    for k, name in enumerate(('side', 'front', 'top')):
        im.paste(render(ob, small, pal, tabs, *VIEWS[name]), (big + 3 + (k % 2) * (small + 3), 16 + (k // 2) * (small + 3)))
    if ob.lod is not None:
        im.paste(render(ob.lod, small, pal, tabs, *VIEWS['3/4']), (big + 3 + small + 3, 16 + small + 3))
    dr = ImageDraw.Draw(im)
    dr.text((2, 2), label, fill=(255, 255, 0))
    if ob.lod is not None:
        dr.text((big + small + 8, small + 20), 'far LOD', fill=(160, 160, 160))
    return im


def sheet(panels, cols, path):
    if not panels:
        return
    pw, ph = panels[0].size
    sh = Image.new('RGB', (cols * pw, ((len(panels) + cols - 1) // cols) * ph), (40, 40, 40))
    for k, p in enumerate(panels):
        sh.paste(p, ((k % cols) * pw, (k // cols) * ph))
    sh.save(path)
    print('wrote', os.path.relpath(path, ROOT))


def describe(ob):
    s = 'f=%d v=%d' % (ob.nf, ob.nv)
    if ob.hdr == 8:
        s += ' coll=%d anim=%d lod=%d/%d' % (ob.nk, len(ob.anim), ob.lod_info[0], ob.lod_info[1])
    elif ob.nc:
        s += ' sprites=%d' % ob.nc
    return s


def render_all(dat):
    os.makedirs(OUT, exist_ok=True)
    pal = td3img.load_palette(os.path.join(RES, 'DATAC', 'OTWCOL.BIN'))
    tabs = colour_tables(dat)
    # O set
    d, tab, objs = load_set(os.path.join(RES, 'DATAB', 'SCENETTO.BIN'))
    panels, seen = [], {}
    os.makedirs(os.path.join(OUT, 'O'), exist_ok=True)
    for i, o in enumerate(tab):
        if o in seen:
            continue
        seen[o] = i
        ob = objs[o]
        p = obj_panel(ob, pal, tabs, 'O %d @%04X %s %s' % (i, o, 'veh' if ob.hdr == 8 else 'obj', describe(ob)))
        p.save(os.path.join(OUT, 'O', 'O%02d.png' % i))
        panels.append(p)
    sheet(panels, 4, os.path.join(OUT, 'SCENETTO_sheet.png'))
    # cars
    panels = []
    for car in td3res.CARS:
        path = os.path.join(GAME, car + '.POB')
        if not os.path.exists(path):
            continue
        d, ob = load_pob(path)
        p = obj_panel(ob, pal, tabs, '%s.POB %s' % (car, describe(ob)))
        p.save(os.path.join(OUT, car + '.png'))
        panels.append(p)
    sheet(panels, 3, os.path.join(OUT, 'POB_sheet.png'))
    # road tiles (same object format; the sprite children are not drawn)
    for name, path in (('SCENETTT', os.path.join(RES, 'DATAB', 'SCENETTT.BIN')),
                       ('SCENE01T', os.path.join(RES, 'SCENE01', 'SCENE01T.BIN')),
                       ('SCENE02T', os.path.join(RES, 'SCENE02', 'SCENE02T.BIN'))):
        if not os.path.exists(path):
            continue
        d, tab, objs = load_set(path)
        panels = []
        for i, o in enumerate(tab):
            if o < 0x11:
                continue                                    # alias: previous tile minus o faces
            ob = objs[o]
            panels.append(render_tile(ob, pal, tabs, '%s %d @%04X %s' % (name, i, o, describe(ob))))
        sheet(panels, 6, os.path.join(OUT, name + '_sheet.png'))


def render_tile(ob, pal, tabs, label, size=200):
    im = Image.new('RGB', (size, size + 14), (40, 40, 40))
    im.paste(render(ob, size, pal, tabs, 30, 50), (0, 14))
    ImageDraw.Draw(im).text((2, 1), label, fill=(255, 255, 0))
    return im


# ----------------------------------------------------------------------------------- text dumps
def dump(path, which=None):
    if path.upper().endswith('.POB'):
        d, ob = load_pob(path)
        items = [('POB', ob)]
    else:
        d, tab, objs = load_set(path)
        items = [(i, objs[o]) for i, o in enumerate(tab) if o >= 0x11 and (which is None or i == which)]
    for name, ob in items:
        print('== %s @%04X hdr=%d %s' % (name, ob.off, ob.hdr, describe(ob)))
        for k, v in enumerate(ob.verts):
            print('  v%-3d A=%6d B=%6d C=%6d' % ((k,) + v))
        for k, w in enumerate(ob.faces):
            nv = (w[0] >> 14) + 1
            print('  f%-3d %s  verts=%s flags=%d%d%d c1=%d c2=%d type=%d %s' % (
                k, ' '.join('%04x' % x for x in w), [x & 0x7FF for x in w[:nv]],
                (w[0] >> 13) & 1, (w[0] >> 12) & 1, (w[0] >> 11) & 1, w[1] >> 11, w[2] >> 11,
                w[3] >> 11, TYPE_NAMES.get(w[3] >> 11, '')))
        for c in ob.children:
            print('  sprite id=%04x x=%d z=%d h=%d' % c)
        for c in ob.coll:
            print('  coll A=%d B=%d C=%d r=%d h=%d' % c)
        for k, a in enumerate(ob.anim):
            print('  anim%d v0=(%d,%d,%d) v1=(%d,%d,%d)' % ((k, a[0], a[2], a[4], a[1], a[3], a[5])))


def stats():
    sets = [os.path.join(RES, 'DATAB', 'SCENETTO.BIN'), os.path.join(RES, 'DATAB', 'SCENETTT.BIN'),
            os.path.join(RES, 'SCENE01', 'SCENE01T.BIN')]
    for path in sets:
        d, tab, objs = load_set(path)
        cnt = collections.Counter()
        for ob in objs.values():
            for w in ob.faces:
                cnt[('nv', (w[0] >> 14) + 1)] += 1
                cnt[('type', w[3] >> 11)] += 1
                cnt[('w0b11-13', (w[0] >> 11) & 7)] += 1
                cnt[('c1', w[1] >> 11)] += 1
        print(os.path.basename(path), sorted(cnt.items()))
    for car in td3res.CARS:
        d, ob = load_pob(os.path.join(GAME, car + '.POB'))
        print(car, describe(ob), sorted(collections.Counter((w[1] >> 11, w[2] >> 11) for w in ob.faces).items()))


def paths():
    d = open(os.path.join(RES, 'DATAB', 'SCENETTP.BIN'), 'rb').read()
    n = struct.unpack_from('<H', d, 0)[0] // 2
    tab = struct.unpack_from('<%dH' % n, d, 0)
    for i, o in enumerate(tab):
        cnt = d[o]
        wps = [struct.unpack_from('<Hh2h', d, o + 1 + 8 * k) for k in range(cnt & 0x7F)]
        print('tile %3d @%04X count=%02x %s' % (i, o, cnt, ' '.join(
            '[%04x h%d %d,%d]' % w for w in wps)))


def main():
    args = sys.argv[1:]
    dat = os.path.join(RES, 'SCENE01', 'SCENE01A.DAT')
    if '--dat' in args:
        k = args.index('--dat')
        dat = args[k + 1]
        del args[k:k + 2]
    if not args:
        render_all(dat)
    elif args[0] == 'dump':
        dump(args[1], int(args[2]) if len(args) > 2 else None)
    elif args[0] == 'stats':
        stats()
    elif args[0] == 'paths':
        paths()
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
