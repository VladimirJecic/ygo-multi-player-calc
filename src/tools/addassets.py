#!/usr/bin/env python3
"""Raw-copy one APK, then append the entries a second APK has and it does not.

The modded APK was built from a merged base+split that had lost 977 of the
game's Unity asset bundles, which is why some Calculator Designs drew with no
textures at all.  This puts them back without touching anything that works:
every entry is copied byte for byte, compression and all.

Usage: addassets.py <ours.apk> <donor.apk> <out.apk>
"""
import sys, struct, zipfile

ours, donor, out = sys.argv[1], sys.argv[2], sys.argv[3]
fout = open(out, 'wb')
central = []
have = set()

def copy_from(path, only_missing):
    zin = zipfile.ZipFile(path)
    infos = sorted(zin.infolist(), key=lambda i: i.header_offset)
    fin = open(path, 'rb')
    added = 0
    for i in infos:
        if only_missing and i.filename in have:
            continue
        if i.filename in have:
            continue
        fin.seek(i.header_offset)
        h = fin.read(30)
        assert h[:4] == b'PK\x03\x04', 'bad local header in ' + path
        nlen, elen = struct.unpack_from('<HH', h, 26)
        flags = struct.unpack_from('<H', h, 6)[0]
        total = 30 + nlen + elen + i.compress_size
        if flags & 0x8:
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
        have.add(i.filename)
        added += 1
    fin.close()
    return added

a = copy_from(ours, False)
b = copy_from(donor, True)

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
print('wrote %s  kept=%d  restored=%d  total=%d' % (out, a, b, len(central)))
