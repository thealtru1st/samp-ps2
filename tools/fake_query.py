#!/usr/bin/env python3
"""Test fixture, NOT a SA-MP server: answers only the 'i' query so the EE network
path can be proven on its own. Prints each datagram it sees."""
import socket, struct, sys, time

host = b'PS2 path test'
gm, lang = b'none', b'none'
info = struct.pack('<BHH', 0, 3, 50)
for s in (host, gm, lang):
    info += struct.pack('<I', len(s)) + s

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(('127.0.0.1', 7777))
s.settimeout(1)
end = time.time() + (float(sys.argv[1]) if len(sys.argv) > 1 else 120)
n = 0
print('listening', flush=True)
while time.time() < end:
    try:
        d, a = s.recvfrom(2048)
    except socket.timeout:
        continue
    except ConnectionResetError:
        continue
    n += 1
    if n <= 5 or n % 50 == 0:
        print('rx #%d from %s: %s' % (n, a, d[:16].hex()), flush=True)
    if d[:4] == b'SAMP' and len(d) >= 11 and d[10:11] == b'i':
        s.sendto(d[:11] + info, a)
print('done, datagrams seen:', n, flush=True)
