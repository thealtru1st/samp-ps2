#!/usr/bin/env python3
"""PC-style keyboard and mouse layout for PCSX2 pad 1. Run with PCSX2 closed.
The PS2 buttons map to these GTA SA on-foot actions: Cross sprint, Square jump,
Circle fire, Triangle enter/exit, R1 aim, L1 action, L2/R2 weapon, L3 crouch."""
import os
import re
import shutil

INI = os.environ.get('PCSX2_INI') or os.path.join(os.path.expanduser('~'), 'Documents', 'PCSX2', 'inis', 'PCSX2.ini')
BACKUP = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'private', 'PCSX2.ini.before-controls')

PAD1 = {
    'LUp': 'Keyboard/W', 'LDown': 'Keyboard/S', 'LLeft': 'Keyboard/A', 'LRight': 'Keyboard/D',
    'RUp': 'Pointer-0/Y+', 'RDown': 'Pointer-0/Y-', 'RLeft': 'Pointer-0/X-', 'RRight': 'Pointer-0/X+',
    'Circle': 'Pointer-0/LeftButton', 'R1': 'Pointer-0/RightButton',
    'Cross': 'Keyboard/Space', 'Square': 'Keyboard/Shift', 'Triangle': 'Keyboard/F',
    'L1': 'Keyboard/Tab', 'L2': 'Keyboard/Q', 'R2': 'Keyboard/E',
    'L3': 'Keyboard/C', 'R3': 'Keyboard/V',
    'Start': 'Keyboard/Return', 'Select': 'Keyboard/T',
    'Up': 'Keyboard/Up', 'Down': 'Keyboard/Down', 'Left': 'Keyboard/Left', 'Right': 'Keyboard/Right',
}
PAD = {'PointerXSpeed': '60', 'PointerYSpeed': '60', 'PointerXDeadZone': '5',
       'PointerYDeadZone': '5', 'PointerInertia': '5'}
# These hotkeys sit on keys the game now needs.
HOTKEYS = {'TogglePause': 'Keyboard/Pause', 'ToggleTurbo': 'Keyboard/F11'}
HOTKEYS_REMOVE = ['ToggleSlowMotion', 'InputRecToggleMode']
CORE = {'EnableMouseLock': 'true'}


def section(s, name):
    m = re.search(r'(?ms)^\[%s\]\n(.*?)(?=^\[|\Z)' % re.escape(name), s)
    assert m, name
    return m


def setk(s, sec, key, val):
    m = section(s, sec)
    body = m.group(1)
    if re.search(r'(?m)^%s\s*=' % key, body):
        body = re.sub(r'(?m)^%s\s*=.*$' % key, '%s = %s' % (key, val), body)
    else:
        body = '%s = %s\n' % (key, val) + body
    return s[:m.start(1)] + body + s[m.end(1):]


def delk(s, sec, key):
    m = section(s, sec)
    body = re.sub(r'(?m)^%s\s*=.*\n' % key, '', m.group(1))
    return s[:m.start(1)] + body + s[m.end(1):]


os.makedirs(os.path.dirname(BACKUP), exist_ok=True)
if not os.path.exists(BACKUP):
    shutil.copyfile(INI, BACKUP)
s = open(INI, encoding='utf-8', newline='').read().replace('\r\n', '\n')
for k, v in PAD1.items():
    s = setk(s, 'Pad1', k, v)
for k, v in PAD.items():
    s = setk(s, 'Pad', k, v)
for k, v in HOTKEYS.items():
    s = setk(s, 'Hotkeys', k, v)
for k in HOTKEYS_REMOVE:
    s = delk(s, 'Hotkeys', k)
for k, v in CORE.items():
    s = setk(s, 'EmuCore', k, v)
open(INI, 'w', encoding='utf-8', newline='\n').write(s)
print('controls written, backup at', BACKUP)
