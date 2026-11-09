#!/usr/bin/env python3
"""Checks coverage.md against the code and the opcode database:

  check_coverage.py <repo root>

- every id in SRPC_TABLE and SRPC_SYNC_TABLE has a row, and every row has an id
- every encoder named exists in samp_rpc.h, and every encoder in samp_rpc.h is named
- every `XXXX NAME` is a real opcode in ref/scm/sa.json with exactly that name, and
  is not marked unsupported or nop. Bare `XXXX` mentions must be real opcodes too.
"""
import json, os, re, sys

root = sys.argv[1]
doc = open(os.path.join(root, 'wip/rpc/coverage.md'), encoding='utf-8').read()
hdr = open(os.path.join(root, 'wip/rpc/samp_rpc.h')).read()
db = json.load(open(os.path.join(root, 'ref/scm/sa.json'), encoding='utf-8'))
ops = {c['id'].upper(): c for c in db['extensions'][0]['commands']}
errors = []

# Slots in the tables.
slots = {n: int(i) for i, n in re.findall(r'X\((\d+),\s+(\w+),', hdr)}
rows = {}
for m in re.finditer(r'^\| (\d+) \| (\w+) \|', doc, re.M):
    rows[m.group(2)] = int(m.group(1))
for slot, i in slots.items():
    if rows.get(slot) != i:
        errors.append('no coverage row for %d %s' % (i, slot))
for slot, i in rows.items():
    if slot not in slots and not slot.startswith('srpc_enc_') and slot != 'none':
        errors.append('row for unknown slot %d %s' % (i, slot))

# Encoders.
encs = set(re.findall(r'\b(srpc_enc_\w+)\(', hdr))
named = set(re.findall(r'\b(srpc_enc_\w+)\b', doc))
for e in encs - named:
    errors.append('encoder not in coverage.md: ' + e)
for e in named - encs:
    errors.append('coverage.md names a missing encoder: ' + e)

# Opcodes.
pairs = re.findall(r'`([0-9A-F]{4}) ([A-Z0-9_]+)`', doc)
for op, name in pairs:
    c = ops.get(op)
    if not c:
        errors.append('no opcode ' + op)
    elif c['name'] != name:
        errors.append('%s is %s, not %s' % (op, c['name'], name))
    elif (c.get('attrs') or {}).get('is_unsupported') or (c.get('attrs') or {}).get('is_nop'):
        errors.append('%s %s is marked unsupported or nop' % (op, name))
bare = set(re.findall(r'`([0-9A-F]{4})[`-]', doc))
for op in bare:
    if op not in ops:
        errors.append('no opcode ' + op)

for e in errors:
    print('FAIL', e)
print('%d rows, %d slots, %d encoders, %d opcode references (%d distinct), %d errors' % (
    len(rows), len(slots), len(encs), len(pairs), len(set(pairs)), len(errors)))
sys.exit(bool(errors))
