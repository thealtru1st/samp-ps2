#!/usr/bin/env python3
"""Real keyboard typing for the in-game text boxes.

The PS2 has no idea a keyboard exists, so this runs beside PCSX2. It reads keystrokes
only while the game says a text box is open AND PCSX2 has focus, and writes the text
into the box over PINE. Nothing is stored or logged, and it exits when PCSX2 closes.
Enter and cancel still reach the game as normal pad input."""
import ctypes
import ctypes.wintypes as wt
import struct
import sys
import threading
import time

sys.path.insert(0, __file__.rsplit('\\', 1)[0] if '\\' in __file__ else 'tools')
from pine import Pine, ST

BASE = ST + 64 + 4096
KB_ALIVE, KB_SEQ, EDIT_OPEN = BASE + 280, BASE + 284, BASE + 288
KB_TEXT, EDIT_TEXT = BASE + 292, BASE + 452
TEXT_MAX = 150
ENTRY_GUARD_S = 0.15   # the key that opened the box must not type into it

user32 = ctypes.WinDLL('user32', use_last_error=True)
WH_KEYBOARD_LL, WM_KEYDOWN, WM_SYSKEYDOWN, HC_ACTION = 13, 0x100, 0x104, 0
VK_BACK, VK_RETURN = 0x08, 0x0D
LRESULT = ctypes.c_ssize_t


class KBD(ctypes.Structure):
    _fields_ = [('vk', wt.DWORD), ('scan', wt.DWORD), ('flags', wt.DWORD),
                ('time', wt.DWORD), ('extra', ctypes.c_size_t)]


HOOK = ctypes.WINFUNCTYPE(LRESULT, ctypes.c_int, wt.WPARAM, wt.LPARAM)
user32.SetWindowsHookExW.restype = wt.HHOOK
user32.SetWindowsHookExW.argtypes = [ctypes.c_int, HOOK, wt.HINSTANCE, wt.DWORD]
user32.CallNextHookEx.restype = LRESULT
user32.CallNextHookEx.argtypes = [wt.HHOOK, ctypes.c_int, wt.WPARAM, wt.LPARAM]
user32.GetForegroundWindow.restype = wt.HWND
user32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
user32.GetKeyboardLayout.restype = ctypes.c_void_p
user32.GetKeyboardLayout.argtypes = [wt.DWORD]
user32.ToUnicodeEx.argtypes = [wt.UINT, wt.UINT, ctypes.c_char_p, wt.LPWSTR, ctypes.c_int,
                               wt.UINT, ctypes.c_void_p]

state = {'active': False, 'since': 0.0, 'text': '', 'dirty': False, 'pcsx2_pid': 0}
lock = threading.Lock()


def pcsx2_pid():
    import subprocess
    # No window: under pythonw each run would otherwise flash a console.
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq pcsx2-qt.exe', '/FO', 'CSV', '/NH'],
                         capture_output=True, text=True,
                         creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)).stdout
    parts = out.strip().split('","')
    return int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else 0


def pcsx2_focused():
    pid = wt.DWORD()
    tid = user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(pid))
    return pid.value == state['pcsx2_pid'], tid


def to_char(k, tid):
    keys = (ctypes.c_ubyte * 256)()
    for vk in (0x10, 0x11, 0x12, 0x14):  # shift, ctrl, alt, caps lock
        keys[vk] = user32.GetKeyState(vk) & 0xFF
    buf = ctypes.create_unicode_buffer(8)
    n = user32.ToUnicodeEx(k.vk, k.scan, bytes(keys), buf, 8, 4, user32.GetKeyboardLayout(tid))
    return buf.value[:n] if n > 0 else ''


def on_key(code, wparam, lparam):
    if code == HC_ACTION and wparam in (WM_KEYDOWN, WM_SYSKEYDOWN) and state['active']:
        focused, tid = pcsx2_focused()
        if focused and time.time() - state['since'] > ENTRY_GUARD_S:
            k = ctypes.cast(lparam, ctypes.POINTER(KBD)).contents
            with lock:
                if k.vk == VK_BACK:
                    state['text'] = state['text'][:-1]
                    state['dirty'] = True
                elif k.vk != VK_RETURN:
                    c = to_char(k, tid)
                    if c and all(32 <= ord(x) < 127 for x in c) and len(state['text']) < TEXT_MAX:
                        state['text'] += c
                        state['dirty'] = True
    return user32.CallNextHookEx(None, code, wparam, lparam)


def pump():
    cb = HOOK(on_key)
    hook = user32.SetWindowsHookExW(WH_KEYBOARD_LL, cb, None, 0)
    if not hook:
        print('could not install keyboard hook', ctypes.get_last_error())
        return
    msg = wt.MSG()
    while user32.GetMessageW(ctypes.byref(msg), None, 0, 0) > 0:
        pass


def push(p, text, seq=None):
    # The game only takes text when KB_SEQ changes, so always step from what is there.
    # A fixed or restarted counter can land on the same value and the text is lost.
    data = text.encode('ascii', 'replace') + bytes(4 - len(text) % 4)
    for i in range(0, len(data), 4):
        p.w32(KB_TEXT + i, struct.unpack('<I', data[i:i + 4])[0])
    p.w32(KB_SEQ, (p.r32(KB_SEQ) + 1) & 0xFFFFFFFF)


def read_text(p):
    raw = p.read(EDIT_TEXT, 160)
    return raw.split(bytes(1))[0].decode('ascii', 'replace')


def main():
    threading.Thread(target=pump, daemon=True).start()
    p, seq, misses = None, 1, 0
    while True:
        try:
            if p is None:
                if not state['pcsx2_pid']:
                    state['pcsx2_pid'] = pcsx2_pid()
                p = Pine(timeout=2)
            frames = p.r32(ST + 4)
            p.w32(KB_ALIVE, frames)
            is_open = p.r32(EDIT_OPEN) != 0
            if is_open and not state['active']:
                with lock:
                    state['text'] = read_text(p)
                    state['dirty'] = False
                state['since'] = time.time()
            state['active'] = is_open
            with lock:
                dirty, text = state['dirty'], state['text']
                state['dirty'] = False
            if dirty:
                seq += 1
                push(p, text, seq)
            misses = 0
            if not is_open:
                # PINE takes one client at a time, so don't sit on it while idle.
                p.c.close()
                p = None
                time.sleep(0.2)
                continue
        except (OSError, ConnectionError):
            state['active'] = False
            state['pcsx2_pid'] = 0
            p = None
            misses += 1
            if misses > 90:  # PCSX2 has been gone for a while: nothing left to do
                return
            time.sleep(1)
            continue
        time.sleep(0.03)


if __name__ == '__main__':
    main()
