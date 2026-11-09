#!/usr/bin/env python3
"""Boots the current payload to the browser and saves it to slot 9 for tools/play.ps1.
Rerun after every payload build: a state holds the payload as it was when saved."""
import struct, subprocess, sys, time
sys.path.insert(0, 'tools')
from pine import Pine, ST

subprocess.run(['taskkill', '/IM', 'pcsx2-qt.exe', '/F'], capture_output=True)
subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tools/run_pcsx2.ps1'])
t = time.time()
while time.time() - t < 170:
    try:
        p = Pine(timeout=2)
        if p.r32(ST + 4) >= 200:
            break
        p.c.close()
    except Exception:
        pass
    time.sleep(1)
else:
    sys.exit('game did not reach the menu')
p._x(struct.pack('<BB', 9, 9))
time.sleep(4)
print('state saved to slot 9 after %.0fs' % (time.time() - t))
