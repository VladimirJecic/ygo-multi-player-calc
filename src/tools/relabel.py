#!/usr/bin/env python3
"""Rename the app in resources.arsc, in place in the string pool.

usage: relabel.py <in.arsc> <out.arsc> <old> <new>

The label is a plain entry in the table's global string pool, so the pool is
rebuilt with the new text and the two enclosing chunk sizes are corrected.
Safe to grow: resources.arsc is the third zip entry and the only one that has
to stay 4-byte aligned, so its own offset does not move, and everything after
it is deflated and has no alignment requirement.
"""
import struct, sys


def read_pool(d, off):
    _, hdr, size = struct.unpack_from('<HHI', d, off)
    cnt, styc, flags, sstart, stystart = struct.unpack_from('<IIIII', d, off + 8)
    utf8 = bool(flags & (1 << 8))
    offs = [struct.unpack_from('<I', d, off + hdr + 4 * i)[0] for i in range(cnt)]
    out = []
    for o in offs:
        p = off + sstart + o
        if utf8:
            n = d[p]; p += 1
            if n & 0x80: p += 1
            n2 = d[p]; p += 1
            if n2 & 0x80:
                n2 = ((n2 & 0x7f) << 8) | d[p]; p += 1
            out.append(d[p:p + n2].decode('utf-8'))
        else:
            n = struct.unpack_from('<H', d, p)[0]; p += 2
            if n & 0x8000:
                n = ((n & 0x7fff) << 16) | struct.unpack_from('<H', d, p)[0]; p += 2
            out.append(d[p:p + n * 2].decode('utf-16-le'))
    return out, utf8, styc, flags, hdr, size


def enc_len(n):
    return bytes([n]) if n < 0x80 else bytes([0x80 | (n >> 8), n & 0xff])


def build_pool(strings, utf8, styc, flags, hdr, styles_blob):
    assert utf8 and styc == 0, 'only the UTF-8, style-free pool is handled'
    data, offs = bytearray(), []
    for s in strings:
        offs.append(len(data))
        b = s.encode('utf-8')
        data += enc_len(len(s)) + enc_len(len(b)) + b + b'\0'
    while len(data) % 4:
        data += b'\0'
    sstart = hdr + 4 * len(strings)
    size = sstart + len(data)
    out = bytearray(struct.pack('<HHI', 0x0001, hdr, size))
    out += struct.pack('<IIIII', len(strings), 0, flags, sstart, 0)
    out += b'\0' * (hdr - len(out))
    for o in offs:
        out += struct.pack('<I', o)
    return bytes(out + data)


def main():
    src, dst, old, new = sys.argv[1:5]
    d = bytearray(open(src, 'rb').read())
    ttype, thdr, tsize = struct.unpack_from('<HHI', d, 0)
    assert ttype == 0x0002, 'not a resource table'
    strings, utf8, styc, flags, hdr, psize = read_pool(d, 12)
    if old not in strings:
        sys.exit(f'relabel: {old!r} is not in the pool')
    hits = [i for i, s in enumerate(strings) if s == old]
    if len(hits) != 1:
        sys.exit(f'relabel: {old!r} appears {len(hits)} times, refusing to guess')
    strings[hits[0]] = new
    pool = build_pool(strings, utf8, styc, flags, hdr, b'')
    out = bytearray(d[:12]) + pool + d[12 + psize:]
    struct.pack_into('<I', out, 4, len(out))
    open(dst, 'wb').write(out)
    print(f'relabel: {old!r} -> {new!r} at index {hits[0]}, '
          f'{len(d)} -> {len(out)} bytes')


main()
