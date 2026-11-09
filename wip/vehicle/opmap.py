#!/usr/bin/env python3
"""Opcode -> interpreter case address for SLES-52927, by decoding the machine code of
the 27 group handlers listed at 0x5F9238 (the table ProcessOneCommand copies to
0x6FE0B0). Each group is either a chain of `li v0,N; beq a1,v0,case` or a jump table.
  opmap.py 0227 00A5 ...     print name, argument count, case address
  opmap.py all               print every opcode found
"""
import json, os, struct, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
D = open(os.path.join(ROOT, 'private', 'SLES_529.27'), 'rb').read()
def rd(a): return struct.unpack('<I', D[a-0x100000+0x180:a-0x100000+0x184])[0]
def sx(v): return v-0x10000 if v & 0x8000 else v
def group(entry, base):
    out = {}; end = entry+4*2400
    p = entry; last_li = {}; lui = {}; addr = {}; sub = 0; count = 100
    while p < end:
        w = rd(p); op = w >> 26; rs = (w>>21)&31; rt = (w>>16)&31; imm = w & 0xffff
        if op == 9 and rs == 0: last_li[rt] = sx(imm)          # li
        if op == 13 and rs == 0: last_li[rt] = imm
        if op == 15: lui[rt] = imm << 16
        if op == 9 and rs in lui: addr[rt] = (lui[rs]+sx(imm)) & 0xffffffff
        if op == 9 and rt == 2 and rs not in (0, 29, 2) and sx(imm) < 0: sub = -sx(imm)  # addiu v0,a1,-N
        if op == 11 and rs in (2, 5) and rt == 1: count = imm  # sltiu at,v0,N
        if op == 3: break                                      # first jal: chain is over
        if op == 4 and rs == 5 and rt in last_li:              # beq a1,rt,target
            t = p+4+sx(imm)*4
            v = last_li[rt]
            if v not in out: out[v] = t
        if w & 0xfc1fffff == 8 and rs == 2:                    # jr v0: jump table
            tbl = [a for a in addr.values() if 0x600000 < a < 0x700000]
            if tbl:
                for i in range(count):
                    out.setdefault(sub+i, rd(tbl[-1]+4*i))
            break
        p += 4
    return out
def build():
    m = {}
    for g in range(27):
        e = rd(0x5F9238+16*g+8)
        m.update({k: v for k, v in group(e, g*100).items() if g*100 <= k < g*100+100})
    return m
if __name__ == '__main__':
    m = build()
    j = json.load(open(os.path.join(ROOT, 'ref', 'scm', 'sa.json')))
    names = {int(c['id'], 16): c for c in j['extensions'][0]['commands']}
    a = sys.argv[1:]
    if a == ['all']: a = ['%04X' % k for k in sorted(m)]
    for s in a:
        k = int(s, 16); c = names.get(k, {})
        print('%04X %-44s n=%-2s %s' % (k, c.get('name', '?'), c.get('num_params', '?'),
              hex(m[k]) if k in m else 'NOT FOUND'))
