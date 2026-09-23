"""16-bit x86 helpers for unpacked MZ images.
  x86dis.py EXE find <hex bytes>              -> image offsets of a byte pattern
  x86dis.py EXE dis <addr> <len>              -> disassembly
<addr> is SSSS:OOOO (segment as stored in the file; branch targets are then shown as offsets in that
segment and far-call targets as SSSS:OOOO) or a bare hex image offset (segment 0).
"""
import struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_16


def load(path):
    d = open(path, 'rb').read()
    hdr = struct.unpack_from('<H', d, 8)[0] * 16
    return d[hdr:]


def main():
    img = load(sys.argv[1])
    cmd = sys.argv[2]
    if cmd == 'find':
        pat = bytes.fromhex(''.join(sys.argv[3:]))
        i = img.find(pat)
        while i >= 0:
            print('%05x' % i)
            i = img.find(pat, i + 1)
    elif cmd == 'dis':
        addr, n = sys.argv[3], int(sys.argv[4], 0)
        seg, off = (int(x, 16) for x in addr.split(':')) if ':' in addr else (0, int(addr, 16))
        start = seg * 16 + off
        md = Cs(CS_ARCH_X86, CS_MODE_16)
        for ins in md.disasm(img[start:start + n], off):
            extra = ''
            if ins.bytes[0] == 0x9A:
                o, s = struct.unpack_from('<HH', ins.bytes, 1)
                extra = '  ; %04x:%04x (image %05x)' % (s, o, s * 16 + o)
            print('%04x:%04x %05x  %-20s %s %s%s' % (seg, ins.address, seg * 16 + ins.address,
                                                   ins.bytes.hex(), ins.mnemonic, ins.op_str, extra))


if __name__ == '__main__':
    main()
