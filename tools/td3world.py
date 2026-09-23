"""Test Drive III world data: leg maps, tile models, lane paths, sprites -> top-down map PNGs.

Needs work/res from `td3res.py extract`. Formats are documented in port/formats/world.md.

Files of a scene <S> (SCENE01, SCENE02; SCENETT = the attract-mode demo set in DATAB):
  <S><A..E>.DAT   leg map, 8504 bytes, a memory image of DS:9592..B6C9 (0792:0fce loads leg
                  DS:0B0A as 'A'+leg). Header parameters, sprite animation table (DS:9611), the
                  32x16 cell map (DS:9671), the object list (DS:A473..), colour tables (DS:B4B9).
  <S>T.BIN        tile models 40h..7Fh (far DS:E770); tiles 00h..3Fh always come from
                  DATAB SCENETTT.BIN (far DS:E7E0). Read by 0e12:72f8.
  <S>O.BIN        object models (far DS:E54C), <S>P.BIN lane paths per tile id (far DS:CE9E);
                  a 7-byte "TJL 90" placeholder means the shared SCENETTO/SCENETTP.BIN are used.
  <S><d>.DAT      roadside sprite bitmaps (near DS:2500), d = DS:0B0C[leg] (always '1').

World: X east 0..7FFFh, Z north 0..3FFFh; a map cell is 400h x 400h, row 0 of the map is the
north edge (Z 3C00h..3FFFh). Tile/object model X/Z are in 1/4 world units (+-800h = half a cell),
model Y is in world units, cell height = (cell hi byte & 3Fh) << 8.

usage: python tools/td3world.py [--cell N] [SCENE ...]   maps -> work/world/<S>_<leg>.png,
                                                         <S>_legs.png (all legs), <S>_sprites.png
       python tools/td3world.py dump SCENE LEG            text dump of one leg (header, map, objects)
       python tools/td3world.py tiles SCENE               every tile model -> work/world/<S>_tiles.png
SCENE is SCENE01, SCENE02 or SCENETT (default: all three); LEG is A..E (SCENETT: A).
"""
import os, struct, sys
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import td3img, td3obj

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, 'work', 'res')
OUT = os.path.join(ROOT, 'work', 'world')
LEG_DS = 0x9592            # leg .DAT load address (near)
CELL = 0x400               # world units per map cell
COLS, ROWS = 32, 16

# ---------------------------------------------------------------------------------------------
# file helpers

def res(scene, name):
    d = 'DATAB' if scene == 'SCENETT' else scene
    return os.path.join(RES, d, name)


def real(path):
    """None for a missing file or the 7-byte 'TJL 90' placeholder, else the path."""
    if not os.path.exists(path) or os.path.getsize(path) < 16:
        return None
    return path


def word_table(d):
    """Offset table at the start of T/O/P.BIN: words until the lowest real offset (> 10h)."""
    offs, lim, i = [], len(d), 0
    while i * 2 < lim:
        o = struct.unpack_from('<H', d, i * 2)[0]
        offs.append(o)
        if o > 0x10:
            lim = min(lim, o)
        i += 1
    return offs


def rot(x, z, r):
    """Quarter-turn rotation used everywhere (0e12:7653, 72f8, 5466): r = rotation byte >> 6."""
    r &= 3
    if r == 0:
        return x, z
    if r == 1:
        return z, -x
    if r == 2:
        return -x, -z
    return -z, x

# ---------------------------------------------------------------------------------------------
# models (T.BIN / SCENETTT.BIN / O.BIN)

class Model:
    """View of a td3obj.Obj (model format: port/formats/objects.md): Y/X/Z vertex arrays
    (td3obj A/B/C), faces (4 words), subs = sprite children (id, x, z, h)."""

    def __init__(self, ob, cut=0):
        self.nv = ob.nv
        self.Y = [v[0] for v in ob.verts]
        self.X = [v[1] for v in ob.verts]
        self.Z = [v[2] for v in ob.verts]
        self.faces = ob.faces[:max(0, len(ob.faces) - cut)]
        self.subs = ob.children


def load_models(path):
    """{table index: Model} via td3obj.load_set. Table entries <= 10h reuse the previous entry's
    model with that many faces fewer (0e12:72f8 -> DS:BD24, subtracted in 0e12:7487)."""
    out = {}
    if path is None:
        return out
    _d, tab, objs = td3obj.load_set(path)
    for i, o in enumerate(tab):
        if o >= 0x11:
            out[i] = Model(objs[o])
        elif i and tab[i - 1] >= 0x11:
            out[i] = Model(objs[tab[i - 1]], cut=o)
    return out


class TileSet:
    def __init__(self, scene):
        self.low = load_models(real(res('SCENETT', 'SCENETTT.BIN')))
        self.high = load_models(real(res(scene, scene + 'T.BIN'))) if scene != 'SCENETT' else {}

    def get(self, tid):
        return self.low.get(tid) if tid < 0x40 else self.high.get(tid - 0x40)

# ---------------------------------------------------------------------------------------------
# lane paths (P.BIN)

def load_paths(scene):
    p = real(res(scene, scene + 'P.BIN')) if scene != 'SCENETT' else None
    d = open(p or res('SCENETT', 'SCENETTP.BIN'), 'rb').read()
    out = {}
    for tid, o in enumerate(word_table(d)):
        n = d[o]
        recs = [struct.unpack_from('<HHhh', d, o + 1 + 8 * k) for k in range(n & 0x7F)]
        out[tid] = (n & 0x80, recs)
    return out

# ---------------------------------------------------------------------------------------------
# leg map (.DAT A..E)

HEADER = [  # (DS address, format, name) -- see world.md
    (0x9594, 'H', 'sim_9594'), (0x9596, 'B', 'snd_9596'),
    (0x95AB, 'H', 'sky_95AB'), (0x95AD, 'H', 'sky_95AD'),
    (0x95AF, '4H', 'traffic_speeds'), (0x95B7, 'h', 'drift_x'), (0x95B9, 'h', 'drift_z'),
    (0x95BB, 'H', 'ref_time_s'), (0x95BD, 'B', 'b_95BD'), (0x95BE, 'B', 'b_95BE'),
    (0x95BF, 'B', 'sprite_scales'), (0x95C0, '4B', 'sky_colours'), (0x95C4, 'B', 'b_95C4'),
    (0x95C7, 'B', 'colour_mode'), (0x95C8, 'B', 'b_95C8'), (0x95C9, '3B', 'route_tiles'),
    (0x95CC, 'B', 'turn_delay'), (0x95CD, 'H', 'w_95CD'), (0x95CF, 'H', 'w_95CF'),
    (0x95D1, 'H', 'w_95D1'), (0x95D3, 'H', 'clearance'), (0x95D5, 'B', 'windmill_speed'),
    (0x95D6, 'B', 'gate_speed'), (0x95D7, 'B', 'b_95D7'),
]


class Leg:
    def __init__(self, scene, leg):
        name = 'SCENETTA.DAT' if scene == 'SCENETT' else '%s%s.DAT' % (scene, leg)
        self.scene, self.leg = scene, leg
        self.d = open(res(scene, name), 'rb').read()
        self.hdr = {}
        for ds, fmt, nm in HEADER:
            v = struct.unpack_from('<' + fmt, self.d, ds - LEG_DS)
            self.hdr[nm] = v if len(v) > 1 else v[0]
        self.opp_script = [list(self.d[0x9597 - LEG_DS + 10 * k:][:10]) for k in range(2)]
        self.sprite_kind = list(self.d[0x95E1 - LEG_DS:][:32])
        self.anim = self.words(0x9611, 48)
        self.cells = self.words(0x9671, COLS * ROWS)
        self.nobj, self.a475, self.first_static = self.words(0xA473, 3)
        n = self.nobj
        self.objects = list(zip(self.words(0xA479, n), self.swords(0xA5B9, n), self.swords(0xA6F9, n),
                                self.words(0xA839, n), self.words(0xA979, n), self.words(0xAAB9, n)))
        self.pairs = self.words(0xB4B9, 256)
        self.direct = list(self.d[0xB6B9 - LEG_DS:][:16])

    def words(self, ds, n):
        return list(struct.unpack_from('<%dH' % n, self.d, ds - LEG_DS))

    def swords(self, ds, n):
        return list(struct.unpack_from('<%dh' % n, self.d, ds - LEG_DS))

    def cell(self, col, row):
        w = self.cells[row * COLS + col]
        return w & 0xFF, (w >> 8) & 0x3F, w >> 14          # tile, height, rotation

    def code_map(self):
        """0e12:261c (day/night part; debug weather toggles ignored)."""
        m = list(range(32))
        if self.hdr['colour_mode'] == 0:
            m[8], m[7] = 0, 8
        return m

    def face_colours(self, c1, c2):
        """0e12:7487 VGA path: two palette indices for a face's colour codes."""
        cm = self.code_map()
        a, b = cm[c1], cm[c2]
        if a & 0x10 or b & 0x10:
            if a & 0x10:
                a = self.direct[a & 15]
            if b & 0x10:
                b = self.direct[b & 15]
            return a, b
        w = self.pairs[(b << 4) | a]
        return w & 0xFF, w >> 8


def cell_centre(col, row):
    return col * CELL + CELL // 2, (ROWS - 1 - row) * CELL + CELL // 2

# ---------------------------------------------------------------------------------------------
# rendering

ROAD_SURFACES = (0x10, 0x11, 0x14, 0x15, 0x16, 0x17)   # asphalt/markings, verge, raised decks
SPRITE_CLASS = {0x00: (255, 255, 255), 0x20: (255, 150, 0), 0x40: (255, 150, 0),
                0x60: (0, 90, 0), 0x80: (255, 255, 0), 0xA0: (255, 0, 255),
                0xC0: (255, 0, 0), 0xE0: (0, 255, 255)}


def palette():
    p = td3img.load_palette(os.path.join(RES, 'DATAC', 'OTWCOL.BIN'))
    return [tuple(p[i * 3:i * 3 + 3]) for i in range(256)]


def mix(pal, a, b):
    return tuple((x + y) // 2 for x, y in zip(pal[a], pal[b]))


class Canvas:
    def __init__(self, cellpx):
        self.s = cellpx
        self.img = Image.new('RGB', (COLS * cellpx, ROWS * cellpx), (0, 0, 0))
        self.dr = ImageDraw.Draw(self.img)

    def xy(self, x, z):
        return x * self.s / CELL, (ROWS * CELL - z) * self.s / CELL


def model_faces(m, cx, cz, cy, r, colour, out):
    """Transform a model's faces into world polygons (X/Z model units are 1/4 world units)."""
    for f in m.faces:
        idx = [w & 0x7FF for w in f[:(f[0] >> 14) + 1]]      # w0 bits 14-15 = vertex count - 1
        if any(v >= m.nv for v in idx):
            continue
        pts, ys = [], []
        for v in idx:
            x, z = rot(m.X[v], m.Z[v], r)
            pts.append((cx + x / 4, cz + z / 4))
            ys.append(cy + m.Y[v])
        surf = f[3] >> 11
        out.append((sum(ys) / len(ys), pts, colour(f[1] >> 11, f[2] >> 11), surf))


def render_leg(leg, tiles, paths, omodels, pal, cellpx=64):
    cv = Canvas(cellpx)
    faces, sprites, specials = [], [], []
    colour = lambda a, b: mix(pal, *leg.face_colours(a, b))
    route = leg.hdr['route_tiles']
    for row in range(ROWS):
        for col in range(COLS):
            tid, h, r = leg.cell(col, row)
            cx, cz = cell_centre(col, row)
            m = tiles.get(tid)
            if m is None:
                specials.append((col, row, (255, 0, 0)))
                continue
            model_faces(m, cx, cz, h << 8, r, colour, faces)
            for typ, sx, sz, sy in m.subs:
                x, z = rot(sx, sz, r)
                sprites.append((cx + x, cz + z, typ))
            if any(f[3] >> 11 == 0x1F for f in m.faces):       # leg finish (gas station)
                specials.append((col, row, (255, 0, 0)))
            if tid in route:
                specials.append((col, row, [(0, 128, 255), (0, 220, 0), (255, 160, 0)][route.index(tid)]))
    # static objects (0e12:70cd: indices first_static..nobj-1 go through 72f8 with AH=1)
    for i, (fl, x, z, y, hd, _) in enumerate(leg.objects):
        if i >= leg.first_static:
            m = omodels.get(fl & 0x3F)
            if m is not None:
                model_faces(m, x, z, y >> 3, (hd >> 14), colour, faces)
    faces.sort(key=lambda f: f[0])
    for _y, pts, col, surf in faces:
        p = [cv.xy(x, z) for x, z in pts]
        if surf not in ROAD_SURFACES:           # dim terrain/scenery so the roads stand out
            col = tuple(c * 3 // 5 for c in col)
        if len(p) >= 3:
            cv.dr.polygon(p, fill=col)
        elif len(p) == 2:
            cv.dr.line(p, fill=col)
    for _y, pts, col, surf in faces:        # event surfaces on top
        if surf >= 0x1C and len(pts) >= 3:
            cv.dr.polygon([cv.xy(x, z) for x, z in pts], outline=(255, 0, 0) if surf == 0x1F else (200, 0, 255))
    # lane paths
    for row in range(ROWS):
        for col in range(COLS):
            tid, h, r = leg.cell(col, row)
            _fl, recs = paths.get(tid, (0, []))
            cx, cz = cell_centre(col, row)
            pts = []
            for k, (fl, y, px, pz) in enumerate(recs):
                x, z = rot(px, pz, r)
                pts.append(cv.xy(cx + x, cz + z))
            prev = None
            for k, (fl, y, px, pz) in enumerate(recs):
                lo = fl & 0xFF
                if prev is not None and (lo & 0xC0) != 0xC0:
                    cv.dr.line([pts[prev], pts[k]], fill=(255, 230, 0))
                for br in (fl & 0x3F, (fl >> 8) & 0x3F):
                    off = br - 64 if br & 0x20 else br
                    if br and 0 <= k + off < len(recs) and fl & 0x3F3F:
                        cv.dr.line([pts[k], pts[k + off]], fill=(0, 255, 255))
                prev = None if (lo & 0xC0) == 0x80 else k
    for x, z, typ in sprites:
        px, py = cv.xy(x, z)
        c = SPRITE_CLASS[(typ >> 8) & 0xE0]
        rr = 2 if (typ >> 8) & 0xE0 in (0xA0, 0xC0) else 1
        cv.dr.ellipse([px - rr, py - rr, px + rr, py + rr], fill=c)
    for col, row, c in specials:
        s = cellpx
        cv.dr.rectangle([col * s, row * s, col * s + s - 1, row * s + s - 1], outline=c, width=2)
    for i, (fl, x, z, y, hd, _) in enumerate(leg.objects):
        if i < leg.first_static:
            px, py = cv.xy(x, z)
            c = (255, 255, 255) if i == 0 else (255, 60, 60) if (fl & 0x3F) in (2, 3) else (255, 120, 255)
            rr = 5 if i == 0 else 3
            cv.dr.ellipse([px - rr, py - rr, px + rr, py + rr], outline=c, width=2)
    return cv.img

# ---------------------------------------------------------------------------------------------
# sprites (<scene><d>.DAT)

def sprites_sheet(scene, pal, zoom=3):
    d = open(res(scene, ('SCENETT1.DAT' if scene == 'SCENETT' else scene + '1.DAT')), 'rb').read()
    n = struct.unpack_from('<H', d, 6)[0]
    imgs = []
    for i in range(1, n):
        h, w, ptrs, lens, _h2 = struct.unpack_from('<BBHHH', d, i * 8)
        im = Image.new('RGB', (max(h, 1), max(w, 1)), (40, 40, 60))   # w rows of up to h pixels
        for c in range(w):
            po = struct.unpack_from('<H', d, ptrs + 2 * c)[0]
            ln = struct.unpack_from('<H', d, lens + 2 * c)[0] & 0x3F
            top = (h - ln) // 2
            for k in range(ln):
                if 0 <= top + k < h:
                    im.putpixel((top + k, c), pal[d[po + k]])
        imgs.append(im.resize((im.width * zoom, im.height * zoom), Image.NEAREST))
    W = sum(i.width for i in imgs) + 4 * len(imgs)
    H = max(i.height for i in imgs) + 14
    sheet = Image.new('RGB', (W, H), (20, 20, 20))
    dr = ImageDraw.Draw(sheet)
    x = 0
    for k, im in enumerate(imgs):
        sheet.paste(im, (x, 12))
        dr.text((x, 0), '%X' % (k + 1), fill=(255, 255, 255))
        x += im.width + 4
    return sheet

# ---------------------------------------------------------------------------------------------

def legs_of(scene):
    return 'A' if scene == 'SCENETT' else 'ABCDE'


def omodels_of(scene):
    p = real(res(scene, scene + 'O.BIN')) if scene != 'SCENETT' else None
    return load_models(p or res('SCENETT', 'SCENETTO.BIN'))


def run(scenes, cellpx):
    os.makedirs(OUT, exist_ok=True)
    pal = palette()
    for scene in scenes:
        tiles, paths, om = TileSet(scene), load_paths(scene), omodels_of(scene)
        imgs = []
        for L in legs_of(scene):
            leg = Leg(scene, L)
            im = render_leg(leg, tiles, paths, om, pal, cellpx)
            p = os.path.join(OUT, '%s_%s.png' % (scene, L))
            im.save(p)
            imgs.append((L, im))
            print(p)
        sw = imgs[0][1].width // 2
        sheet = Image.new('RGB', (sw, sum(i.height // 2 + 16 for _, i in imgs)), (0, 0, 0))
        dr = ImageDraw.Draw(sheet)
        y = 0
        for L, im in imgs:
            dr.text((4, y + 2), '%s leg %s' % (scene, L), fill=(255, 255, 255))
            sheet.paste(im.resize((im.width // 2, im.height // 2)), (0, y + 16))
            y += im.height // 2 + 16
        sheet.save(os.path.join(OUT, '%s_legs.png' % scene))
        sprites_sheet(scene, pal).save(os.path.join(OUT, '%s_sprites.png' % scene))


def dump(scene, L):
    leg = Leg(scene, L)
    for k, v in leg.hdr.items():
        print('%-15s %s' % (k, v if not isinstance(v, int) else '%d (%Xh)' % (v, v)))
    print('opp_script', leg.opp_script)
    print('sprite_kind', leg.sprite_kind)
    print('anim', ' '.join('%02X>%02X/%d' % (i, a & 0x3F, a >> 8) for i, a in enumerate(leg.anim) if a != i))
    print('map (tile, rotation r/R/L = 90/180/270, height):')
    for row in range(ROWS):
        print(' '.join('%02X%s%X' % (t, '.rRL'[r], h) for t, h, r in (leg.cell(c, row) for c in range(COLS))))
    print('objects: %d, dynamic 0..%d, static %d..' % (leg.nobj, leg.first_static - 1, leg.first_static))
    for i, (fl, x, z, y, hd, e) in enumerate(leg.objects):
        print('%3d flags %04X model %02X  X %5d Z %5d Y %04X heading %04X %04X' % (i, fl, fl & 0x3F, x, z, y, hd, e))


def tiles_sheet(scene, pal, cellpx=128):
    tiles, paths = TileSet(scene), load_paths(scene)
    leg = Leg(scene, 'A')
    ids = [t for t in range(0x80) if tiles.get(t) is not None]
    per = 16
    img = Image.new('RGB', (per * cellpx, ((len(ids) + per - 1) // per) * (cellpx + 12)), (0, 0, 0))
    dr = ImageDraw.Draw(img)
    colour = lambda a, b: mix(pal, *leg.face_colours(a, b))
    for k, t in enumerate(ids):
        ox, oy = (k % per) * cellpx, (k // per) * (cellpx + 12) + 12
        faces = []
        model_faces(tiles.get(t), 512, 512, 0, 0, colour, faces)
        faces.sort(key=lambda f: f[0])
        tr = lambda x, z: (ox + x * cellpx / CELL, oy + (CELL - z) * cellpx / CELL)
        for _y, pts, col, surf in faces:
            p = [tr(x, z) for x, z in pts]
            if len(p) >= 3:
                dr.polygon(p, fill=col)
        _fl, recs = paths.get(t, (0, []))
        prev = None
        for i, (fl, y, px, pz) in enumerate(recs):
            if prev is not None and (fl & 0xC0) != 0xC0:
                dr.line([tr(512 + recs[prev][2], 512 + recs[prev][3]), tr(512 + px, 512 + pz)], fill=(255, 230, 0))
            prev = None if (fl & 0xC0) == 0x80 else i
        dr.text((ox + 2, oy - 12), '%02X' % t, fill=(255, 255, 255))
    p = os.path.join(OUT, '%s_tiles.png' % scene)
    img.save(p)
    print(p)


def main(argv):
    cellpx = 64
    if '--cell' in argv:
        i = argv.index('--cell')
        cellpx = int(argv[i + 1])
        del argv[i:i + 2]
    if argv and argv[0] == 'dump':
        return dump(argv[1], argv[2] if len(argv) > 2 else 'A')
    if argv and argv[0] == 'tiles':
        os.makedirs(OUT, exist_ok=True)
        return tiles_sheet(argv[1], palette())
    run(argv or ['SCENE01', 'SCENE02', 'SCENETT'], cellpx)


if __name__ == '__main__':
    main(sys.argv[1:])
