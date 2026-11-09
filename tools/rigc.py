#!/usr/bin/env python3
"""Client for tools/rig.py, as a library for test scripts or from the shell:
    python tools/rigc.py st | log | shot NAME | chat TEXT | key K[,K] | scm ARGS... | pos
Screenshots land in wip/ingame/shots/. SCM args are as in tools/scmcmd.py."""
import json
import os
import socket
import struct
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS = os.path.join(ROOT, 'wip', 'ingame', 'shots')


def scm_bytes(args):
    b = bytearray()
    for a in args:
        if a.startswith('i8:'):
            b += bytes([4, int(a[3:]) & 255])
        elif a.startswith('i16:'):
            b += bytes([5]) + struct.pack('<h', int(a[4:]))
        elif a.startswith('i32:'):
            b += bytes([1]) + struct.pack('<i', int(a[4:]))
        elif a.startswith('f:'):
            b += bytes([6]) + struct.pack('<f', float(a[2:]))
        elif a.startswith('s8:'):
            b += bytes([9]) + a[3:].encode().ljust(8, bytes(1))[:8]
        elif a.startswith('s:'):
            t = a[2:].encode()
            b += bytes([0x0E, len(t)]) + t
        elif a[0] == 'l' and a[1:].isdigit():
            b += bytes([3]) + struct.pack('<H', int(a[1:]))
        elif len(a) == 4:
            b += struct.pack('<H', int(a, 16))
        else:
            raise ValueError(a)
    return bytes(b)


class Rig:
    def __init__(s):
        s.c = socket.create_connection(('127.0.0.1', 28120), timeout=30)
        s.f = s.c.makefile('rb')

    def x(s, **k):
        s.c.sendall((json.dumps(k) + '\n').encode())
        r = json.loads(s.f.readline())
        if 'err' in r:
            raise RuntimeError(r['err'])
        return r['ok']

    def key(s, k, hold=0.2, gap=0.2): return s.x(c='key', k=k, hold=hold, gap=gap)
    def down(s, k): return s.x(c='down', k=k)
    def up(s, k): return s.x(c='up', k=k)
    def chat(s, t): return s.x(c='chat', t=t)
    def push(s, t): return s.x(c='push', t=t)
    def st(s): return s.x(c='st')
    def log(s): return s.x(c='log')
    def r32(s, a): return s.x(c='r32', a=a)
    def r8(s, a): return s.x(c='r8', a=a)
    def read(s, a, n): return bytes.fromhex(s.x(c='read', a=a, n=n))
    def w32(s, a, v): return s.x(c='w32', a=a, v=v)
    def w8(s, a, v): return s.x(c='w8', a=a, v=v)
    def rf(s, a): return struct.unpack('<f', struct.pack('<I', s.r32(a)))[0]
    def wf(s, a, v): return s.w32(a, struct.unpack('<I', struct.pack('<f', v))[0])
    def editopen(s): return s.x(c='editopen')
    def pad(s, **v): return s.x(c='pad', v=v)

    def call(s, fn, *args):
        """Calls a game function on the game's thread. Returns v0."""
        base = 0x83F00000 + 64 + 4096 + 612 + 44   # dbg_fn, see status.h
        for i, a in enumerate(args):
            s.w32(base + 8 + i * 4, a)
        s.w32(base, fn)
        for _ in range(100):
            if s.r32(base) == 0:
                return s.r32(base + 4)
            time.sleep(0.02)
        raise RuntimeError('call timed out')

    DBG_BUF = 0x83F00000 + 64 + 4096 + 612 + 44 + 24

    def scm(s, *args):
        return s.x(c='scm', h=scm_bytes(args).hex())

    def shot(s, name):
        if not name.endswith('.png'):
            name += '.png'
        p = os.path.join(SHOTS, name)
        s.x(c='shot', p=p)
        return p

    def fps(s, secs=2.0):
        a = s.st()['frames']
        time.sleep(secs)
        return (s.st()['frames'] - a) / secs


R = Rig


if __name__ == '__main__':
    r = Rig()
    a = sys.argv[1:]
    k = a[0]
    if k == 'st':
        print(r.st())
    elif k == 'log':
        print(r.log()[-int(a[1]) if len(a) > 1 else -2500:])
    elif k == 'shot':
        print(r.shot(a[1]))
    elif k == 'chat':
        print(r.chat(' '.join(a[1:])))
    elif k == 'key':
        print(r.key(a[1], *(float(x) for x in a[2:])))
    elif k == 'scm':
        print(r.scm(*a[1:]))
    elif k == 'pos':
        print(r.st()['pos'])
    elif k == 'fps':
        print(r.fps())
    else:
        sys.exit('?')
