"""td3snd.py - Test Drive III music (.MUS), INSTR.DAT and sound-effect tools.

Format and driver notes: port/formats/sound.md.  Run from the repository root.

  python tools/td3snd.py list  FILE.MUS             event listing in file order (tick = 1/145.65 s)
  python tools/td3snd.py midi  [FILE.MUS ...] [--loops N] [--adlib-only] [--raw-programs]
        -> work/sound/<name>.mid   (standard MIDI file, format 0, 96 ppq, tempo so that
           1 MIDI tick = 1 driver tick; loops unrolled as the driver plays them: intro + N
           passes of the endless cycle, default N = 1).  Programs are the MT-32 program
           numbers stored in the file, mapped to General MIDI (approximate table) unless
           --raw-programs.  --adlib-only keeps only the channels the AdLib driver plays.
  python tools/td3snd.py dro   [FILE.MUS ...] [--loops N]
        -> work/sound/<name>.dro   (DOSBox raw OPL v2.0, playable with AdPlug / foobar2000
           adplug / DOSBox-X): Python re-implementation of the game's AdLib driver
           (segment 1ace) fed with the .MUS, so this is what the game sends to the OPL2.
  python tools/td3snd.py instr [Game/INSTR.DAT]     -> work/sound/instr.txt (all 3 banks)
  python tools/td3snd.py sfx   [--dro]              -> work/sound/sfx.txt (+ sfx_XX.dro)
        sound-effect scripts from the executable (needs work/TDIII_unp.exe)
  python tools/td3snd.py all                        everything above for every .MUS in work/res

Default .MUS set: work/res/*/*.MUS (extract with tools/td3res.py first).
"""
import glob
import os
import struct
import sys

EXE = 'work/TDIII_unp.exe'
INSTR = 'Game/INSTR.DAT'
OUT = 'work/sound'
DGROUP = 0x1BE40                      # image offset of DS (segment 1BE4)
PIT_HZ = 1193181.667 / 0x2000         # timer_install: PIT ch0 divisor 2000h -> 145.652 Hz
TICK_MS = 1000.0 / PIT_HZ             # 6.866 ms

# ---------------------------------------------------------------- .MUS event stream
# status byte: bit 7 = a delta time follows the event, bits 4-6 = type, bits 0-3 = channel
EV_LEN = (2, 3, 2, 1, 2, 1, 1, 1)     # DS:C985, bytes incl. status, per type 0..7
EV_NAME = ('off', 'on', 'loop', 'brk?', 'prog', 'skip', 'hook', 'end')


def u16(b, o):
    return b[o] | b[o + 1] << 8


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s8(v):
    v &= 0xFF
    return v - 0x100 if v & 0x80 else v


def read_delta(d, p):
    """1ace:0468: 1 byte (0..127) or 2 bytes little-endian 7+8 bits."""
    b = d[p]
    p += 1
    if b & 0x80:
        b = (b & 0x7F) | d[p] << 7
        p += 1
    return b, p


def parse_mus(d):
    """Linear parse (file order, loops not followed). Returns (header_word, events);
    event = dict(off, tick, st, typ, ch, args, delta)."""
    hdr = u16(d, 0)
    ev = []
    p = 2
    t = 0
    while p < len(d):
        off = p
        st = d[p]
        typ = (st >> 4) & 7
        n = EV_LEN[typ]
        args = d[p + 1:p + n]
        p += n
        e = dict(off=off, tick=t, st=st, typ=typ, ch=st & 15, args=bytes(args), delta=None)
        ev.append(e)
        if st == 0xFC:                 # 1ace:05ca stops before the delta is read
            e['end'] = p
            break
        if st & 0x80:
            e['delta'], p = read_delta(d, p)
            t += max(e['delta'], 1)    # counter: dec, run when <= 0 -> delta 0 == 1
    return hdr, ev


def describe(e):
    a = e['args']
    typ = e['typ']
    if typ == 0:
        return 'note off ch%-2d %3d' % (e['ch'], a[0])
    if typ == 1:
        return 'note on  ch%-2d %3d vel %3d%s' % (e['ch'], a[0], a[1], ' (=off)' if a[1] == 0 else '')
    if typ == 2:
        return 'loop %s ch%d' % ('start' if a[0] == 0 else 'end x%d' % a[0], e['ch'])
    if typ == 4:
        return 'program  ch%-2d %3d' % (e['ch'], a[0])
    if typ == 3:
        return 'Bx: stop here if a stop was requested'
    if typ == 5:
        return 'Dx: jump to the exit of the last loop'
    if typ == 6:
        return 'Ex: callback (no-op)'
    return 'END' if e['st'] == 0xFC else 'Fx %02X (no-op)' % e['st']


class Sequencer:
    """The event part of the music tick (1ace:03d6 + handlers DS:C975), independent of the
    device.  voice_of(ch) = the channel map of the loaded bank (loop state is per voice)."""

    def __init__(self, d, voice_of):
        self.d = d
        self.voice_of = voice_of
        self.pos = 2
        self.wait = 1
        self.done = False
        self.cnt = {}
        self.exit = {}
        self.start = {}
        self.sst = {}

    def state(self):
        return (self.pos, tuple(sorted(self.cnt.items())), tuple(sorted(self.exit.items())),
                tuple(sorted(self.start.items())))

    def tick(self, out):
        """Advance one timer tick; appends (st, typ, ch, voice, a1, a2) events to out."""
        if self.done:
            return
        self.wait -= 1
        if self.wait > 0:
            return
        d = self.d
        while True:
            st = d[self.pos]
            typ = (st >> 4) & 7
            ch = st & 15
            v = self.voice_of(ch)
            a1 = d[self.pos + 1] if EV_LEN[typ] > 1 else 0
            a2 = d[self.pos + 2] if EV_LEN[typ] > 2 else 0
            self.pos += EV_LEN[typ]
            cur = st
            if typ == 2:                       # 1ace:0536 loop
                if a1 == 0:
                    self.start[v] = self.pos
                    self.sst[v] = st
                else:
                    c = self.cnt.get(v, 0)
                    jump = False
                    if c == 0:
                        if self.start.get(v, 0):
                            self.cnt[v] = a1
                            self.exit[v] = self.pos
                            jump = True
                    elif c != 1:
                        jump = True
                    if jump:
                        self.pos = self.start[v]
                        cur = self.sst[v]
                    if c or jump:
                        self.cnt[v] = self.cnt.get(v, 0) - 1
            elif typ == 5:                     # 1ace:05b4
                if self.exit.get(v, 0):
                    self.pos = self.exit[v]
            elif typ == 7 and st == 0xFC:      # 1ace:05ca
                out.append((st, typ, ch, v, 0, 0))
                self.done = True
                return
            out.append((st, typ, ch, v, a1, a2))
            if cur & 0x80:
                self.wait, self.pos = read_delta(d, self.pos)
                return


def unroll(d, voice_of, loops=1, max_ticks=145 * 60 * 30):
    """Play the sequence like the driver.  Returns (timeline [(tick, ev)], loop_tick, end_tick).
    Songs loop forever through their loop events; we stop after `loops` passes of the cycle."""
    seq = Sequencer(d, voice_of)
    seen = {}
    tl = []
    loop_tick = None
    end_tick = None
    t = 0
    while t < max_ticks:
        if seq.wait <= 1 and not seq.done:     # events will be processed this tick
            k = seq.state()
            if loop_tick is None and k in seen:
                loop_tick = seen[k]
                end_tick = t + (loops - 1) * (t - loop_tick)
            seen.setdefault(k, t)
        if end_tick is not None and t >= end_tick:
            break
        out = []
        seq.tick(out)
        tl.extend((t, e) for e in out)
        if seq.done:
            end_tick = t + 1
            break
        t += 1
    return tl, loop_tick, (end_tick if end_tick is not None else t)


# ---------------------------------------------------------------- INSTR.DAT
def parse_instr(path=INSTR):
    """Sections: u16 size-of-rest, ASCIIZ device name, 0x162-byte table block
    (-> DS:C638), then count*size instrument bytes (-> far buffer DS:C96A)."""
    d = open(path, 'rb').read()
    banks = []
    p = 0
    while p + 2 <= len(d):
        size = u16(d, 0 + p)
        if size == 0:
            break
        q = p + 2
        e = d.index(b'\0', q)
        name = d[q:e].decode('latin-1')
        q = e + 1
        blk = d[q:q + 0x162]
        q += 0x162
        cnt, isz = blk[0x160], blk[0x161]
        data = d[q:q + cnt * isz]
        banks.append(dict(off=p, size=size, name=name, progmap=list(blk[0:128]),
                          veloff=[s8(x) for x in blk[0x80:0x100]], unused=list(blk[0x100:0x110]),
                          chmap=list(blk[0x110:0x120]), percmap=list(blk[0x120:0x160]),
                          count=cnt, isize=isz,
                          inst=[list(data[i * isz:(i + 1) * isz]) for i in range(cnt)],
                          end=q + cnt * isz))
        p = p + 2 + size
    return banks


def op_str(r20, r40, r60, r80):
    return ('AM%d VIB%d EG%d KSR%d MULT%2d  KSL%d TL%2d  AR%2d DR%2d  SL%2d RR%2d' % (
        r20 >> 7, r20 >> 6 & 1, r20 >> 5 & 1, r20 >> 4 & 1, r20 & 15, r40 >> 6, r40 & 63,
        r60 >> 4, r60 & 15, r80 >> 4, r80 & 15))


def dump_instr(path=INSTR, out_path=None):
    banks = parse_instr(path)
    L = []
    for bi, b in enumerate(banks):
        L.append('=== section %d at 0x%X: "%s"  (size word %d, %d instruments x %d bytes, ends 0x%X)'
                 % (bi, b['off'], b['name'], b['size'], b['count'], b['isize'], b['end']))
        L.append('program map  (MIDI program -> %s):' % ('instrument' if bi == 0 else 'value sent'))
        for r in range(0, 128, 16):
            L.append('  %3d: %s' % (r, ' '.join('%3d' % x for x in b['progmap'][r:r + 16])))
        L.append('velocity offset per program (signed, added to note-on velocity):')
        for r in range(0, 128, 16):
            L.append('  %3d: %s' % (r, ' '.join('%4d' % x for x in b['veloff'][r:r + 16])))
        L.append('block 0x100..0x10F (not referenced): %s' % ' '.join('%02X' % x for x in b['unused']))
        L.append('channel map (MIDI ch -> voice, >15 = not played): %s'
                 % ' '.join('%d:%s' % (c, v if v <= 15 else '-') for c, v in enumerate(b['chmap'])))
        L.append('percussion note map (note 36+i -> %s):' % ('pitch note' if bi == 0 else 'note sent'))
        for r in range(0, 64, 16):
            L.append('  %3d: %s' % (36 + r, ' '.join('%3d' % x for x in b['percmap'][r:r + 16])))
        if b['isize'] == 9:
            users = {}
            for prg, ins in enumerate(b['progmap']):
                users.setdefault(ins, []).append(prg)
            L.append('instruments (9 bytes: op1 20/40/60/80, op2 20/40/60/80, C0):')
            for i, ins in enumerate(b['inst']):
                tag = {1: ' [BD]', 2: ' [SD]', 3: ' [TT]', 4: ' [CY]', 5: ' [HH]'}.get(i, '')
                L.append('  %2d: %s%s  programs %s' % (i, ' '.join('%02X' % x for x in ins), tag,
                                                      users.get(i, [])))
                L.append('      mod %s' % op_str(*ins[0:4]))
                L.append('      car %s' % op_str(*ins[4:8]))
                L.append('      C0 %02X: FB%d CON%d (written to register C0 only, see sound.md)'
                         % (ins[8], ins[8] >> 1 & 7, ins[8] & 1))
        L.append('')
    txt = '\n'.join(L)
    if out_path:
        open(out_path, 'w').write(txt)
    return txt


# ---------------------------------------------------------------- driver tables (DGROUP)
FNUM = (0x205, 0x223, 0x244, 0x267, 0x28B, 0x2B2, 0x2DB, 0x306, 0x334, 0x365, 0x399, 0x3CF)  # DS:C9AA
FSTEP = (1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3)     # DS:C992: F-number per 1/16 semitone
OPOFF = (0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 16, 17, 18, 19, 20, 21)   # DS:C9C7 slot->operator
VSLOT = (0, 3, 1, 4, 2, 5, 6, 9, 7, 10, 8, 11, 12, 15, 16, 0xFF, 14, 0xFF, 17, 0xFF, 13, 0xFF)  # DS:C9D9
CARRIER = (0, 0, 0, 1, 1, 1) * 3                  # DS:C9EF
RHY_BIT = {6: 0x10, 7: 0x08, 8: 0x04, 9: 0x02, 10: 0x01}   # DS:C9BC[6..10]
OPREG = (0x20, 0x40, 0x60, 0x80)                  # DS:CA01
PERC_INST = (0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5)     # DS:CA25 voice -> instrument (9ed / 09a1)
PERC_VOICE = (6, 7, 7, 7, 7, 8, 10, 8, 10, 8, 10, 8, 8, 9, 8, 9, 15, 15, 10, 15,   # DS:CA30
              10, 15, 15, 15, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 10, 15, 15, 8, 15, 8,
              # the table has 40 entries (notes 36..75); higher notes read the next table
              2, 0, 2, 0, 2, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0)
DEV_ADLIB = 4


def load_exe():
    d = open(EXE, 'rb').read()
    return d[u16(d, 8) * 16:]


def check_tables(img):
    ds = lambda o, n: img[DGROUP + o:DGROUP + o + n]
    ok = [list(struct.unpack('<12H', ds(0xC9AA, 24))) == list(FNUM),
          list(struct.unpack('<12H', ds(0xC992, 24))) == list(FSTEP),
          list(ds(0xC9C7, 18)) == list(OPOFF), list(ds(0xC9D9, 22)) == list(VSLOT),
          list(ds(0xC9EF, 18)) == list(CARRIER), list(ds(0xCA25, 11)) == list(PERC_INST),
          list(ds(0xCA30, 64)) == list(PERC_VOICE), list(ds(0xC985, 8)) == list(EV_LEN)]
    if not all(ok):
        raise SystemExit('driver tables differ from the executable: %s' % ok)


# ---------------------------------------------------------------- AdLib driver model
class AdlibDriver:
    """Python model of the AdLib/SB routines of the 1ace driver (device index 3, C5E3 = 4),
    the music tick and the sound-effect engine.  Names follow port/spec/sound_symbols.csv."""

    def __init__(self, bank, write):
        self.write = write
        self.bank = bank
        self.chmap = bank['chmap']                 # DS:C748
        self.progmap = bank['progmap']             # DS:C638
        self.veloff = bank['veloff']               # DS:C6B8
        self.percmap = bank['percmap']             # DS:C758
        self.inst = bank['inst']                   # far DS:C96A
        self.owner = [0] * 16      # C8AD  0 free, 1 music, 2..3 sfx slot+1
        self.prio = [0] * 16       # C8CD
        self.note = [0] * 16       # C8BD  device note (MIDI-19 for melodic)
        self.nword = [0] * 16      # C86D  note<<8 | fine (fine/16 = 1/16 semitone)
        self.vel = [0] * 16        # C88D
        self.busy = [0] * 16       # C8DD  note started this tick
        self.prog = [0] * 16       # C843
        self.b0 = [0] * 16         # C8ED  shadow of B0+v
        self.tl = [0] * 18         # CA05  instrument TL per slot
        self.conn = [0] * 16       # CA15  instrument C0 bit 0 per voice
        self.bd = 0                # C5E6
        self.cur = 1               # C8FF  current owner id
        self.forced = False        # C98D
        self.mvol = 0              # C5DE
        self.flags0 = 0            # C5E0  bit0 music ready, bit2 sfx ready
        self.flags1 = 0            # C5E1  bit0 music playing, bit2 sfx active
        self.seq = None
        # sound-effect slots 0..2 (only 1 and 2 are allocated on AdLib)
        self.s_step = [0] * 3      # CB05 vibrato step
        self.s_cnt = [1] * 3       # CB11 vibrato counter
        self.s_per = [0] * 3       # CB17 vibrato period
        self.s_slide = [0] * 3     # CB1D slide per tick
        self.s_wait = [0] * 3      # CB23
        self.s_ptr = [0] * 3       # CB29
        self.s_lstart = [0] * 3    # CB2F
        self.s_lcnt = [0] * 3      # CB35
        self.s_voice = [0xFF] * 3  # CB3B
        self.sfx_id = [0] * 16     # CAF5 voice -> game sfx id
        self.ds = None             # DGROUP bytes for the sfx scripts
        self.engine_off = 0        # DS:C900

    # --- 1bd8:0028 chip reset + 9ed + 2a5 (as run by 01cc(4) / 0ffe(4))
    def init(self):
        self.write(0xBD, 0x20)                     # rhythm mode (C5E6 = 20h)
        self.bd = 0x20
        self.write(0x01, 0x20)                     # waveform select enable (waveforms stay 0)
        for v in range(10, -1, -1):                # 1ace:09ed
            self.prog[v] = PERC_INST[v]
            self.load_inst(v, PERC_INST[v])
        self.note_on(9, 0x26, 0, 0)                # 1ace:02e6 warm-up snare, velocity 0
        self.note_off(9, 0x26)
        self.flags0 |= 5

    def load_inst(self, v, i):                     # 1ace:092a
        ins = self.inst[i]
        self.conn[v] = ins[8] & 1
        self.tl[VSLOT[2 * v]] = ins[1]
        if VSLOT[2 * v + 1] != 0xFF:
            self.tl[VSLOT[2 * v + 1]] = ins[5]
        k = 0
        for o in (2 * v, 2 * v + 1):
            slot = VSLOT[o]
            if slot == 0xFF:
                return
            for c in range(4):
                self.write(OPOFF[slot] + OPREG[c], ins[k])
                k += 1
        self.write(0xC0, ins[8])                   # sic: always channel 0's C0

    def set_tl(self, idx, vel2):                   # 1ace:07ad
        slot = VSLOT[idx]
        if CARRIER[slot] == 1 or slot > 6 or self.conn[slot]:
            val = (0x3F - vel2) & 0x3F
        else:
            val = self.tl[slot]
        self.write(0x40 + OPOFF[slot], val)

    def set_freq(self, v):                         # 1ace:0884 (+086d, 0d86)
        if v >= 9:
            return
        n = self.nword[v] >> 8
        fine = self.nword[v] & 0xFF
        while s8(n) > 0x60:
            n -= 12
        while s8(n) < 0:
            n += 12
        n &= 0xFF
        self.nword[v] = n << 8 | fine
        octv, semi = divmod(n, 12)
        f = FNUM[semi] + (fine >> 4) * FSTEP[semi]
        self.write(0xA0 + v, f & 0xFF)
        b = ((f >> 8) | (octv << 2) | (self.b0[v] & 0x20)) & 0xFF
        self.b0[v] = b
        self.write(0xB0 + v, b)

    def alloc(self, v, n, prio):                   # 1ace:05df, carry = reject
        if self.cur > 1:
            self.busy[v] = 1
            return True
        if prio < self.prio[v]:
            return False
        if self.owner[v] != 1:
            self.release(v)
            self.owner[v] = 1
            self.prio[v] = 0x40
        if self.busy[v] and n <= self.note[v]:
            return False
        self.busy[v] = 1
        return True

    def note_on(self, v, n, vel, fine, prio=0x40):  # 1ace:07df
        if v == 9:
            i = (n - 0x24) & 0xFF
            rv = PERC_VOICE[i] if i < len(PERC_VOICE) else 15
            if rv >= 11:
                return
            pitch = self.percmap[i] if i < 64 else 0
            self.set_tl(2 * rv, vel >> 1)
            if rv < 7:
                self.set_tl(2 * rv + 1, vel >> 1)
            self.bd |= RHY_BIT[rv]
            self.write(0xBD, self.bd)
            self.b0[rv] = fine
            self.nword[rv] = pitch << 8 | fine
            self.set_freq(rv)
            return
        while n < 0x13:
            n += 12
        n -= 0x13
        if not self.alloc(v, n, prio):
            return
        self.owner[v] = self.cur
        self.forced = True
        self.note_off(v, n)
        self.forced = False
        self.nword[v] = n << 8 | fine
        self.note[v] = n
        self.set_tl(2 * v, vel >> 1)
        self.set_tl(2 * v + 1, vel >> 1)
        self.b0[v] = 0x20
        self.set_freq(v)

    def note_off(self, v, n):                      # 1ace:08c1
        if v > 9:
            return
        if v == 9:
            n = max(n, 0x24) - 0x24
            rv = PERC_VOICE[n] if n < len(PERC_VOICE) else 15
            if rv > 10:
                return
            self.bd &= ~RHY_BIT[rv] & 0xFF
            self.write(0xBD, self.bd)
            self.nword[9] = 0
            self.note[rv] = 0
            return
        if not self.forced:
            n -= 0x13
            while n < 0:
                n += 12
            if n != self.note[v]:
                return
        self.b0[v] &= 0x1F
        self.write(0xB0 + v, self.b0[v])
        self.nword[v] = 0                           # the driver also clears noteword and note
        self.note[v] = 0                            # (port/spec/sound.md, 1ace:08c1)

    def program(self, v, p):                       # 1ace:05a6 + 09a1
        if v > 13:
            return
        self.prog[v] = p
        if v < 6:
            self.load_inst(v, self.progmap[p])
        elif v <= 10:
            self.prog[v] = PERC_INST[v]

    def release(self, v):                          # 1ace:00ec
        if v > 15 or self.owner[v] <= 1:
            return
        self.sfx_id[v] = 0
        s = self.owner[v] - 1
        self.s_voice[s] = 0xFF
        self.s_ptr[s] = 0
        self.forced = True
        self.note_off(v, self.note[v])
        self.forced = False
        self.prio[v] = self.nword[v] = self.note[v] = self.owner[v] = 0
        self.flags1 &= ~4

    # --- music API
    def music_notes_off(self):                     # 1ace:0006
        self.flags1 &= ~1
        for ch in range(15, -1, -1):
            v = self.chmap[ch]
            if v <= 15 and self.owner[v] == 1:
                self.forced = True
                self.note_off(v, self.nword[v] >> 8)
                self.forced = False
                self.owner[v] = self.prio[v] = 0

    def music_play(self, data):                    # 1ace:004b(ptr, 0, 0)
        self.music_notes_off()
        self.seq = Sequencer(data, lambda ch: self.chmap[ch])
        self.mvol = 0
        for v in range(15, -1, -1):                # 1ace:0151
            if self.owner[v] >= 2:
                if self.prio[v] > 0x40:
                    continue
                self.release(v)
            self.owner[v] = 1
            self.prio[v] = 0x40
        self.flags1 |= 1

    def music_event(self, e):
        st, typ, ch, v, a1, a2 = e
        if typ == 0:
            if v <= 13:
                self.note_off(v, a1)
        elif typ == 1:                             # 1ace:04e8
            if v > 13:
                return
            if a2 == 0:
                self.note_off(v, a1)
                return
            self.cur = 1
            x = max(0, min(127, a2 + self.veloff[self.prog[v]]))
            x += self.mvol
            x = 1 if x < 0 else min(x, 127)
            self.note_on(v, a1, x, 0, 0x40)
        elif typ == 4:
            self.program(v, a1)
        elif typ == 7 and st == 0xFC:
            self.music_notes_off()

    # --- timer tick (1ace:03d6)
    def tick(self):
        self.busy = [0] * 16
        self.sfx_mod()
        self.sfx_step()
        if self.seq and self.flags1 & 1:
            out = []
            self.seq.tick(out)
            for e in out:
                self.music_event(e)

    # --- sound effects
    def sfx_start(self, ptr, prio):                # 1ace:0db9
        if not self.flags0 & 4:
            return 0xFF
        self.flags1 |= 4
        dx = 3
        cx = 8
        while True:
            al = self.chmap[cx]
            if al < 9:
                break
            cx -= 1
            if cx == 0:
                break
        cx = al
        bx = cx
        if cx and self.owner[bx] == dx:
            bx -= 1
            dx -= 1
            if self.owner[bx] == dx and s8(self.prio[bx]) >= s8(self.prio[bx + 1]):
                bx += 1
                dx += 1
        if s8(self.prio[bx]) > s8(prio):
            return 0xFF
        self.forced = True
        self.note_off(bx, self.note[bx])
        self.forced = False
        self.owner[bx] = dx
        self.prio[bx] = prio
        s = dx - 1
        self.s_voice[s] = bx
        self.s_ptr[s] = ptr
        self.s_wait[s] = 1
        return bx

    def w(self, a):
        return u16(self.ds, a)

    def sfx_mod(self):                             # 1ace:0d1c
        for s in (2, 1, 0):
            self.s_cnt[s] = s16(self.s_cnt[s] - 1)
            if self.s_cnt[s] < 0:
                self.s_step[s] = s16(-self.s_step[s])
                self.s_cnt[s] = self.s_per[s]
        if self.flags0 & 4:
            for s in (2, 1):
                self.cur = s + 1
                if self.s_voice[s] != 0xFF:
                    self.mod_voice(self.s_voice[s], s)
        self.cur = 1
        for bx in range(8, -1, -1):
            if self.owner[bx] == 1:
                v = self.chmap[bx]
                if v <= 15 and self.note[v]:
                    self.mod_voice(v, 0)

    def mod_voice(self, v, s):                     # 1ace:0ce9
        cx = (self.nword[v] + self.s_slide[s] + self.s_step[s]) & 0xFFFF
        if cx == self.nword[v] and self.note[v] == cx >> 8:
            return
        self.nword[v] = cx
        self.set_freq(v)

    def sfx_step(self):                            # 1ace:0e55
        if not self.flags0 & 4:
            return
        for s in (2, 1, 0):
            if self.s_voice[s] == 0xFF or not self.s_ptr[s]:
                continue
            if self.s_wait[s]:
                self.s_wait[s] -= 1
                continue
            di = self.s_ptr[s]
            op = s16(self.w(di))
            if op > 12:
                op = 9
            di = self.sfx_op(s, op, di + 2)
            self.s_ptr[s] = di

    def sfx_op(self, s, op, di):
        v = self.s_voice[s]
        if op == 1:
            a, b = self.w(di), self.w(di + 2)
            self.s_slide[s] = b if a else s16(-b)
            return di + 4
        if op == 2:
            self.s_slide[s] = 0
            return di
        if op == 3:
            self.s_per[s] = self.w(di)
            self.s_cnt[s] = (self.w(di) >> 1) | 1
            self.s_step[s] = s16(self.w(di + 2))
            return di + 4
        if op == 4:
            self.s_step[s] = self.s_per[s] = 0
            return di
        if op == 5:
            self.s_wait[s] = self.w(di)
            return di + 2
        if op == 6:
            self.cur = s + 1
            if self.nword[v]:
                self.forced = True
                self.note_off(v, self.note[v])
                self.forced = False
            n = self.w(di) & 0xFF
            vel = self.w(di + 2)
            self.vel[v] = vel & 0xFF
            if vel:
                self.note_on(v, n, vel & 0xFF, 0, self.prio[v])
            return di + 4
        if op == 7:
            self.s_lstart[s] = di + 2
            return di + 2
        if op == 8:
            c = self.s_lcnt[s]
            if c:
                if c != 0xFFFF:
                    c -= 1
                self.s_lcnt[s] = c
                return self.s_lstart[s] if c else di + 4
            c = self.w(di + 2) or 0xFFFF
            self.s_lcnt[s] = c
            return self.s_lstart[s]
        if op == 10:
            self.sfx_reset(s)
            return di
        if op == 11:
            p = self.w(di)
            self.program(v, self.progmap[p & 0x7F])   # 0fb1 maps through C638 once more (09a1)
            return di + 2
        if op == 12:
            if not self.engine_off and 1 not in self.sfx_id:
                self.prio[v] = 0x60
                self.sfx_id[v] = 1
                return self.w(di)
        # 9 (and 12 when the engine is already running): end
        self.release(v)
        self.sfx_reset(s)
        return 0

    def sfx_reset(self, s):                        # 1ace:0f8d
        self.s_lstart[s] = self.s_lcnt[s] = self.s_step[s] = self.s_per[s] = self.s_slide[s] = 0
        self.s_cnt[s] = 1
        self.s_wait[s] = 1


# ---------------------------------------------------------------- MIDI / DRO writers
MT32_TO_GM = (  # approximate MT-32 -> General MIDI program map (for listening only)
    0, 1, 0, 2, 4, 4, 5, 3, 16, 17, 18, 16, 16, 19, 20, 21,
    6, 6, 6, 7, 7, 7, 8, 112, 62, 62, 63, 63, 38, 38, 39, 39,
    88, 95, 52, 98, 97, 99, 14, 54, 102, 96, 53, 102, 81, 100, 14, 80,
    48, 48, 49, 45, 41, 40, 42, 42, 43, 46, 45, 24, 25, 28, 27, 104,
    32, 32, 34, 33, 36, 37, 35, 35, 79, 73, 72, 72, 74, 75, 64, 65,
    66, 67, 71, 71, 68, 69, 70, 22, 56, 59, 57, 57, 60, 60, 58, 61,
    61, 11, 11, 98, 14, 9, 14, 13, 12, 107, 107, 77, 78, 78, 76, 76,
    47, 117, 127, 118, 118, 116, 115, 119, 115, 112, 55, 124, 123, 0, 14, 117)
PPQ = 96


def vlq(n):
    b = [n & 0x7F]
    n >>= 7
    while n:
        b.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(b))


def write_midi(path, name, tl, loop_tick, end_tick, chans=None, gm=True):
    tempo = round(PPQ * 1e6 / PIT_HZ)
    ev = [(0, -1, b'\xFF\x51\x03' + tempo.to_bytes(3, 'big'))]
    nm = name.encode('ascii')
    ev.append((0, -1, b'\xFF\x03' + vlq(len(nm)) + nm))
    if loop_tick is not None:
        ev.append((loop_tick, -1, b'\xFF\x06\x09loopStart'))
    on = set()
    for i, (t, (st, typ, ch, v, a1, a2)) in enumerate(tl):
        if chans is not None and ch not in chans:
            continue
        if typ == 0 or (typ == 1 and a2 == 0):
            if (ch, a1) in on:
                on.discard((ch, a1))
                ev.append((t, 2 * i, bytes((0x80 | ch, a1, 64))))
        elif typ == 1:
            if (ch, a1) in on:
                ev.append((t, 2 * i, bytes((0x80 | ch, a1, 64))))
            on.add((ch, a1))
            ev.append((t, 2 * i + 1, bytes((0x90 | ch, a1, a2))))
        elif typ == 4:
            p = MT32_TO_GM[a1 & 127] if gm and ch != 9 else a1 & 127
            ev.append((t, 2 * i, bytes((0xC0 | ch, p))))
    for ch, n in sorted(on):
        ev.append((end_tick, 1 << 30, bytes((0x80 | ch, n, 64))))
    if loop_tick is not None:
        ev.append((end_tick, 1 << 31, b'\xFF\x06\x07loopEnd'))
    ev.sort(key=lambda x: (x[0], x[1]))
    trk = bytearray()
    last = 0
    for t, _, b in ev:
        trk += vlq(t - last) + b
        last = t
    trk += b'\x00\xFF\x2F\x00'
    with open(path, 'wb') as f:
        f.write(b'MThd' + struct.pack('>IHHH', 6, 0, 1, PPQ))
        f.write(b'MTrk' + struct.pack('>I', len(trk)) + trk)


def write_dro(path, writes, total_ticks):
    """writes = [(tick, reg, val)] -> DOSBox raw OPL v2.0, OPL2."""
    regs = sorted({r for _, r, _ in writes})
    if len(regs) > 126:
        raise SystemExit('too many registers for a DRO codemap')
    code = {r: i for i, r in enumerate(regs)}
    sdc, ldc = len(regs), len(regs) + 1
    pairs = bytearray()
    ms_done = 0

    def delay(ms):
        while ms > 0:
            if ms > 256:
                n = min(ms // 256, 256)
                pairs.extend((ldc, n - 1))
                ms -= n * 256
            else:
                pairs.extend((sdc, ms - 1))
                ms = 0
    for t, r, v in writes:
        target = round(t * TICK_MS)
        if target > ms_done:
            delay(target - ms_done)
            ms_done = target
        pairs.extend((code[r], v))
    end = round(total_ticks * TICK_MS)
    if end > ms_done:
        delay(end - ms_done)
        ms_done = end
    hdr = (b'DBRAWOPL' + struct.pack('<HHII', 2, 0, len(pairs) // 2, ms_done)
           + bytes((0, 0, 0, sdc, ldc, len(regs))) + bytes(regs))
    with open(path, 'wb') as f:
        f.write(hdr + pairs)


# ---------------------------------------------------------------- commands
def mus_files(args):
    fs = [a for a in args if not a.startswith('--')]
    return fs or sorted(glob.glob('work/res/*/*.MUS'))


def opt(args, name, default):
    if name in args:
        return int(args[args.index(name) + 1])
    return default


def cmd_list(path):
    d = open(path, 'rb').read()
    hdr, ev = parse_mus(d)
    print('%s: %d bytes, header word %d (event bytes incl. FC; file = 2 + header + 1 pad)'
          % (path, len(d), hdr))
    for e in ev:
        print('%05X t=%5d %6.2fs  %02X %-6s %-8s %-34s%s' % (
            e['off'], e['tick'], e['tick'] / PIT_HZ, e['st'], e['args'].hex(), EV_NAME[e['typ']],
            describe(e), '' if e['delta'] is None else ' +%d' % e['delta']))
    last = ev[-1]
    print('end at 0x%X, trailing bytes: %s' % (last.get('end', len(d)), d[last.get('end', len(d)):].hex()))


def summarize(path, bank):
    d = open(path, 'rb').read()
    hdr, ev = parse_mus(d)
    notes = [e['args'][0] for e in ev if e['typ'] == 1]
    chans = sorted({e['ch'] for e in ev if e['typ'] in (0, 1)})
    pc = sorted({(e['ch'], e['args'][0]) for e in ev if e['typ'] == 4})
    tl, lt, et = unroll(d, lambda ch: bank['chmap'][ch] if bank else ch, loops=1)
    ok_hdr = hdr == len(d) - 3 and ev[-1]['st'] == 0xFC and ev[-1]['end'] == len(d) - 1
    ons = sum(1 for e in ev if e['typ'] == 1 and e['args'][1])
    offs = sum(1 for e in ev if e['typ'] == 0 or (e['typ'] == 1 and not e['args'][1]))
    return ('%-14s %5dB hdr_ok=%s pass=%6.1fs loop@%6.1fs cycle=%6.1fs notes %d-%d on/off %d/%d ch %s prog %s'
            % (os.path.basename(path), len(d), ok_hdr, ev[-1]['tick'] / PIT_HZ,
               (lt or 0) / PIT_HZ, (et - (lt or 0)) / PIT_HZ, min(notes), max(notes), ons, offs,
               chans, pc))


def cmd_midi(args, bank):
    loops = opt(args, '--loops', 1)
    os.makedirs(OUT, exist_ok=True)
    for f in mus_files([a for a in args if a != str(loops)]):
        d = open(f, 'rb').read()
        name = os.path.splitext(os.path.basename(f))[0]
        tl, lt, et = unroll(d, lambda ch: bank['chmap'][ch] if bank else ch, loops=loops)
        chans = None
        if '--adlib-only' in args and bank:
            chans = {c for c in range(16) if bank['chmap'][c] <= 13}
        out = os.path.join(OUT, name + '.mid')
        write_midi(out, name, tl, lt, et, chans, gm='--raw-programs' not in args)
        print('%s -> %s  (%.1f s, loop at %.1f s)' % (f, out, et / PIT_HZ, (lt or 0) / PIT_HZ))


def cmd_dro(args, bank):
    loops = opt(args, '--loops', 1)
    os.makedirs(OUT, exist_ok=True)
    for f in mus_files([a for a in args if a != str(loops)]):
        d = open(f, 'rb').read()
        name = os.path.splitext(os.path.basename(f))[0]
        _, lt, et = unroll(d, lambda ch: bank['chmap'][ch], loops=loops)
        writes = []
        t = [0]
        drv = AdlibDriver(bank, lambda r, v: writes.append((t[0], r, v)))
        drv.init()
        drv.music_play(d)
        while t[0] < et:
            drv.tick()
            t[0] += 1
            if not drv.flags1 & 1:
                break
        drv.music_notes_off()
        out = os.path.join(OUT, name + '.dro')
        write_dro(out, writes, t[0] + 30)
        print('%s -> %s  (%d OPL writes, %.1f s)' % (f, out, len(writes), t[0] / PIT_HZ))


SFX_OPS = {1: ('slide', 2), 2: ('slide_off', 0), 3: ('vibrato', 2), 4: ('vibrato_off', 0),
           5: ('wait', 1), 6: ('note', 2), 7: ('loop_start', 1), 8: ('loop_end', 2), 9: ('end', 0),
           10: ('reset', 0), 11: ('program', 1), 12: ('goto_engine', 1)}
SFX_CALLERS = {
    1: '0e12:23df (engine; pitch from RPM DS:129C, per frame)', 2: '0792:1c00', 3: '0e12:33f7',
    4: '0e12:4590', 6: '0792:19ca', 7: '0e12:5e70', 8: '0e12:5e70', 9: '0e12:4fc7',
    10: '0e12:0e74', 11: '0e12:01d8, 0e12:0e74, 0e12:0edb', 12: '0e12:0edb', 13: '0e12:33f7',
    14: '0e12:0db4', 15: '0977:0008 (stop 8Fh)', 16: '0977:0008 (stop 90h)',
    17: '0977:0008 (x3)', 18: '0e12:4631', 19: '0977:0008', 20: '0e12:5ffe (on/off 94h)',
    23: 'id 20 when DS:9596 != 0'}


def sfx_table(img):
    ds = img[DGROUP:DGROUP + 0x10000]
    prio = list(ds[0x915E:0x915E + 24])
    ptr = [u16(ds, 0x918E + 2 * i) for i in range(24)]
    return ds, prio, ptr


def disasm_sfx(ds, start):
    L = []
    p = start
    seen = set()
    while p not in seen and len(L) < 64:
        seen.add(p)
        op = s16(u16(ds, p))
        name, n = SFX_OPS.get(op, ('end(%d)' % op, 0))
        args = [u16(ds, p + 2 + 2 * i) for i in range(n)]
        extra = ''
        if op == 6:
            extra = '  ; MIDI note %d, vel %d' % (args[0], args[1])
        if op == 1:
            extra = '  ; %+d/256 semitone per tick' % (args[1] if args[0] else -args[1])
        if op == 3:
            extra = '  ; step %+d/256 st, reverse every %d ticks' % (s16(args[1]), args[0])
        L.append('  %04X: %-11s %s%s' % (p, name, ' '.join('%d' % a for a in args), extra))
        if op in (9, 12) or op > 12 or op < 1:
            break
        p += 2 + 2 * n
    return L


def cmd_sfx(args, bank):
    img = load_exe()
    check_tables(img)
    ds, prio, ptr = sfx_table(img)
    L = ['Sound effects: game id -> DS:918E script pointer, DS:915E priority',
         'call: 0c1c:1110 (far, arg id) / 0c1c:111d (AX = id); id | 80h stops the effect', '']
    for i in range(1, 24):
        L.append('id %2d (%02Xh) prio %02Xh script DS:%04X  callers: %s'
                 % (i, i, prio[i], ptr[i], SFX_CALLERS.get(i, '-')))
        L.extend(disasm_sfx(ds, ptr[i]))
        L.append('')
    os.makedirs(OUT, exist_ok=True)
    open(os.path.join(OUT, 'sfx.txt'), 'w').write('\n'.join(L))
    print('\n'.join(L))
    if '--dro' in args:
        for i in range(1, 24):
            writes = []
            t = [0]
            drv = AdlibDriver(bank, lambda r, v: writes.append((t[0], r, v)))
            drv.ds = ds
            drv.init()
            v = drv.sfx_start(ptr[i], prio[i])
            if v != 0xFF:
                drv.sfx_id[v] = i
            limit = int(4 * PIT_HZ)
            while t[0] < limit:
                if i == 1 and v != 0xFF and t[0] % 5 == 0:
                    # 0e12:23df once per frame (5 ticks): DS:129C (0..1500) ramped over 3 s
                    x = min(1500, t[0] * 1500 // int(3 * PIT_HZ))
                    cx = x * 4 + 0x1300 + 0x400          # +400h when DS:9486 bit 5 clear
                    drv.nword[v] = cx & 0xFF80
                drv.tick()
                t[0] += 1
                if all(s == 0xFF for s in drv.s_voice):
                    break
            drv.release(v)
            write_dro(os.path.join(OUT, 'sfx_%02d.dro' % i), writes, t[0] + 30)
        print('sfx DROs written to %s' % OUT)


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    cmd, rest = a[0], a[1:]
    bank = parse_instr()[0] if os.path.exists(INSTR) else None
    if cmd == 'list':
        cmd_list(rest[0])
    elif cmd == 'midi':
        cmd_midi(rest, bank)
    elif cmd == 'dro':
        cmd_dro(rest, bank)
    elif cmd == 'instr':
        os.makedirs(OUT, exist_ok=True)
        print(dump_instr(rest[0] if rest else INSTR, os.path.join(OUT, 'instr.txt')))
    elif cmd == 'sfx':
        cmd_sfx(rest, bank)
    elif cmd == 'all':
        os.makedirs(OUT, exist_ok=True)
        check_tables(load_exe())
        dump_instr(INSTR, os.path.join(OUT, 'instr.txt'))
        for f in mus_files([]):
            name = os.path.splitext(os.path.basename(f))[0]
            import io
            import contextlib
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                cmd_list(f)
            open(os.path.join(OUT, name + '.txt'), 'w').write(buf.getvalue())
            print(summarize(f, bank))
        cmd_midi([], bank)
        cmd_dro([], bank)
        cmd_sfx(['--dro'], bank)
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
