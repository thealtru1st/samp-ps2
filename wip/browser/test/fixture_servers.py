#!/usr/bin/env python3
"""Local stand-ins, loopback only, so nothing public is queried.

HTTP on 127.0.0.1:<http_port>, one behaviour per path, covering what a real list
host might do. And <n> fake SA-MP servers on 127.0.0.1:<udp_base>+k, with some
deliberately lossy, silent or malformed. Those are NOT real servers: they exist to
test scheduling. Parsing is tested against the real server separately.

usage: fixture_servers.py <http_port> <udp_base> <n>"""
import random
import selectors
import socket
import struct
import sys
import threading
import time

http_port, udp_base, n_srv = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])


def behaviour(k):
    if k % 11 == 3:
        return 'silent'
    if k % 11 == 5:
        return 'garbage'    # always malformed, so it must end up offline
    if k % 7 == 2:
        return 'lossy'      # drops the first two datagrams of each kind
    if k % 13 == 6:
        return 'noping'     # answers 'i' but never 'p'
    return 'ok'


def udp_main():
    sel = selectors.DefaultSelector()
    seen = {}
    for k in range(n_srv):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.bind(('127.0.0.1', udp_base + k))
        s.setblocking(False)
        sel.register(s, selectors.EVENT_READ, k)
    while True:
        for key, _ in sel.select():
            s, k = key.fileobj, key.data
            try:
                d, a = s.recvfrom(2048)
            except OSError:
                continue
            if d[:4] != b'SAMP' or len(d) < 11:
                continue
            b, op = behaviour(k), d[10:11]
            if b == 'silent':
                continue
            if b == 'lossy':
                c = seen.get((k, op), 0)
                seen[(k, op)] = c + 1
                if c < 2:
                    continue
            if b == 'noping' and op == b'p':
                continue
            if b == 'garbage':
                s.sendto(d[:11] + b'\x00\x05\x00\x32\x00\xff\xff\xff\x7f', a)
                continue
            if op == b'i':
                name = ('Fixture %d' % k).encode()
                players = {8: 64, 10: 65}.get(k, k % 50)
                body = struct.pack('<BHH', k % 2, players, 100 if k in (8, 10) else 50)
                for t in (name, b'test mode', b'English'):
                    body += struct.pack('<I', len(t)) + t
                s.sendto(d[:11] + body, a)
            elif op == b'p':
                s.sendto(d[:15], a)
            elif op == b'r':
                s.sendto(d[:11] + struct.pack('<H', 2) + b'\x07version\x03omp' + b'\x07mapname\x02SA', a)
            elif op == b'c' and k in (8, 10):
                # 8: the largest reply we allow, over the MTU but inside NetPacket.
                # 10: over the limit. Asking for it would overrun PCSX2's buffer.
                n = 64 if k == 8 else 100
                body = struct.pack('<H', n)
                for i in range(n):
                    body += b'\x18' + (b'Player_%017d' % i) + struct.pack('<i', i)
                s.sendto(d[:11] + body, a)
            elif op == b'c':
                s.sendto(d[:11] + struct.pack('<H', 1) + b'\x04Test' + struct.pack('<i', -5), a)


def list_body(n):
    return ''.join('127.0.0.1:%d\n' % (udp_base + k) for k in range(n)).encode()


def big_body(size):
    """Deterministic, and position dependent so reordering or loss changes the hash."""
    r = random.Random(1234)
    return r.randbytes(size)


def send_all(c, b):
    try:
        c.sendall(b)
    except OSError:
        pass


def handle(c):
    c.settimeout(10)
    req = b''
    try:
        while b'\r\n\r\n' not in req:
            d = c.recv(4096)
            if not d:
                return
            req += d
    except OSError:
        return
    line = req.split(b'\r\n')[0].decode('latin1')
    path = line.split(' ')[1] if ' ' in line else ''
    ok = b'HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n'
    if line.split(' ')[-1] != 'HTTP/1.0' or b'\r\nHost: ' not in req:
        send_all(c, b'HTTP/1.1 400 Bad Request\r\n\r\n')
    elif path == '/list':
        send_all(c, ok + b'\r\n' + list_body(n_srv))
    elif path == '/list-cl':
        b = list_body(n_srv)
        send_all(c, ok + b'Content-Length: %d\r\n\r\n' % len(b) + b)
    elif path == '/slowdrip':       # headers and body cut at awkward points
        msg = ok + b'\r\n' + list_body(20)
        for i in range(0, len(msg), 7):
            send_all(c, msg[i:i + 7])
            time.sleep(0.004)
    elif path == '/messy':          # CRLF, junk, an overlong line, no final newline
        b = (b'127.0.0.1:%d\r\n# comment\r\n\r\n' % udp_base + b'x' * 5000 + b'\n'
             b'999.1.1.1:7777\n1.2.3.4:99999\n1.2.3:7777\n127.0.0.1:%d' % (udp_base + 1))
        send_all(c, ok + b'\r\n' + b)
    elif path == '/private':        # must be filtered out unless explicitly allowed
        send_all(c, ok + b'\r\n127.0.0.1:7777\n10.0.0.1:7777\n192.168.1.1:7777\n'
                 b'172.16.0.1:7777\n192.0.2.1:7777\n224.0.0.1:7777\n8.8.8.8:7777\n')
    elif path.startswith('/big/'):
        b = big_body(int(path[5:]))
        send_all(c, ok + b'\r\n' + b)
    elif path.startswith('/bigcl/'):
        b = big_body(int(path[7:]))
        send_all(c, ok + b'Content-Length: %d\r\n\r\n' % len(b) + b)
    elif path == '/longheader':
        send_all(c, ok + b'X-Long: ' + b'a' * 3000 + b'\r\nContent-Length: 13\r\n\r\n127.0.0.1:80\n')
    elif path == '/404':
        send_all(c, b'HTTP/1.1 404 Not Found\r\nContent-Length: 4\r\n\r\nnope')
    elif path == '/301':
        send_all(c, b'HTTP/1.1 301 Moved\r\nLocation: https://example.invalid/\r\nContent-Length: 0\r\n\r\n')
    elif path == '/chunked':
        send_all(c, ok + b'Transfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n')
    elif path == '/shortcl':
        send_all(c, ok + b'Content-Length: 1000\r\n\r\nonly this')
    elif path == '/rstmid':         # a hard reset part way through a body
        send_all(c, ok + b'\r\n' + list_body(5))
        time.sleep(0.15)
        c.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
    elif path == '/notfhttp':
        send_all(c, b'SSH-2.0-OpenSSH_9.6\r\n\r\n')
    elif path == '/html':           # a 200 that is not a list, like a captive portal
        send_all(c, ok + b'\r\n<html><body>Not a list</body></html>\n')
    elif path == '/empty':
        send_all(c, ok + b'Content-Length: 0\r\n\r\n')
    elif path == '/hang':
        time.sleep(8)
    else:
        send_all(c, b'HTTP/1.1 404 Not Found\r\n\r\n')


def http_main():
    l = socket.socket()
    l.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    l.bind(('127.0.0.1', http_port))
    l.listen(32)
    print('fixtures ready', flush=True)
    while True:
        c, _ = l.accept()

        def run(c=c):
            try:
                handle(c)
            finally:
                c.close()
        threading.Thread(target=run, daemon=True).start()


def blackhole_main():
    """A full accept queue: further SYNs are ignored, so connect() just hangs. That
    is the closest Linux gets to a host that never answers."""
    l = socket.socket()
    l.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    l.bind(('127.0.0.1', http_port + 2))
    l.listen(0)
    keep = []
    for _ in range(4):
        c = socket.socket()
        c.setblocking(False)
        try:
            c.connect(('127.0.0.1', http_port + 2))
        except BlockingIOError:
            pass
        keep.append(c)
    while True:
        time.sleep(60)


threading.Thread(target=udp_main, daemon=True).start()
threading.Thread(target=blackhole_main, daemon=True).start()
http_main()
