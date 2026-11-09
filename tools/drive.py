#!/usr/bin/env python3
"""Test driver over PINE: status, log tail, and typing into an open text box.
  python tools/drive.py status        frames, stage, memory, where the frame is
  python tools/drive.py log [N]       last N chars of the payload log
  python tools/drive.py type TEXT     type into the open text box (like kbd.py)
Keys themselves go through tools/win.ps1 -keys."""
import struct, sys, time
sys.path.insert(0, 'tools')
from pine import Pine, ST

BASE = ST + 64 + 4096
KB_ALIVE, KB_SEQ, EDIT_OPEN, KB_TEXT, EDIT_TEXT = BASE + 280, BASE + 284, BASE + 288, BASE + 292, BASE + 452
GP = 0x66B170
WHERE, WHERE_OP = BASE + 744, BASE + 748


def log(p, n):
    lp = p.r32(ST + 64 + 4096)
    raw = p.read(ST + 64, 4096)[:lp]
    return raw.decode('latin1')[-n:]


def status(p):
    a = [p.r32(ST + i * 4) for i in range(16)]
    time.sleep(1)
    b = [p.r32(ST + i * 4) for i in range(16)]
    used, avail, queued = p.r32(GP + 0xD1C) // 1024, p.r32(GP - 0x42E8) // 1024, p.r32(GP + 0xD18)
    print('frames/s %d stage %d rpcs %d rx %d tx %d | stream %d/%d KB queued %d | edit open %d | where %d op %04X'
          % (b[1] - a[1], b[2], b[4], b[9], b[10], used, avail, queued, p.r32(EDIT_OPEN),
             p.r32(WHERE), p.r32(WHERE_OP)))


def type_text(p, text):
    # The menu only takes text while it believes a keyboard helper is alive.
    for _ in range(10):
        p.w32(KB_ALIVE, p.r32(ST + 4))
        time.sleep(0.05)
    if not p.r32(EDIT_OPEN):
        print('no text box open')
        return
    data = text.encode('ascii') + bytes(4 - len(text) % 4)
    for i in range(0, len(data), 4):
        p.w32(KB_TEXT + i, struct.unpack('<I', data[i:i + 4])[0])
    p.w32(KB_SEQ, (p.r32(KB_SEQ) + 1) & 0xFFFFFFFF)
    for _ in range(10):
        p.w32(KB_ALIVE, p.r32(ST + 4))
        time.sleep(0.05)
    print('box now holds %d chars' % len(p.read(EDIT_TEXT, 160).split(bytes(1))[0]))


if __name__ == '__main__':
    p = Pine(timeout=10)
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'status'
    if cmd == 'status':
        status(p)
    elif cmd == 'log':
        print(log(p, int(sys.argv[2]) if len(sys.argv) > 2 else 2500))
    elif cmd == 'type':
        type_text(p, sys.argv[2])
    p.c.close()
