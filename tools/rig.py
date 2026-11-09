#!/usr/bin/env python3
"""Test rig daemon: owns the one PINE connection, keeps KB_ALIVE fresh, and takes commands
from test scripts over 127.0.0.1:28120 (one JSON object per line).

    python tools/rig.py            # run in the background
    from rigc import R             # scripts talk to it through tools/rigc.py

Keys and screenshots go straight to the PCSX2 window through ctypes, so nothing needs focus."""
import ctypes
import ctypes.wintypes as wt
import json
import os
import socketserver
import struct
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pine import Pine, ST
import kbd

PORT = 28120
BASE = ST + 64 + 4096
CMD = BASE + 4
DBG_PAD = kbd.EDIT_TEXT + 160          # see status.h
PAD_NAMES = ['lx', 'ly', 'rx', 'ry', 'l1', 'l2', 'r1', 'r2', 'up', 'down', 'left', 'right',
             'start', 'select', 'square', 'triangle', 'cross', 'circle', 'l3', 'r3']

u = ctypes.WinDLL('user32', use_last_error=True)
g = ctypes.WinDLL('gdi32')
u.SetProcessDPIAware()
u.FindWindowW.restype = wt.HWND
u.FindWindowW.argtypes = [wt.LPCWSTR, wt.LPCWSTR]
ENUM = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
u.EnumChildWindows.argtypes = [wt.HWND, ENUM, wt.LPARAM]
u.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
u.MapVirtualKeyW.argtypes = [wt.UINT, wt.UINT]
u.GetDC.restype = wt.HDC
u.GetDC.argtypes = [wt.HWND]
u.ReleaseDC.argtypes = [wt.HWND, wt.HDC]
u.PrintWindow.argtypes = [wt.HWND, wt.HDC, wt.UINT]
g.CreateCompatibleDC.restype = wt.HDC
g.CreateCompatibleDC.argtypes = [wt.HDC]
g.CreateCompatibleBitmap.restype = wt.HBITMAP
g.CreateCompatibleBitmap.argtypes = [wt.HDC, ctypes.c_int, ctypes.c_int]
g.SelectObject.argtypes = [wt.HDC, wt.HGDIOBJ]
g.DeleteObject.argtypes = [wt.HGDIOBJ]
g.DeleteDC.argtypes = [wt.HDC]
g.GetDIBits.argtypes = [wt.HDC, wt.HBITMAP, wt.UINT, wt.UINT, ctypes.c_void_p, ctypes.c_void_p, wt.UINT]

VK = {'Return': 0x0D, 'Up': 0x26, 'Down': 0x28, 'Left': 0x25, 'Right': 0x27, 'Backspace': 8,
      'Shift': 0x10, 'Tab': 9, 'Space': 0x20}

state = {'p': None, 'seq': 0}
L = threading.RLock()


def pine():
    if state['p'] is None:
        state['p'] = Pine(timeout=6)
    return state['p']


def drop():
    try:
        state['p'].c.close()
    except Exception:
        pass
    state['p'] = None


def guarded(fn):
    with L:
        try:
            return fn(pine())
        except (OSError, ConnectionError, IOError, struct.error):
            drop()
            raise


def alive():
    while True:
        try:
            def tick(p):
                p.w32(kbd.KB_ALIVE, p.r32(ST + 4))
            guarded(tick)
        except Exception:
            time.sleep(1)
        time.sleep(0.2)


def wnd():
    h = u.FindWindowW(None, 'Grand Theft Auto - San Andreas')
    if not h:
        raise RuntimeError('no window')
    kids = []

    def cb(x, _):
        kids.append(x)
        return True
    u.EnumChildWindows(h, ENUM(cb), 0)
    return h, kids


def key_msg(name, down):
    h, kids = wnd()
    vk = VK.get(name) or ord(name.upper())
    sc = u.MapVirtualKeyW(vk, 0)
    ext = (1 << 24) if 0x25 <= vk <= 0x28 else 0
    lp = (1 | (sc << 16) | ext) if down else (0xC0000001 | (sc << 16) | ext)
    for t in [h] + kids:
        u.PostMessageW(t, 0x100 if down else 0x101, vk, lp)


def key(name, hold=0.2):
    key_msg(name, True)
    time.sleep(hold)
    key_msg(name, False)


def shot(path):
    from PIL import Image
    h, _ = wnd()
    r = wt.RECT()
    u.GetClientRect(h, ctypes.byref(r))
    w, ht = r.right - r.left, r.bottom - r.top
    sdc = u.GetDC(h)
    dc = g.CreateCompatibleDC(sdc)
    bmp = g.CreateCompatibleBitmap(sdc, w, ht)
    g.SelectObject(dc, bmp)
    u.PrintWindow(h, dc, 3)
    hdr = struct.pack('<IiiHHIIiiII', 40, w, -ht, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(w * ht * 4)
    g.GetDIBits(dc, bmp, 0, ht, buf, hdr, 0)
    g.DeleteObject(bmp)
    g.DeleteDC(dc)
    u.ReleaseDC(h, sdc)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    Image.frombuffer('RGBA', (w, ht), buf.raw, 'raw', 'BGRA', 0, 1).convert('RGB').save(path)
    return [w, ht]


def edit_open():
    return guarded(lambda p: p.r32(kbd.EDIT_OPEN))


def push(text):
    def f(p):
        kbd.push(p, text)
    guarded(f)


def chat(text):
    """Opens the chat box, types, sends. Returns how many tries the box took to open."""
    for tries in range(1, 6):
        key('T', 0.15)
        t = time.time()
        while time.time() - t < 1.5 and not edit_open():
            time.sleep(0.05)
        if edit_open():
            break
    else:
        raise RuntimeError('chat box did not open')
    time.sleep(0.15)
    push(text)
    time.sleep(0.25)
    key('Return', 0.15)
    t = time.time()
    while time.time() - t < 1.5 and edit_open():
        time.sleep(0.05)
    return tries


def scm(hexbytes):
    code = bytes.fromhex(hexbytes)

    def f(p):
        pad = code + bytes(-len(code) % 4)
        for i in range(0, len(pad), 4):
            p.w32(CMD + 4 + 16 + i, struct.unpack('<I', pad[i:i + 4])[0])
        p.w32(CMD, len(code))
        for _ in range(100):
            if p.r32(CMD) == 0:
                break
            time.sleep(0.02)
        else:
            return ['NOT EXECUTED', 0, 0]
        r2, r3 = struct.unpack('<ii', struct.pack('<II', p.r32(CMD + 4), p.r32(CMD + 8)))
        return ['ok', r2, r3]
    return guarded(f)


def status():
    def f(p):
        w = [p.r32(ST + i * 4) for i in range(16)]
        pos = struct.unpack('<fff', struct.pack('<III', *w[13:16]))
        return {'magic': w[0], 'frames': w[1], 'stage': w[2], 'err': w[3], 'v': w[4:], 'pos': pos}
    return guarded(f)


def log():
    def f(p):
        n = p.r32(ST + 64 + 4096)
        return p.read(ST + 64, min(n, 4096)).decode('latin1')
    return guarded(f)


def pad(values):
    """values: dict of pad field -> short, or {} to stop overriding. Written last-to-first."""
    def f(p):
        p.w32(DBG_PAD, 0)
        words = [0] * 20
        mask = 0
        for k, v in values.items():
            i = PAD_NAMES.index(k)
            words[i] = v & 0xFFFF
            mask |= 1 << i
        for i in range(0, 20, 2):
            p.w32(DBG_PAD + 4 + i * 2, words[i] | words[i + 1] << 16)
        p.w32(DBG_PAD, mask)
    guarded(f)


def do(c):
    k = c['c']
    if k == 'ping':
        return 'pong'
    if k == 'key':
        for n in c['k'].split(','):
            key(n, c.get('hold', 0.2))
            time.sleep(c.get('gap', 0.2))
        return 'ok'
    if k == 'down':
        key_msg(c['k'], True)
        return 'ok'
    if k == 'up':
        key_msg(c['k'], False)
        return 'ok'
    if k == 'chat':
        return chat(c['t'])
    if k == 'push':
        push(c['t'])
        return 'ok'
    if k == 'shot':
        return shot(c['p'])
    if k == 'scm':
        return scm(c['h'])
    if k == 'st':
        return status()
    if k == 'log':
        return log()
    if k == 'r32':
        return guarded(lambda p: p.r32(c['a']))
    if k == 'r8':
        return guarded(lambda p: p.r8(c['a']))
    if k == 'read':
        return guarded(lambda p: p.read(c['a'], c['n']).hex())
    if k == 'w32':
        return guarded(lambda p: p.w32(c['a'], c['v']))
    if k == 'w8':
        return guarded(lambda p: p.w8(c['a'], c['v']))
    if k == 'editopen':
        return edit_open()
    if k == 'pad':
        pad(c['v'])
        return 'ok'
    if k == 'quit':
        os._exit(0)
    if k == 'reset':
        with L:
            drop()
        return 'ok'
    raise ValueError(k)


class H(socketserver.StreamRequestHandler):
    def handle(self):
        for line in self.rfile:
            try:
                r = {'ok': do(json.loads(line))}
            except Exception as e:
                r = {'err': repr(e)}
            self.wfile.write((json.dumps(r) + '\n').encode())
            self.wfile.flush()


class S(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == '__main__':
    threading.Thread(target=alive, daemon=True).start()
    S(('127.0.0.1', PORT), H).serve_forever()
