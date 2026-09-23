"""Merge port/spec/*_symbols.csv into port/symbols.csv and report naming conflicts.

Function addresses are SSSS:OOOO (file segment:offset, see port/RE_GUIDE.md); globals are DS:xxxx.
Also writes port/symbols_ghidra.txt
lines) for tools/ghidra/ApplySymbols.java: "func 16C9:403B name" (Ghidra address, segment + 1000) or
"global DS:BD44 name" / "global 1E12:0000 name".
"""
import csv, glob, json, os, re
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
rows = defaultdict(list)  # (kind, addr) -> [(spec, name, type, notes)]

for path in sorted(glob.glob(os.path.join(ROOT, 'port', 'spec', '*_symbols.csv'))):
    spec = os.path.basename(path)[:-len('_symbols.csv')]
    with open(path, newline='', encoding='utf-8') as fh:
        for r in csv.DictReader(fh):
            kind = (r.get('kind') or '').strip().lower()
            addr = (r.get('address') or '').strip().upper().replace('0X', '')
            name = re.sub(r'\W', '_', (r.get('name') or '').strip())
            m = re.fullmatch(r'([0-9A-F]{4}):([0-9A-F]{4})', addr)
            if kind == 'func' and m:
                lin = int(m.group(1), 16) * 16 + int(m.group(2), 16)
            elif kind == 'global' and re.fullmatch(r'DS:[0-9A-F]{1,4}', addr):
                lin = int(addr[3:], 16)
            elif kind == 'global' and m:
                kind, lin = 'cglobal', int(m.group(1), 16) * 16 + int(m.group(2), 16)
            else:
                if kind in ('func', 'global') and name:
                    print('skipped %s: %s %s %s' % (spec, kind, addr, name))
                continue
            if not name:
                continue
            rows[(kind, lin)].append((spec, name, (r.get('type') or '').strip(), (r.get('notes') or '').strip()))

# Image-offset ranges owned by each spec (port/RE_GUIDE.md, "Subsystem split"). Segment 0e12 mixes
# update and drawing, so it has no range owner: PRIORITY decides there (and for globals).
RANGES = [('game_flow', 0x00000, 0x09770), ('simulation', 0x09770, 0x0AB40),
          ('platform', 0x0AB40, 0x0C1C0),         # 0ab4 helpers; 0ab4:1220 is named by simulation
          ('platform', 0x0C1C0, 0x0E120), ('platform', 0x16CF0, 0x1ACE0), ('sound', 0x1ACE0, 0x1BE40)]
# Most authoritative first: phase 3 specs over phase 2 format docs over the phase 1 seed.
PRIORITY = ['render3d', 'simulation', 'platform', 'sound', 'game_flow', 'hud',
            'objects', 'world', 'descriptions', 'seed']


SEGS = [int(x, 16) for x in json.load(open(os.path.join(ROOT, 'port', 'tdiii_functions.json')))['segments']]


def fmt_func(lin):
    seg = max(x for x in SEGS if x * 16 <= lin)
    return '%04x:%04x' % (seg, lin - seg * 16)


def pick(kind, addr, entries):
    """Functions: the spec owning the address range wins. Otherwise (and for globals) the spec
    highest in PRIORITY."""
    if kind == 'func':
        owner = next((r[0] for r in RANGES if r[1] <= addr < r[2]), None)
        for e in entries:
            if e[0] == owner:
                return e
    rank = lambda spec: PRIORITY.index(spec) if spec in PRIORITY else len(PRIORITY)
    return min(entries, key=lambda e: (rank(e[0]), -len(e[3])))


conflicts = []
with open(os.path.join(ROOT, 'port', 'symbols.csv'), 'w', newline='', encoding='utf-8') as out, \
        open(os.path.join(ROOT, 'port', 'symbols_ghidra.txt'), 'w') as gh:
    w = csv.writer(out)
    w.writerow(['kind', 'address', 'name', 'type', 'owner', 'other_names', 'notes'])
    for (kind, addr), entries in sorted(rows.items()):
        names = sorted({e[1] for e in entries})
        best = pick(kind, addr, entries)
        if len(names) > 1:
            conflicts.append((kind, addr, entries))
        a = ('DS:%04X' % addr) if kind == 'global' else fmt_func(addr)
        w.writerow(['func' if kind == 'func' else 'global', a, best[1], best[2], best[0],
                    ' '.join(n for n in names if n != best[1]), ' | '.join(e[3] for e in entries if e[3])])
        if kind == 'global':
            gh.write('global DS:%04X %s\n' % (addr, best[1]))
        else:  # Ghidra loads the image at segment 1000
            seg = max(x for x in SEGS if x * 16 <= addr)
            gh.write('%s %04X:%04X %s\n' % ('func' if kind == 'func' else 'global', seg + 0x1000,
                                            addr - seg * 16, best[1]))

with open(os.path.join(ROOT, 'port', 'symbol_conflicts.txt'), 'w', encoding='utf-8') as fh:
    for kind, addr, entries in conflicts:
        where = ('DS:%04X' % addr) if kind == 'global' else fmt_func(addr)
        fh.write('%s %s: %s\n' % (kind, where, '; '.join('%s=%s' % (e[0], e[1]) for e in entries)))

print('%d symbols (%d funcs, %d globals), %d with conflicting names' % (
    len(rows), sum(1 for k in rows if k[0] == 'func'), sum(1 for k in rows if k[0] != 'func'), len(conflicts)))
