#!/usr/bin/env python3
"""Captures one real reply per opcode from the local server into fixtures/.
Four datagrams, sent only to the address given. Written independently of the C
parser so that the two can check each other. usage: capture.py <ip> <port> <dir>"""
import os
import socket
import struct
import sys

ip, port, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
hdr = b'SAMP' + socket.inet_aton(ip) + struct.pack('<H', port)
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(2)
for op, extra in ((b'i', b''), (b'r', b''), (b'c', b''), (b'p', b'\xde\xad\xbe\xef')):
    pkt = hdr + op + extra
    s.sendto(pkt, (ip, port))
    d, a = s.recvfrom(4096)
    assert d[:11] == pkt[:11], (d[:11], pkt[:11])
    with open(os.path.join(out, 'reply_%s.bin' % op.decode()), 'wb') as f:
        f.write(d)
    print(op.decode(), len(d), 'bytes from', a, d[11:60].hex())

with open(os.path.join(out, 'meta.txt'), 'w', newline='\n') as f:
    f.write('%s %d\n' % (ip, port))

# An independent reading of the info reply, for the C test to compare against.
d = open(os.path.join(out, 'reply_i.bin'), 'rb').read()
o = 11
pw, players, maxp = struct.unpack_from('<BHH', d, o)
o += 5
strs = []
for _ in range(3):
    (n,) = struct.unpack_from('<I', d, o)
    o += 4
    strs.append(d[o:o + n].decode('latin1'))
    o += n
print('python parse:', pw, players, maxp, strs)
with open(os.path.join(out, 'expect_i.txt'), 'w', newline='\n') as f:
    f.write('%d\n%d\n%d\n%s\n%s\n%s\n' % (pw, players, maxp, strs[0], strs[1], strs[2]))
