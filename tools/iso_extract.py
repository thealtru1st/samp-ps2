#!/usr/bin/env python3
"""Minimal ISO9660 reader: list the tree, or extract one file. Usage:
   iso_extract.py ISO ls [path]      iso_extract.py ISO get /PATH/FILE OUT"""
import struct, sys
S = 2048
def rec_iter(f, lba, size):
    off = 0
    f.seek(lba * S); data = f.read(size)
    while off < size:
        n = data[off]
        if n == 0:
            off = (off // S + 1) * S
            continue
        r = data[off:off + n]
        ext, = struct.unpack_from('<I', r, 2); sz, = struct.unpack_from('<I', r, 10)
        flags = r[25]; nl = r[32]; name = r[33:33 + nl]
        off += n
        if name in (b'\0', b'\1'): continue
        yield name.decode('latin1').split(';')[0], ext, sz, bool(flags & 2)
def root(f):
    f.seek(16 * S); p = f.read(S)
    assert p[1:6] == b'CD001', 'not iso9660'
    return struct.unpack_from('<I', p, 156 + 2)[0], struct.unpack_from('<I', p, 156 + 10)[0]
def walk(f, lba, size, pre=''):
    for n, e, s, d in rec_iter(f, lba, size):
        yield pre + '/' + n, e, s, d
        if d: yield from walk(f, e, s, pre + '/' + n)
if __name__ == '__main__':
    iso, cmd = sys.argv[1:3]
    with open(iso, 'rb') as f:
        l, s = root(f)
        if cmd == 'ls':
            for p, e, z, d in walk(f, l, s):
                print(f"{'D' if d else 'F'} {e:8d} {z:11d} {p}")
        else:
            want, out = sys.argv[3:5]
            for p, e, z, d in walk(f, l, s):
                if p.upper() == want.upper():
                    f.seek(e * S)
                    with open(out, 'wb') as o:
                        left = z
                        while left:
                            b = f.read(min(left, 1 << 20)); o.write(b); left -= len(b)
                    print('wrote', out, z); break
            else: sys.exit('not found')
