#!/usr/bin/env python3
"""Reads feat_world_game.c and checks what it can against the ELF itself: every gravity
site really holds 0.008, the help setters are what they should be, and the opcodes it uses
have the argument counts the code passes."""
import json, os, re, struct, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
ELF = os.environ.get('GTASA_ELF') or os.path.join(ROOT, 'private', 'SLES_529.27')
if not os.path.exists(ELF):
    # The game executable is not in the repository: copy SLES_529.27 off your disc
    # (tools/iso_extract.py) to private/ or point GTASA_ELF at it.
    print('address checks skipped: no game ELF at', ELF)
    sys.exit(0)
d = open(ELF, 'rb').read()
src = open(os.path.join(ROOT, 'payload', 'feat_world_game.c')).read()
BASE, OFF = 0x100000, 0x180
bad = 0


def word(a):
    return struct.unpack_from('<I', d, a - BASE + OFF)[0]


def check(ok, what):
    global bad
    if not ok:
        print('BAD', what)
        bad += 1


pairs = re.findall(r'\{0x([0-9A-F]+), 0x([0-9A-F]+)\}', src)
check(len(pairs) == 24, 'gravity code sites %d' % len(pairs))
for lui, ori in pairs:
    lui, ori = int(lui, 16), int(ori, 16)
    w, o = word(lui), word(ori)
    check(w >> 26 == 0x0F and w & 0xFFFF == 0x3C03, 'lui at %x' % lui)
    check(o >> 26 == 0x0D and o & 0xFFFF == 0x126F, 'ori at %x' % ori)
    check(((w >> 16) & 31) == ((o >> 21) & 31), 'lui and ori registers at %x' % lui)
data = re.search(r'gravity_data\[\] = \{(.*?)\}', src, re.S).group(1)
data = [int(x, 16) for x in re.findall(r'0x([0-9A-F]+)', data)]
check(len(data) == 9, 'gravity literals')
for a in data:
    check(word(a) == 0x3C03126F, 'literal at %x' % a)

# Whole-binary search: no site left out.
n = 0
for a in range(BASE, BASE + 0x500000, 4):
    w = word(a)
    if w >> 26 == 0x0F and w & 0xFFFF == 0x3C03:
        n += 1
check(n == 24, 'lui 0x3C03 in the text: %d' % n)

for a in (0x2A9200, 0x2A93C0, 0x2A9550):
    check(word(a) >> 16 == 0x27BD, 'prologue at %x' % a)

# jal 0x2A9200 from PRINT_HELP
check(word(0x315664) == 0x0C000000 | (0x2A9200 >> 2), 'PRINT_HELP calls the setter')
# the two function pointers
check(word(0x11D12C) == 0x0C000000 | (0x110310 >> 2), 'CalcScreenCoors transforms a point')
check(word(0x26EA68) == 0x0C000000 | (0x26E5A0 >> 2), 'gang overlay calls DrawAreaOnRadar')
fmt = d[0x5FE9E0 - BASE + OFF:0x5FE9E0 - BASE + OFF + 10]
check(fmt == b'%02d:%02d\0', 'clock format %r' % fmt)

# Opcode argument counts: count scm_* calls after each scm_op in the file.
SA_JSON = os.path.join(ROOT, 'ref', 'scm', 'sa.json')  # Sanny Builder's opcode list, not included
if not os.path.exists(SA_JSON):
    print('opcode checks skipped: no', SA_JSON)
    sys.exit(0)
sa = json.load(open(SA_JSON, encoding='utf8'))
ops = {int(c['id'], 16): c for c in sa['extensions'][0]['commands']}
names = dict((m[0], int(m[1], 16)) for m in re.findall(r'#define (OP_\w+)\s+0x([0-9A-Fa-f]+)', src))
for name, op in names.items():
    if name == 'OP_JR_RA':
        continue
    check(op in ops, 'opcode %s %x' % (name, op))
    c = ops.get(op, {})
    if c.get('attrs', {}).get('is_unsupported') or c.get('attrs', {}).get('is_nop'):
        check(False, 'opcode %s %s is marked dead' % (name, c.get('name')))
    for m in re.finditer(r'scm_op\(&s, %s\);(.*?)(?=scm_op|scm_run)' % name, src, re.S):
        args = len(re.findall(r'scm_(?:i8|i32|f|str|local)\(', m.group(1)))
        check(args == c.get('num_params'), '%s passes %d of %s' % (c.get('name'), args, c.get('num_params')))
print('address checks done,', bad, 'bad')
sys.exit(bad != 0)
