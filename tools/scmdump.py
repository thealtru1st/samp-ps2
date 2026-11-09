#!/usr/bin/env python3
"""Linear SCM disassembler using the Sanny Builder opcode database for names and arg counts."""
import json,struct,sys
d=open('private/MAIN.SCM','rb').read()
j=json.load(open('ref/scm/sa.json',encoding='utf-8'))
C={int(c['id'],16):c for c in j['extensions'][0]['commands']}
def param(o):
    t=d[o]
    if t==0: return o+1,'END'
    if t==1: return o+5,'%d'%struct.unpack_from('<i',d,o+1)
    if t==2: return o+3,'$%d'%struct.unpack_from('<H',d,o+1)
    if t==3: return o+3,'%d@'%struct.unpack_from('<H',d,o+1)
    if t==4: return o+2,'%d'%struct.unpack_from('<b',d,o+1)
    if t==5: return o+3,'%d'%struct.unpack_from('<h',d,o+1)
    if t==6: return o+5,'%.3f'%struct.unpack_from('<f',d,o+1)
    if t in(7,8): return o+7,'arr'
    if t==9: return o+9,repr(d[o+1:o+9].split(b'\0')[0].decode('latin1'))
    if t in(0xA,0xB,0xC,0xD): return o+3,'s$'
    if t==0xE:
        n=d[o+1]; return o+2+n,repr(d[o+2:o+2+n].decode('latin1'))
    raise ValueError('bad type %d at %x'%(t,o))
o=int(sys.argv[1],16); n=int(sys.argv[2]) if len(sys.argv)>2 else 80
for i in range(n):
    op=struct.unpack_from('<H',d,o)[0]; c=op&0x7fff; s=o; o+=2
    e=C.get(c)
    if not e: print('%06x %04X ???'%(s,c),d[o:o+12].hex()); break
    args=[]
    variadic=any(a.get('type')=='arguments' for a in e.get('input',[])) or c in(0x4F,0x4C,0x913,0xD4)
    k=e.get('num_params',0)
    try:
        if variadic:
            for _ in range(max(k-1,0)):
                o,v=param(o); args.append(v)
            while True:
                o,v=param(o)
                if v=='END': break
                args.append(v)
        else:
            for _ in range(k):
                o,v=param(o); args.append(v)
    except ValueError as x:
        print('%06x %04X %s %s  <%s>'%(s,c,e['name'],args,x)); break
    print('%06x %04X %-34s %s'%(s,c,e['name'],' '.join(args)))
