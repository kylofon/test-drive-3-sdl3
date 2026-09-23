"""Test Drive III description / configuration files -> readable text + JSON.

Decodes (layouts in port/formats/descriptions.md):
  C<car>.LST      675 bytes: car name, picture pair counts, gearbox + shift lever, dashboard gauges,
                  colours, view/units block, physics block, archive directory part
  SCENE<nn>.LST   1638 bytes: scene name, leg count, picture pair counts, leg data digits, leg and
                  route names, copy-protection code sheet, archive directory part
  SCENE<nn>.HI    450 bytes: top-7 score table + best time/speed/score per leg and route (checksum)
  TD3.CFG         6 bytes: video, audio, MIDI note flag
  PLAYDISK.DAT    179 bytes: disk label, car and scene slots, last selections and options
  MASTERQ.BIN     713 bytes (DATAB archive, from work/res): the master copy-protection code sheet

usage: python tools/td3car.py [GAME_DIR [OUT_DIR]]
       defaults: Game/ and work/desc/. MASTERQ.BIN is read from work/res/DATAB/ (run td3res.py
       extract first). Writes <file>.json and <file>.txt per input plus cars.txt (all cars side by
       side) and summary.txt.
"""
import glob, json, os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    import td3res
except ImportError:                      # name recovery is optional
    td3res = None

# Speed unit of the simulation (DS:1296): 1 unit ~ 1.64 mph, derived from the speedometer dial
# (needle angle / face scale) and the speed governor DS:1244 vs the published top speeds.
MPH_PER_SPEED_UNIT = 1.64
# Engine speed unit (DS:129C): ~10 rpm, derived from the tachometer dial faces.
RPM_PER_ENGINE_UNIT = 10

# ---------------------------------------------------------------- car .LST

PIC_NAMES = ['.ICN', '.SIC', '(unused)', '.BIC', '.SID', '.TOP', '1.BOT', '2.BOT', 'L.BOT',
             'R.BOT', '.ETC', 'FL1.LZ', 'FL2.LZ']

# DS:1200 block, file offset 0x18B. (DS address, name, meaning)
PHYSICS = [
    (0x1200, 'grip_scale', 'grip multiplier in the max yaw-rate formula 0e12:23b1 (600 for all cars)'),
    (0x1202, 'engine_drag_lin', 'engine drag: (rpm^2 + this*rpm) / engine_drag_div (48)'),
    (0x1204, 'grip_slide', 'lateral grip limit, start sliding (~ published lateral g x 100)'),
    (0x1206, 'grip_recover', 'lateral grip threshold to regain grip (~0.8 x grip_slide)'),
    (0x1208, 'engine_drag_div', 'engine drag divisor (300)'),
    (0x120A, 'auto_upshift_rpm', 'auto-shift: upshift when rpm >= this (engine units)'),
    (0x120C, 'auto_downshift_rpm', 'auto-shift: downshift when rpm < this and throttle 0'),
    (0x120E, 'drag_speed_div', 'speed-proportional resistance = speed / this'),
    (0x1210, 'slope_speed_mul', 'multiplies speed in the divisor of the pitch/height term (0977:0008)'),
    (0x1212, 'ratio_R', 'gear ratio x100, reverse (gear index 0)'),
    (0x1214, 'ratio_N', 'gear ratio x100, neutral (index 1) = 0'),
    (0x1216, 'ratio_1', 'gear ratio x100, 1st'),
    (0x1218, 'ratio_2', 'gear ratio x100, 2nd'),
    (0x121A, 'ratio_3', 'gear ratio x100, 3rd'),
    (0x121C, 'ratio_4', 'gear ratio x100, 4th'),
    (0x121E, 'ratio_5', 'gear ratio x100, 5th'),
    (0x1220, 'ratio_6', 'gear ratio x100, 6th (5-speed cars repeat 5th)'),
    (0x1222, 'steer_num', 'steering rate numerator: (DS:B70E+17) x this x 7 x input x 80'),
    (0x1224, 'steer_den', 'steering rate denominator (x 696)'),
    (0x1226, 'brake_force', 'brake force (x brake input, x1/x2/x4 by brake key flags)'),
    (0x1228, 'accel_mul', 'velocity step = net force x this / 4 (8 for all cars)'),
    (0x122A, 'rpm_div', 'rpm = ratio x speed / this (final drive + tyre)'),
    (0x122C, 'rev_limit', 'over-rev threshold (engine units); above it 0e12:0e74 may damage the gearbox'),
    (0x122E, 'torque_base', 'engine force base term (32)'),
    (0x1230, 'torque_throttle', 'engine force per throttle step (throttle 0..29)'),
    (0x1232, 'throttle_upshift', 'throttle value set after an upshift (doubled into 1st)'),
    (0x1234, 'throttle_downshift', 'throttle value set after a downshift'),
    (0x1236, 'free_rev_div', 'rpm change divisor when declutched / airborne (x2 when airborne)'),
    (0x1238, 'grip_mul', 'second grip multiplier in 0e12:23b1 (44)'),
    (0x123A, 'steer_speed_num', 'steering speed factor numerator (1)'),
    (0x123C, 'steer_speed_den', 'steering speed factor denominator (3)'),
    (0x123E, 'grade_div', 'road slope / side-slope force divisor (32; Diablo 27, Mythos 42)'),
    (0x1240, 'turn_num', 'yaw fraction numerator (3)'),
    (0x1242, 'turn_den', 'yaw fraction denominator (4)'),
    (0x1244, 'top_speed', 'speed governor: no drive force at speed >= this (speed units)'),
]

GAUGES = [
    (0xCC64, 'tach_len', 'tachometer needle length (px)'),
    (0xCC66, 'tach_step', 'tach needle angle per step (1/65536 turn); step = rpm >> 5'),
    (0xCC68, 'tach_x', 'tach needle centre x'),
    (0xCC6A, 'tach_y', 'tach needle centre y'),
    (0xCC6C, 'speedo_len', 'speedometer needle length (px)'),
    (0xCC6E, 'speedo_step', 'speedo needle angle per step; step = speed >> 2 (max 31)'),
    (0xCC70, 'speedo_x', 'speedo needle centre x'),
    (0xCC72, 'speedo_y', 'speedo needle centre y'),
    (0xCC74, 'unused_cc74', 'no reader found (152 for all cars)'),
    (0xCC76, 'tach_max', 'tach step clamp (needle stop)'),
    (0xCC78, 'odo_x', 'odometer x (0 = no odometer drawn)'),
    (0xCC7A, 'odo_y', 'odometer y'),
    (0xCC7C, 'tach_box_x', 'tach backing rectangle x (saved/restored around the wheel rim)'),
    (0xCC7E, 'tach_box_y', 'tach backing rectangle bottom y'),
    (0xCC80, 'speedo_box_x', 'speedo backing rectangle x'),
    (0xCC82, 'speedo_box_y', 'speedo backing rectangle bottom y'),
    (0xCC84, 'tach_box_w', 'tach backing rectangle width'),
    (0xCC86, 'speedo_box_w', 'speedo backing rectangle width'),
    (0xCC88, 'speedo_zero', 'speedo zero angle = -this x 0x500'),
    (0xCC8A, 'tach_zero', 'tach zero angle = -this x 0x500'),
]

COLOURS = [
    (0xCEAE, 'radar_led_on', 'radar detector bar colour (0e12:0db4)'),
    (0xCEAF, 'radar_led_bar', 'radar detector bar colour 2 (0e12:0db4)'),
    (0xCEB0, 'unused_ceb0', 'no reader found'),
    (0xCEB1, 'unused_ceb1', 'no reader found'),
    (0xCEB2, 'odo_digits', 'odometer digit colour (0792:16c4)'),
    (0xCEB3, 'odo_back', 'odometer background colour (0792:16c4)'),
    (0xCEB4, 'tach_needle', 'tach needle colour (0792:15d4)'),
    (0xCEB5, 'speedo_needle', 'speedo needle colour (0792:18da)'),
    (0xCEB6, 'countdown_digits', 'top-bar 2-digit countdown colour (0e12:0cbe)'),
    (0xCEB7, 'clock_digits', 'top-bar race clock colour (0e12:0b85)'),
]

STEER = [
    (0xE77E, 'colour', 'steering-wheel marker colour'),
    (0xE780, 'radius', 'marker distance from centre (px)'),
    (0xE782, 'step', 'angle per steering step (1/65536 turn); angle = step x (2 x wheel - 32)'),
    (0xE784, 'x', 'wheel centre x'),
    (0xE786, 'y', 'wheel centre y (screen y of the dashboard page)'),
]

CAR_SUFFIXES = ['COL.BIN', 'SC.BIN', 'SIC.BIN', '.ICN', '.BIC', '.SID', '.SIC', '.ETC', '.TOP',
                '1.BOT', '2.BOT', 'L.BOT', 'R.BOT', 'FL1.LZ', 'FL2.LZ']


def cstr(b):
    return b.split(b'\0')[0].decode('latin-1').rstrip()


def dir_entries(raw, base, suffixes):
    names = {}
    if td3res:
        for s in suffixes:
            names[td3res.name_hash(base + s)] = base + s
    out = []
    for i in range(0, len(raw), 14):
        h2, h1, arc, _pad, off, size = struct.unpack_from('<HHBBII', raw, i)
        h = h1 << 16 | h2
        out.append(dict(name=names.get(h, '?'), hash='%08X' % h, archive=chr(arc) if arc else '',
                        offset=off, size=size))
    return out


def parse_car(path):
    d = open(path, 'rb').read()
    base = os.path.splitext(os.path.basename(path))[0].upper()
    if len(d) != 675:
        raise ValueError('%s: %d bytes, expected 675' % (path, len(d)))
    car = dict(file=os.path.basename(path), name=cstr(d[0:0x13]))
    pairs = struct.unpack_from('<13H', d, 0x13)
    car['picture_pairs'] = {PIC_NAMES[i]: pairs[i] for i in range(13)}
    g = d[0x2D:0x6F]
    top = g[0]
    names = ['R', 'N'] + [str(i) for i in range(1, 7)]
    car['gearbox'] = dict(
        top_gear_index=top, forward_gears=top - 1, gears_per_range=g[0x41],
        lever_step=[g[1 + i] for i in range(8)],
        lever_path=[(g[9 + 2 * i], g[10 + 2 * i]) for i in range(28)])
    car['unused_e541'] = list(d[0x6F:0x76])
    car['steering_marker'] = {n: v for (_, n, _), v in zip(STEER, struct.unpack_from('<5H', d, 0x76))}
    car['colours'] = {n: d[0x80 + i] for i, (_, n, _) in enumerate(COLOURS)}
    gv = struct.unpack_from('<20H', d, 0x8A)
    car['gauges'] = {n: gv[i] for i, (_, n, _) in enumerate(GAUGES)}
    v = d[0xB2:0x18B]
    odo = struct.unpack_from('<I', v, 1)[0]
    car['view'] = dict(imperial=v[0], odo_step=odo, crash_threshold=struct.unpack_from('<h', v, 5)[0],
                       eye_height=struct.unpack_from('<h', v, 7)[0],
                       draw_depth=struct.unpack_from('<H', v, 9)[0],
                       vis_band=struct.unpack_from('<H', v, 11)[0],
                       cockpit_colour=struct.unpack_from('<H', v, 0x13)[0],
                       cockpit_colour2=struct.unpack_from('<H', v, 0x15)[0],
                       rest_hex=v[0x17:].hex())
    pv = struct.unpack_from('<35h', d, 0x18B)
    phys = {n: pv[i] for i, (_, n, _) in enumerate(PHYSICS)}
    car['physics'] = phys
    ratios = [phys['ratio_' + x] for x in ['R', 'N', '1', '2', '3', '4', '5', '6']][:top + 1]
    car['derived'] = dict(
        gears=' '.join('%s:%.2f' % (names[i], r / 100) for i, r in enumerate(ratios)),
        top_speed_mph=round(phys['top_speed'] * MPH_PER_SPEED_UNIT),
        rev_limit_rpm=phys['rev_limit'] * RPM_PER_ENGINE_UNIT,
        auto_upshift_rpm=phys['auto_upshift_rpm'] * RPM_PER_ENGINE_UNIT,
        auto_downshift_rpm=phys['auto_downshift_rpm'] * RPM_PER_ENGINE_UNIT,
        lateral_g=phys['grip_slide'] / 100,
        units='miles / mph' if v[0] else 'km / km/h (results converted to mph)')
    car['directory'] = dir_entries(d[0x1D1:0x2A3], base, CAR_SUFFIXES)
    return car


def car_text(c):
    L = ['%s  "%s"' % (c['file'], c['name']), '']
    L.append('picture pair counts (DS:0B50..0B68):')
    L += ['  %-9s %5d' % (k, v) for k, v in c['picture_pairs'].items()]
    gb = c['gearbox']
    L += ['', 'gearbox (DS:E564): top gear index %d (%d forward), gears per lever range %d'
          % (gb['top_gear_index'], gb['forward_gears'], gb['gears_per_range']),
          '  lever step per gear R,N,1..6: %s' % gb['lever_step'],
          '  lever path (dx,dy)+(208,175): %s' % ' '.join('%d,%d' % p for p in gb['lever_path']),
          '  derived ratios: %s' % c['derived']['gears']]
    L += ['', 'steering-wheel marker (DS:E77E):']
    L += ['  %-8s %6d  %s' % (n, c['steering_marker'][n], m) for _, n, m in STEER]
    L += ['', 'colours (DS:CEAE):']
    L += ['  %-17s %3d  %s' % (n, c['colours'][n], m) for _, n, m in COLOURS]
    L += ['', 'gauges (DS:CC64):']
    L += ['  %04X %-13s %6d  %s' % (a, n, c['gauges'][n], m) for a, n, m in GAUGES]
    L += ['', 'view / units block (DS:94B8, 0xD9 bytes):']
    L += ['  %-16s %s' % (k, v) for k, v in c['view'].items()]
    L += ['', 'physics (DS:1200, file 0x18B):']
    L += ['  %04X %-19s %6d  %s' % (a, n, c['physics'][n], m) for a, n, m in PHYSICS]
    L += ['', 'derived:']
    L += ['  %-19s %s' % (k, v) for k, v in c['derived'].items()]
    L += ['', 'archive directory part (-> DS:074C):']
    L += ['  %-14s %s %s %8d %7d' % (e['name'], e['hash'], e['archive'], e['offset'], e['size'])
          for e in c['directory']]
    return '\n'.join(L) + '\n'


def cars_table(cars):
    L = ['%-20s' % 'field' + ''.join('%10s' % c['file'][1:5] for c in cars)]
    def row(label, vals):
        L.append('%-20s' % label + ''.join('%10s' % v for v in vals))
    row('name', [c['name'].split()[-1][:9] for c in cars])
    for _, n, _ in PHYSICS:
        row(n, [c['physics'][n] for c in cars])
    for k in ['top_speed_mph', 'rev_limit_rpm', 'lateral_g']:
        row(k, [c['derived'][k] for c in cars])
    row('imperial', [c['view']['imperial'] for c in cars])
    row('gears_per_range', [c['gearbox']['gears_per_range'] for c in cars])
    for _, n, _ in GAUGES:
        row(n, [c['gauges'][n] for c in cars])
    return '\n'.join(L) + '\n'

# ---------------------------------------------------------------- scene .LST

SCENE_SUFFIXES = (['.ICN', '.SIC', 'T.BIN', 'O.BIN', 'P.BIN'] +
                  ['%d.%s' % (n, e) for n in range(1, 10) for e in ('COL', 'ALZ', 'BLZ', 'DAT')] +
                  ['%s.DAT' % c for c in 'ABCDEFGHI'] + ['%s.MUS' % c for c in 'ABCDEFGHI'])


def qdecode(b):
    """Copy-protection answer byte: typed character = 0xFF - swap_nibbles(byte)."""
    return chr(0xFF - (((b >> 4) | (b << 4)) & 0xFF))


def parse_codes(q):
    """0x2C8-byte copy-protection block (scene .LST 0x208, DS:8E04, MASTERQ.BIN)."""
    rows = []
    for r in range(12):
        row = q[0x0C + 32 * r:0x0C + 32 * r + 32]
        rows.append([''.join(qdecode(x) for x in row[i:i + 4]) for i in range(0, 32, 4)])
    def words(at):
        offs = struct.unpack_from('<12H', q, at)
        return [q[o:q.index(b'\xAA', o)].decode('latin-1').strip() for o in offs]
    return dict(column_of=list(q[0:12]), grid=rows, picture_a=list(q[0x18C:0x198]),
                picture_b=list(q[0x198:0x1A4]), words_b=words(0x1A4), words_a=words(0x1BC))


def codes_text(cq):
    L = ['  column_of[12]: %s' % cq['column_of'],
         '  picture tables: %s / %s' % (cq['picture_a'], cq['picture_b']),
         '  words b (0x1A4): %s' % ', '.join(cq['words_b']),
         '  words a (0x1BC): %s' % ', '.join(cq['words_a']),
         '  answer grid (12 rows x 8 cols of 4 chars; HOLE = use the other half):']
    L += ['   %2d  %s' % (i, ' '.join(r)) for i, r in enumerate(cq['grid'])]
    return L


def parse_scene(path):
    d = open(path, 'rb').read()
    base = os.path.splitext(os.path.basename(path))[0].upper()
    if len(d) != 1638:
        raise ValueError('%s: %d bytes, expected 1638' % (path, len(d)))
    s = dict(file=os.path.basename(path), name=cstr(d[0:0x13]), legs=d[0x13])
    w = struct.unpack_from('<29H', d, 0x14)
    s['icn_pairs'], s['sic_pairs'] = w[0], w[1]
    digits = d[0x4E:0x58]
    legs = []
    for i in range(9):
        a, b, pic = w[2 + 3 * i:5 + 3 * i]
        rec = d[0x58 + 48 * i:0x58 + 48 * (i + 1)].decode('latin-1')
        legs.append(dict(leg=i + 1, used=i < d[0x13], name=rec[0:12].strip(),
                         routes=[rec[12 + 12 * k:24 + 12 * k].strip() for k in range(3)],
                         alz_pairs=a, blz_pairs=b, picture_set=chr(pic) if pic else '',
                         data_digit=chr(digits[i])))
    s['leg_table'] = legs
    s['own_objects'] = digits[9] == 0
    s['shared_objects_flag'] = digits[9]
    s['codes'] = parse_codes(d[0x208:0x4D0])
    s['directory'] = dir_entries(d[0x4D0:0x666], base, SCENE_SUFFIXES)
    return s


def scene_text(s):
    L = ['%s  "%s"  legs %d' % (s['file'], s['name'], s['legs']),
         'banner .ICN pairs %d, icon .SIC pairs %d' % (s['icn_pairs'], s['sic_pairs']),
         'objects: %s (DS:0B15 = %d)' % ('own <scene>O.BIN/P.BIN' if s['own_objects']
                                          else 'shared SCENETTO/P.BIN', s['shared_objects_flag']),
         '', 'leg table:']
    for g in s['leg_table']:
        L.append('  %d%s %-12s | %-11s | %-11s | %-11s  pics "%s" ALZ %5d BLZ %5d  data "%s"'
                 % (g['leg'], ' ' if g['used'] else '-', g['name'], *g['routes'], g['picture_set'],
                    g['alz_pairs'], g['blz_pairs'], g['data_digit']))
    L += ['', 'copy-protection code sheet (0x208, -> DS:8E04):'] + codes_text(s['codes'])
    L += ['', 'archive directory part (-> DS:081E):']
    L += ['  %-14s %s %s %8d %7d' % (e['name'], e['hash'], e['archive'], e['offset'], e['size'])
          for e in s['directory']]
    return '\n'.join(L) + '\n'

# ---------------------------------------------------------------- .HI


def leg_record(b):
    return dict(time='%d:%02d.%02d' % (b[0], b[1], b[2]), avg_mph=b[3],
                score=struct.unpack_from('<I', b, 4)[0])


def parse_hi(path, cars=None):
    d = open(path, 'rb').read()
    if len(d) != 450:
        raise ValueError('%s: %d bytes, expected 450' % (path, len(d)))
    x = 0
    for c in d[:0x1C1]:
        x ^= c
    h = dict(file=os.path.basename(path), checksum=d[0x1C1], checksum_ok=(x ^ 0x5B) == d[0x1C1])
    top = []
    for i in range(7):
        e = d[0x23 + 18 * i:0x23 + 18 * (i + 1)]
        car = d[i]
        top.append(dict(rank=i + 1, score=struct.unpack_from('<I', d, 7 + 4 * i)[0], car_slot=car,
                        car=(cars[car] if cars and car < len(cars) else ''),
                        name=e[2:17].decode('latin-1').rstrip(), col=e[0], row=e[1], end='%02X' % e[17]))
    h['top_scores'] = top
    h['best_per_route'] = [[leg_record(d[0xA1 + 24 * g + 8 * r:0xA1 + 24 * g + 8 * r + 8])
                            for r in range(3)] for g in range(9)]
    h['best_through_leg'] = [leg_record(d[0x179 + 8 * g:0x181 + 8 * g]) for g in range(9)]
    return h


def hi_text(h):
    L = ['%s  checksum %02X %s' % (h['file'], h['checksum'], 'ok' if h['checksum_ok'] else 'BAD'),
         '', 'top scores (rank, score, car slot, name):']
    L += ['  %d %9d  %2d %-6s "%s"' % (t['rank'], t['score'], t['car_slot'], t['car'], t['name'])
          for t in h['top_scores']]
    L += ['', 'best per leg / route (time, avg mph, score):']
    for g, rs in enumerate(h['best_per_route']):
        L.append('  leg %d: ' % (g + 1) + ' | '.join('%s %3d %8d' % (r['time'], r['avg_mph'], r['score'])
                                                  for r in rs))
    L += ['', 'best cumulative through leg (time, avg mph, score):']
    L += ['  leg %d: %s %3d %8d' % (g + 1, r['time'], r['avg_mph'], r['score'])
          for g, r in enumerate(h['best_through_leg'])]
    return '\n'.join(L) + '\n'

# ---------------------------------------------------------------- TD3.CFG, PLAYDISK.DAT

VIDEO = ['VGA/MCGA 256 colours (library mode 13h)', 'EGA 16 colours (mode 0Dh)',
         'Tandy 16 colours (mode 09h)']
AUDIO = ['PC single-voice', 'Tandy 3-voice', 'Creative Music Systems (CMS / Game Blaster)',
         'MIDI (Roland MT-32 / LAPC-1)', 'Ad Lib or Sound Blaster']


def parse_cfg(path):
    v, a, m = struct.unpack_from('<3H', open(path, 'rb').read())
    return dict(file=os.path.basename(path), video=v, video_name=VIDEO[v] if v < 3 else '?',
                audio=a, audio_name=AUDIO[a] if a < 5 else '?', midi_note=m)


def parse_playdisk(path):
    d = open(path, 'rb').read()
    if len(d) != 179:
        raise ValueError('%s: %d bytes, expected 179' % (path, len(d)))
    cars = [cstr(d[0x12 + 6 * i:0x18 + 6 * i]) for i in range(14)]
    scenes = [cstr(d[0x66 + 8 * i:0x6E + 8 * i]) for i in range(8)]
    car, unk, scene, skill = struct.unpack_from('<4H', d, 0xA6)
    ncars, nscenes, against, computer, steer = d[0xAE:0xB3]
    return dict(file=os.path.basename(path), label=d[0:0x12].split(b'\0')[0].decode('latin-1'),
                car_slots=cars, scene_slots=scenes, car=car, car_name=cars[car] if car < 14 else '',
                unused_0b00=unk, scene=scene, scene_name=scenes[scene] if scene < 8 else '',
                skill=skill, skill_display=skill + 1, auto_shift=skill < 3, cars_on_disk=ncars,
                scenes_on_disk=nscenes, race_players=against, computer_cars=computer,
                steering_response=steer)

# ---------------------------------------------------------------- main


def dump(out, name, obj, text):
    with open(os.path.join(out, name + '.json'), 'w') as f:
        json.dump(obj, f, indent=1)
    with open(os.path.join(out, name + '.txt'), 'w') as f:
        f.write(text)


def main():
    game = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'Game')
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'work', 'desc')
    os.makedirs(out, exist_ok=True)
    summary = []
    cars = [parse_car(p) for p in sorted(glob.glob(os.path.join(game, 'C*.LST')))]
    for c in cars:
        dump(out, c['file'], c, car_text(c))
        summary.append('%-10s %-20s %3d mph  %s' % (c['file'], c['name'], c['derived']['top_speed_mph'],
                                                    c['derived']['gears']))
    with open(os.path.join(out, 'cars.txt'), 'w') as f:
        f.write(cars_table(cars))
    pd = None
    p = os.path.join(game, 'PLAYDISK.DAT')
    if os.path.exists(p):
        pd = parse_playdisk(p)
        dump(out, 'PLAYDISK.DAT', pd, '\n'.join('%-18s %s' % kv for kv in pd.items()) + '\n')
        summary.append('PLAYDISK   "%s" car %s scene %s skill %d' % (pd['label'], pd['car_name'],
                                                                   pd['scene_name'], pd['skill_display']))
    for p in sorted(glob.glob(os.path.join(game, 'SCENE*.LST'))):
        s = parse_scene(p)
        dump(out, s['file'], s, scene_text(s))
        summary.append('%-10s %-20s %d legs' % (s['file'], s['name'], s['legs']))
    for p in sorted(glob.glob(os.path.join(game, 'SCENE*.HI'))):
        h = parse_hi(p, pd['car_slots'] if pd else None)
        dump(out, h['file'], h, hi_text(h))
        summary.append('%-10s checksum %s, best %d' % (h['file'], 'ok' if h['checksum_ok'] else 'BAD',
                                                      h['top_scores'][0]['score']))
    p = os.path.join(game, 'TD3.CFG')
    if os.path.exists(p):
        c = parse_cfg(p)
        dump(out, 'TD3.CFG', c, '\n'.join('%-11s %s' % kv for kv in c.items()) + '\n')
        summary.append('TD3.CFG    %s, %s' % (c['video_name'], c['audio_name']))
    p = os.path.join(ROOT, 'work', 'res', 'DATAB', 'MASTERQ.BIN')
    if os.path.exists(p):
        q = open(p, 'rb').read()
        cq = parse_codes(q[:0x2C8])
        cq['trailer'] = q[0x2C8:].hex()
        scene1 = os.path.join(game, 'SCENE01.LST')
        if os.path.exists(scene1):
            cq['equals_SCENE01_block'] = open(scene1, 'rb').read()[0x208:0x4D0] == q[:0x2C8]
        dump(out, 'MASTERQ.BIN', cq, '\n'.join(['MASTERQ.BIN'] + codes_text(cq)) + '\n')
        summary.append('MASTERQ    decoded (equals SCENE01 block: %s)' % cq.get('equals_SCENE01_block'))
    with open(os.path.join(out, 'summary.txt'), 'w') as f:
        f.write('\n'.join(summary) + '\n')
    print('\n'.join(summary))
    print('->', out)


if __name__ == '__main__':
    main()
