"""Test Drive III resource archives.

The executable holds a directory of every archived file (DS:049E, 14-byte entries, zero-terminated):
  u16 h2, u16 h1   name hash (see name_hash)
  u8  archive      'a' DATAA.DAT, 'b' DATAB.DAT, 'c' DATAC.DAT, 'd' the selected car's C<car>.DAT,
                   'e' the selected scene's SCENExx.DAT
  u8  0
  u32 offset, u32 size   within that archive
The 'd' part (DS:074C, 15 entries) and 'e' part (DS:081E, 29 entries) are replaced by the directory at
the end of the selected car's .LST (offset 1D1h) and scene's .LST (offset 4D0h); the executable holds
CCERV's and SCENE01's. Files are stored raw (the loader 0000:0ee0 just reads them); names exist only as
hashes, so they are recovered by hashing candidate names built from the executable's strings.

usage: td3res.py list                 directory with recovered names
       td3res.py extract [OUTDIR]     every entry of every archive -> OUTDIR (default work/res)
"""
import os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, 'Game')
EXE = os.path.join(ROOT, 'work', 'TDIII_unp.exe')
DGROUP = 0x1BE4
DIR_OFF = 0x049E
CARS = ['CCERV', 'CCNSX', 'CDIAB', 'CMYTH', 'CSTEL']
SCENES = ['SCENE01', 'SCENE02']


def name_hash(name):
    """0000:1760: h1 over all characters from the last one (h = h*0x101 + c), h2 = sum c[i]*i over
    all characters but the last. Characters are signed bytes, arithmetic is 16-bit."""
    b = [c - 256 if c > 127 else c for c in name.encode('latin-1')]
    h1 = 0
    for c in reversed(b):
        h1 = (h1 * 0x101 + c) & 0xFFFF
    h2 = sum(c * i for i, c in enumerate(b[:-1])) & 0xFFFF
    return h1 << 16 | h2


def image():
    d = open(EXE, 'rb').read()
    return d[struct.unpack_from('<H', d, 8)[0] * 16:]


CAR_DIR, CAR_LST_DIR, CAR_ENTRIES = 0x074C, 0x1D1, 15
SCENE_DIR, SCENE_LST_DIR, SCENE_ENTRIES = 0x081E, 0x4D0, 29


def directory(img=None, car=CARS[0], scene=SCENES[0]):
    img = bytearray(img or image())
    ds = DGROUP * 16
    for name, at, lst_at, n in ((car, CAR_DIR, CAR_LST_DIR, CAR_ENTRIES),
                                (scene, SCENE_DIR, SCENE_LST_DIR, SCENE_ENTRIES)):
        lst = open(os.path.join(GAME, name + '.LST'), 'rb').read()
        img[ds + at:ds + at + 14 * n] = lst[lst_at:lst_at + 14 * n]
    p = ds + DIR_OFF
    out = []
    while True:
        h2, h1, arc, _pad, off, size = struct.unpack_from('<HHBBII', img, p)
        if not h1 and not h2:
            return out
        out.append(dict(hash=h1 << 16 | h2, archive=chr(arc), offset=off, size=size))
        p += 14


def candidates(img):
    """Names the game can ask for: every string in DGROUP, and car / scene base names combined with
    short suffix strings (optionally with a leg / variant character in between)."""
    ds = img[DGROUP * 16:]
    strings = {m.group(0).decode('latin-1').strip() for m in re.finditer(rb'[\x21-\x7e]{1,16}', ds)}
    strings = {s[2:] if s[1:2] == ':' else s for s in strings}
    names = set(strings)
    suffixes = {s for s in strings if 1 <= len(s) <= 8 and re.fullmatch(r'[A-Z0-9_.]+', s) and '.' in s}
    suffixes |= {'.BLZ', '.ALZ'}    # 01f4: "<scene><c>.COL" is patched in place to .BLZ, then .ALZ
    bases = CARS + [c[1:] for c in CARS] + SCENES + ['SCENETT', 'SCENETTT']
    extra = [''] + [chr(c) for c in range(0x30, 0x3A)] + [chr(c) for c in range(0x41, 0x5B)]
    for b in bases:
        for x in extra:
            for s in suffixes:
                names.add(b + x + s)
    return names


def names_by_hash(img):
    out = {}
    for n in candidates(img):
        out.setdefault(name_hash(n), []).append(n)
    return out


def lzw_decode(data):
    """0ab4:0047: LZW, codes LSB-first, 9 bits growing to 12 (width += 1 once the next free code
    reaches 1 << width), 100h = clear (back to 9 bits, next code 102h), 101h = end."""
    out = bytearray()
    pos = 0
    width, nxt = 9, 0x102
    table = {}
    prev = None
    first = 0

    def expand(c):
        s = bytearray()
        while c > 0xFF:
            p, ch = table[c]
            s.append(ch)
            c = p
        s.append(c)
        s.reverse()
        return s

    while True:
        if (pos + width + 7) // 8 > len(data):
            raise ValueError('LZW stream ended without 101h at byte %d' % (pos // 8))
        v = int.from_bytes(data[pos // 8:pos // 8 + 3], 'little') >> (pos % 8)
        code = v & ((1 << width) - 1)
        pos += width
        if code == 0x101:
            return bytes(out)
        if code == 0x100:
            width, nxt, table, prev = 9, 0x102, {}, None
            continue
        if prev is None:
            s = expand(code)
        elif code < nxt:
            s = expand(code)
            table[nxt] = (prev, s[0]); nxt += 1
        else:                               # KwKwK
            s = expand(prev); s.append(s[0])
            table[nxt] = (prev, s[0]); nxt += 1
        if prev is not None and nxt >= 1 << width and width < 12:
            width += 1
        out += s
        prev = code


def pick_name(cands, arc, car, scene):
    """Hash collisions exist (CSTELSC.BIN = CMYTH8T.BIN): prefer the selected car's / scene's
    name in their archives and plain (non-composite) names in DATAx."""
    base = {'d': car, 'e': scene}.get(arc)
    composite = tuple(CARS + SCENES + [c[1:] for c in CARS] + ['SCENETT'])
    ok = [n for n in cands if n.startswith(base)] if base else [n for n in cands if not n.startswith(composite)]
    return sorted(ok or cands)[0]


def archive_path(arc, car=CARS[0], scene=SCENES[0]):
    return os.path.join(GAME, {'a': 'DATAA.DAT', 'b': 'DATAB.DAT', 'c': 'DATAC.DAT',
                               'd': car + '.DAT', 'e': scene + '.DAT'}[arc])


def main():
    img = image()
    names = names_by_hash(img)
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'list'
    if cmd == 'list':
        for car, scene in [(CARS[0], SCENES[0])] + [(c, SCENES[0]) for c in CARS[1:]] + [(CARS[0], s) for s in SCENES[1:]]:
            print('== %s / %s' % (car, scene))
            ents = directory(img, car, scene)
            for e in ents:
                if car != CARS[0] and e['archive'] != 'd' or scene != SCENES[0] and e['archive'] != 'e':
                    continue
                n = pick_name(names[e['hash']], e['archive'], car, scene) if e['hash'] in names else '?'
                print('%08x %s %06x %6d  %s' % (e['hash'], e['archive'], e['offset'], e['size'], n))
            print('%d entries, %d named' % (len(ents), sum(1 for e in ents if e['hash'] in names)))
    elif cmd == 'extract':
        out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'work', 'res')
        for car, scene in [(CARS[0], SCENES[0])] + [(c, SCENES[0]) for c in CARS[1:]] + [(CARS[0], s) for s in SCENES[1:]]:
            for e in directory(img, car, scene):
                if car != CARS[0] and e['archive'] != 'd' or scene != SCENES[0] and e['archive'] != 'e':
                    continue
                n = pick_name(names[e['hash']], e['archive'], car, scene) if e['hash'] in names else '%08x' % e['hash']
                sub = car if e['archive'] == 'd' else scene if e['archive'] == 'e' else 'DATA' + e['archive'].upper()
                os.makedirs(os.path.join(out, sub), exist_ok=True)
                path = archive_path(e['archive'], car, scene)
                with open(path, 'rb') as f:
                    f.seek(e['offset'])
                    data = f.read(e['size'])
                assert len(data) == e['size'], (path, n)
                open(os.path.join(out, sub, n), 'wb').write(data)
        print('extracted to', out)


if __name__ == '__main__':
    main()
