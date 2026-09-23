"""Unpack Microsoft EXEPACK-compressed DOS executables into plain MZ files."""
import struct, sys

def unpack(path, out_path):
    data = open(path, 'rb').read()
    (magic, cblp, cp, crlc, cparhdr, minalloc, maxalloc, ss, sp, csum, ip, cs,
     lfarlc, ovno) = struct.unpack_from('<14H', data, 0)
    assert magic == 0x5A4D
    file_len = cp * 512 - (512 - cblp if cblp else 0)
    hdr_len = cparhdr * 16
    image = bytearray(data[hdr_len:file_len])

    ep = cs * 16  # EXEPACK header lives at CS:0000
    sig_off = ep + 16 if image[ep + 16:ep + 18] == b'RB' else ep + 14
    assert image[sig_off:sig_off + 2] == b'RB', 'no EXEPACK signature'
    fields = struct.unpack_from('<%dH' % ((sig_off - ep) // 2), image, ep)
    real_ip, real_cs, _mem, exepack_size, real_sp, real_ss, dest_len = fields[:7]
    skip_len = fields[7] if len(fields) > 7 else 1

    # Relocation table follows the error message in the stub.
    msg = b'Packed file is corrupt'
    stub = bytes(image[ep:ep + exepack_size])
    rel = stub.index(msg) + len(msg)
    relocs = []
    for seg in range(16):
        n, = struct.unpack_from('<H', stub, rel); rel += 2
        for _ in range(n):
            off, = struct.unpack_from('<H', stub, rel); rel += 2
            relocs.append((seg * 0x1000, off))

    packed = image[:ep - (skip_len - 1) * 16]
    out_len = dest_len * 16
    buf = bytearray(max(len(packed), out_len))
    buf[:len(packed)] = packed
    src, dst = len(packed), out_len
    while buf[src - 1] == 0xFF:
        src -= 1
    while True:
        cmd = buf[src - 1]; src -= 1
        length = buf[src - 1] << 8 | buf[src - 2]; src -= 2
        if cmd & 0xFE == 0xB0:
            fill = buf[src - 1]; src -= 1
            dst -= length
            buf[dst:dst + length] = bytes([fill]) * length
        elif cmd & 0xFE == 0xB2:
            src -= length; dst -= length
            buf[dst:dst + length] = bytes(buf[src:src + length])
        else:
            raise ValueError('bad command %02x at %x' % (cmd, src))
        if cmd & 1:
            break
    body = bytes(buf[:out_len])

    hdr_size = 0x1C + 4 * len(relocs)
    hdr_size = (hdr_size + 511) // 512 * 512
    total = hdr_size + len(body)
    extra = (total - len(data)) // 16
    hdr = struct.pack('<14H', 0x5A4D, total % 512, (total + 511) // 512, len(relocs),
                      hdr_size // 16, max(0, minalloc - extra), 0xFFFF, real_ss, real_sp,
                      0, real_ip, real_cs, 0x1C, 0)
    hdr += b''.join(struct.pack('<HH', off, seg) for seg, off in relocs)
    hdr = hdr.ljust(hdr_size, b'\0')
    open(out_path, 'wb').write(hdr + body)
    print('%s: packed %d -> %d bytes, entry %04x:%04x, %d relocs' %
          (path, len(packed), len(body), real_cs, real_ip, len(relocs)))

if __name__ == '__main__':
    unpack(sys.argv[1], sys.argv[2])
