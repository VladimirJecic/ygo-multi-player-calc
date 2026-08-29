#!/usr/bin/env python3
"""Rebuild an APK by raw-copying untouched entries and replacing/adding a few.
Usage: mkapk.py <src.apk> <out.apk> name=path [name=path ...]
Entries whose name matches a replacement are substituted; unknown names are appended.
"""
import sys, os, struct, zipfile, zlib

src, out = sys.argv[1], sys.argv[2]
repl = {}
for a in sys.argv[3:]:
    n, p = a.split('=', 1)
    repl[n] = p

zin = zipfile.ZipFile(src)
infos = sorted(zin.infolist(), key=lambda i: i.header_offset)
orig_ct = {i.filename: i.compress_type for i in infos}
fin = open(src, 'rb')
fout = open(out, 'wb')
central = []

def local_header_len(off):
    fin.seek(off)
    h = fin.read(30)
    assert h[:4] == b'PK\x03\x04', 'bad local header'
    nlen, elen = struct.unpack_from('<HH', h, 26)
    return 30 + nlen + elen, struct.unpack_from('<H', h, 6)[0]

written = set()
for i in infos:
    if i.filename in repl:
        continue
    hlen, flags = local_header_len(i.header_offset)
    total = hlen + i.compress_size
    if flags & 0x8:  # data descriptor follows
        fin.seek(i.header_offset + total)
        d = fin.read(16)
        total += 16 if d[:4] == b'PK\x07\x08' else 12
    new_off = fout.tell()
    fin.seek(i.header_offset)
    remaining = total
    while remaining:
        chunk = fin.read(min(1 << 20, remaining))
        if not chunk:
            raise SystemExit('truncated: ' + i.filename)
        fout.write(chunk)
        remaining -= len(chunk)
    central.append((i, new_off))
    written.add(i.filename)

def add(name, path, compress):
    data = open(path, 'rb').read()
    crc = zlib.crc32(data) & 0xffffffff
    if compress:
        co = zlib.compressobj(9, zlib.DEFLATED, -15)
        blob = co.compress(data) + co.flush()
        method = 8
    else:
        blob = data
        method = 0
    off = fout.tell()
    nb = name.encode()
    fout.write(struct.pack('<IHHHHHIIIHH', 0x04034b50, 20, 0, method, 0, 0,
                           crc, len(blob), len(data), len(nb), 0))
    fout.write(nb)
    fout.write(blob)
    zi = zipfile.ZipInfo(name)
    zi.compress_type = method
    zi.CRC = crc
    zi.compress_size = len(blob)
    zi.file_size = len(data)
    zi.external_attr = 0o644 << 16
    central.append((zi, off))

for name, path in repl.items():
    # keep the original compression choice; new files default to deflate
    compress = orig_ct.get(name, 8) == 8
    add(name, path, compress)

cd_off = fout.tell()
for zi, off in central:
    nb = zi.filename.encode()
    fout.write(struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, 20, 20, 0,
                           zi.compress_type, 0, 0, zi.CRC, zi.compress_size,
                           zi.file_size, len(nb), 0, 0, 0, 0,
                           getattr(zi, 'external_attr', 0), off))
    fout.write(nb)
cd_size = fout.tell() - cd_off
fout.write(struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, len(central), len(central),
                       cd_size, cd_off, 0))
fout.close()
print('wrote %s  entries=%d  replaced=%s' % (out, len(central), list(repl)))
