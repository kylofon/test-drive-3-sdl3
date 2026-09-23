"""Rename Ghidra's DGROUP globals to DS offsets and build a globals cross-reference.

Ghidra loads the image at segment 1000, so DGROUP 178F is segment 278F (the loader may split it into
several blocks, e.g. 278F and 2793). Anything that lands inside DGROUP is renamed:

  iRam000278f8     -> i_DS0008     (linear 0x278F8 - DGROUP linear 0x278F0)
  LAB_2793_0010    -> DS_0050      (linear 0x27940)
  DAT_278f_0004    -> DS_0004
Function names get their file segment back: FUN_16c9_1234 -> FUN_06c9_1234.

usage: postprocess.py port/decomp/td2ega.c 178F
       (writes td2ega_ds.c and td2ega_globals_xref.txt next to it)
"""
import os, re, sys
from collections import defaultdict

LOAD_SEG = 0x1000
src, dgroup = sys.argv[1], int(sys.argv[2], 16)
DG_LINEAR = (LOAD_SEG + dgroup) * 16
text = open(src, encoding='utf-8').read()


def ds_off(linear):
    off = linear - DG_LINEAR
    return off if 0 <= off < 0x10000 else None


def ram(m):
    off = ds_off(int(m.group(2), 16))
    return m.group(0) if off is None else '%s_DS%04X' % (m.group(1), off)


def seg_label(m):
    off = ds_off(int(m.group(2), 16) * 16 + int(m.group(3), 16))
    return m.group(0) if off is None else 'DS_%04X' % off


text = re.sub(r'\b([a-zA-Z]*)Ram000([0-9a-f]{5})\b', ram, text)
text = re.sub(r'\b(LAB|DAT)_([0-9a-f]{4})_([0-9a-f]{4})\b', seg_label, text)
text = re.sub(r'\b(FUN|LAB|DAT|fn)_([0-9a-f]{4})_([0-9a-f]{4})\b',
              lambda m: '%s_%04x_%s' % (m.group(1), int(m.group(2), 16) - LOAD_SEG, m.group(3))
              if int(m.group(2), 16) >= LOAD_SEG else m.group(0), text)

xref = defaultdict(set)
current = None
for line in text.splitlines():
    m = re.match(r'// ==== (\S+)\s+image (0x[0-9a-f]+)', line)
    if m:
        current = '%s@%s' % (m.group(1), m.group(2))
        continue
    if current:
        for g in re.findall(r'\b[a-zA-Z]*_DS([0-9A-F]{4})\b|\bDS_([0-9A-F]{4})\b', line):
            xref[g[0] or g[1]].add(current)

out_dir = os.path.dirname(src)
base = os.path.splitext(os.path.basename(src))[0]
open(os.path.join(out_dir, base + '_ds.c'), 'w', encoding='utf-8').write(text)
with open(os.path.join(out_dir, base + '_globals_xref.txt'), 'w') as fh:
    for off in sorted(xref, key=lambda s: int(s, 16)):
        fns = sorted(xref[off])
        fh.write('DS:%s  %2d fns  %s\n' % (off, len(fns), ' '.join(fns)))
print('globals referenced:', len(xref), '->', base + '_ds.c, ' + base + '_globals_xref.txt')
