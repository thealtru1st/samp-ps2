#!/usr/bin/env python3
"""Run SCM in the live game over PINE.
  scmcmd.py 016A i32:500 i8:1
Args: 4-hex-digit opcode, i8:N, i32:N, f:X, s8:TEXT (8-byte string), s:TEXT (var length),
l<N> (local var). Locals: l0 player, l1 char; l2/l3 come back as r2/r3."""
import struct
import sys
import time

sys.path.insert(0, 'tools')
from pine import Pine, ST

CMD = ST + 64 + 4096 + 4
b = bytearray()
for a in sys.argv[1:]:
    if a.startswith('i8:'):
        b += bytes([4, int(a[3:]) & 255])
    elif a.startswith('i32:'):
        b += bytes([1]) + struct.pack('<i', int(a[4:]))
    elif a.startswith('f:'):
        b += bytes([6]) + struct.pack('<f', float(a[2:]))
    elif a.startswith('s8:'):
        b += bytes([9]) + a[3:].encode().ljust(8, bytes(1))[:8]
    elif a.startswith('s:'):
        t = a[2:].encode()
        b += bytes([0x0E, len(t)]) + t
    elif a[0] in 'lr' and a[1:].isdigit():
        b += bytes([3]) + struct.pack('<H', int(a[1:]))
    elif len(a) == 4:
        b += struct.pack('<H', int(a, 16))
    else:
        sys.exit('bad arg ' + a)

p = Pine()
pad = bytes(b) + bytes(-len(b) % 4)
for i in range(0, len(pad), 4):
    p.w32(CMD + 4 + 16 + i, struct.unpack('<I', pad[i:i + 4])[0])
p.w32(CMD, len(b))
for _ in range(50):
    if p.r32(CMD) == 0:
        break
    time.sleep(0.05)
r2, r3 = struct.unpack('<ii', struct.pack('<II', p.r32(CMD + 4), p.r32(CMD + 8)))
print('done' if p.r32(CMD) == 0 else 'NOT EXECUTED', 'r2=%d r3=%d' % (r2, r3))
