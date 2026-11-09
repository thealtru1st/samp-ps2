#!/usr/bin/env python3
"""Every SCM command the tests saw must have the argument count sa.json gives it,
counting outputs: the interpreter has no way to notice a wrong count, it just walks off
into the next command. Also lists the opcodes in the source that no test reached.
  check_scm.py <dump> <repo root>"""
import json
import re
import sys

dump, root = sys.argv[1], sys.argv[2]
commands = json.load(open(root + '/ref/scm/sa.json'))['extensions'][0]['commands']
by_id = {int(c['id'], 16): c for c in commands}

seen = {}
for line in open(dump):
    op, n = line.split()
    seen.setdefault(int(op, 16), set()).add(int(n))

bad = 0
for op, counts in sorted(seen.items()):
    c = by_id.get(op)
    if not c:
        print('UNKNOWN %04X' % op)
        bad += 1
        continue
    want = len(c.get('input', [])) + len(c.get('output', []))
    attrs = c.get('attrs') or {}
    if attrs.get('is_unsupported') or attrs.get('is_nop'):
        print('UNSUPPORTED %04X %s' % (op, c['name']))
        bad += 1
    if counts != {want}:
        print('ARGS %04X %s: sa.json %d, used %s' % (op, c['name'], want, sorted(counts)))
        bad += 1

src = open(root + '/payload/feat_combat_game.c').read()
defined = {int(m, 16) for m in re.findall(r'#define OP_\w+\s+0x([0-9A-Fa-f]{4})', src)}
for op in sorted(defined - set(seen)):
    print('not reached by a test: %04X %s' % (op, by_id.get(op, {}).get('name')))
print('%d opcodes checked, %d bad' % (len(seen), bad))
sys.exit(1 if bad else 0)
