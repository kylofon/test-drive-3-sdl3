"""Assign every indexed function to the port module that implements it (phase 5).

Rules, in order:
  1. listed by one spec only -> that spec;
  2. listed by several: drop specs whose note says "see <other spec>";
  3. segment owner: platform (0ab4, 0c1c, 16cf-1940), sound (1ace+), simulation (0977);
  4. 0e12 / 0792 ties: reachability from the frame roots of port/RE_GUIDE.md (simulation roots vs
     render3d roots vs hud roots); still tied -> render3d > simulation > hud > game_flow;
  5. not listed by any spec: segment owner (01f4/0000/0792 -> game_flow), MSC runtime 1940 -> platform.
Writes port/modules.csv (address,module,name,listed_by) and prints a summary.
"""
import csv, json, os, re
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPECS = ['game_flow', 'simulation', 'render3d', 'hud', 'platform', 'sound']
listed = defaultdict(dict)
for s in SPECS:
    for r in csv.DictReader(open(os.path.join(ROOT, 'port', 'spec', s + '_symbols.csv'), encoding='utf-8')):
        if r['kind'] == 'func':
            listed[r['address'].lower()][s] = (r['name'], r.get('notes') or '')
names = {r['address'].lower(): r['name'] for r in csv.DictReader(open(os.path.join(ROOT, 'port', 'symbols.csv'), encoding='utf-8'))}
j = json.load(open(os.path.join(ROOT, 'port', 'tdiii_functions.json')))
F = {f['start']: f for f in j['functions']}


def seg_owner(a):
    s = a[:4]
    if s in ('0000', '01f4', '0792'):
        return 'game_flow'
    if s == '0977':
        return 'simulation'
    if s in ('0ab4', '0c1c') or '16cf' <= s <= '1940':
        return 'platform'
    if s >= '1ace':
        return 'sound'
    return None


def reach(roots):
    out, st = set(), list(roots)
    while st:
        a = st.pop()
        if a in out or a not in F:
            continue
        out.add(a)
        st += [c for c in F[a]['calls'] if c[:4] in ('0e12', '0977', '0ab4', '0792')]
    return out


R = {'simulation': reach(['0977:0008', '0ab4:1220', '0e12:095a', '0e12:23df', '0e12:6e92', '0e12:0084', '0e12:5466']),
     'render3d': reach(['0e12:34c3', '0e12:323e', '0e12:6421', '0e12:46c0', '0e12:4c51', '0e12:7c21', '0e12:70cd']),
     'hud': reach(['0e12:0b1d', '0792:0414', '0792:10c8', '0792:15d4', '0792:16c4', '0792:1952'])}
PRIO = ['render3d', 'simulation', 'hud', 'game_flow', 'platform', 'sound']

# Deliberate choices for functions documented by two specs (phase 5 planning): gameplay logic goes to
# simulation, cockpit drawing in 0792 to hud, drawing-side helpers stay with render3d.
OVERRIDE = {a: 'simulation' for a in (
    '0e12:0edb', '0e12:0f31', '0e12:0fd6', '0e12:2465', '0e12:33f7', '0e12:34f1', '0e12:4631',
    '0e12:4c51', '0e12:4d60', '0e12:509b', '0e12:5ad4', '0e12:5cf2', '0e12:5e70', '0e12:5ffe',
    '0e12:61d2', '0e12:61fd', '0e12:624a')}
OVERRIDE.update({a: 'hud' for a in (
    '0792:0414', '0792:04ca', '0792:059a', '0792:0602', '0792:0922', '0792:0cec', '0792:10a6',
    '0792:19ca', '0792:1a72', '0792:1c00', '0792:1c66')})

rows, why = [], Counter()
for a in sorted(F):
    specs = listed.get(a, {})
    if a in OVERRIDE:
        m = OVERRIDE[a]; why['override'] += 1
    elif len(specs) == 1:
        m = next(iter(specs)); why['single'] += 1
    elif not specs:
        m = seg_owner(a) or 'render3d'; why['unlisted'] += 1
    else:
        cand = [s for s in specs if not any(re.search(r'see\s+`?%s' % o, specs[s][1]) for o in SPECS if o != s)]
        cand = cand or list(specs)
        so = seg_owner(a)
        if len(cand) > 1 and so in cand:
            cand = [so]
        if len(cand) > 1:
            r = [s for s in cand if a in R.get(s, ())]
            cand = r if len(r) == 1 else cand
        m = min(cand, key=PRIO.index); why['multi'] += 1
    rows.append((a, m, names.get(a, ''), ' '.join(sorted(specs))))

with open(os.path.join(ROOT, 'port', 'modules.csv'), 'w', newline='', encoding='utf-8') as f:
    w = csv.writer(f)
    w.writerow(['address', 'module', 'name', 'listed_by'])
    w.writerows(rows)
print(why, Counter(r[1] for r in rows))
