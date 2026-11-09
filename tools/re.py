#!/usr/bin/env python3
"""Static analysis helpers for the symbol-less PS2 ELF.
  re.py callers ADDR      callers of function ADDR
  re.py callees ADDR      jal targets inside function starting at ADDR
  re.py str TEXT          strings containing TEXT + code xrefs
  re.py ref ADDR          code that materialises constant ADDR
  re.py func ADDR         containing function range for ADDR
  re.py dis ADDR [N]      raw disassembly (needs ~/elf.dis via tools/w), see dis.sh
"""
import struct, sys, re, pickle, os, bisect
BASE=0x100000; OFF=0x180; SIZE=0x56b200
P=os.path.join(os.path.dirname(__file__),'..','private')
d=open(os.path.join(P,'SLES_529.27'),'rb').read()[OFF:OFF+SIZE]
N=len(d)//4
W=struct.unpack('<%dI'%N,d[:N*4])
def build():
    calls={}   # target -> [site]
    refs={}    # const -> [site]
    lui=[None]*32; age=[0]*32
    for i,w in enumerate(W):
        a=BASE+i*4; op=w>>26
        if op==3:
            t=((a+4)&0xf0000000)|((w&0x3ffffff)<<2)
            calls.setdefault(t,[]).append(a)
        rs=(w>>21)&31; rt=(w>>16)&31; imm=w&0xffff; s=imm-0x10000 if imm&0x8000 else imm
        if op==0xf:
            lui[rt]=imm<<16; age[rt]=i
        elif lui[rs] is not None and i-age[rs]<24:
            v=None
            if op==9: v=(lui[rs]+s)&0xffffffff            # addiu
            elif op==0xd: v=lui[rs]|imm                    # ori
            elif op in (0x20,0x21,0x23,0x24,0x25,0x27,0x28,0x29,0x2b,0x31,0x39,0x37,0x3f,0x1e,0x1f): v=(lui[rs]+s)&0xffffffff
            if v is not None: refs.setdefault(v,[]).append(a)
            if op in (9,0xd) and rt==rs: pass
    # function starts: jal targets + prologue addiu sp,sp,-x after jr ra
    starts=set(calls)
    for i,w in enumerate(W):
        if (w&0xffff8000)==0x27bd8000:
            starts.add(BASE+i*4)
    return calls,refs,sorted(starts)
C=os.path.join(P,'re.cache')
if os.path.exists(C):
    calls,refs,starts=pickle.load(open(C,'rb'))
else:
    calls,refs,starts=build(); pickle.dump((calls,refs,starts),open(C,'wb'))
def func(a):
    i=bisect.bisect_right(starts,a)-1
    return starts[i],(starts[i+1] if i+1<len(starts) else BASE+SIZE)
def x(s): return int(s,16)
def main():
    c=sys.argv[1]
    if c=='callers':
        for s in calls.get(x(sys.argv[2]),[]): print(hex(s),'in',hex(func(s)[0]))
    elif c=='callees':
        a=x(sys.argv[2]); e=func(a)[1] if len(sys.argv)<4 else x(sys.argv[3])
        for p in range(a,e,4):
            w=W[(p-BASE)//4]
            if w>>26==3:
                t=((p+4)&0xf0000000)|((w&0x3ffffff)<<2)
                print(hex(p),'->',hex(t),len(calls.get(t,[])))
    elif c=='str':
        t=sys.argv[2].encode()
        for m in re.finditer(re.escape(t),d):
            s=m.start()
            while s>0 and 32<=d[s-1]<127: s-=1
            e=d.index(b'\0',m.start()); va=BASE+s
            print(hex(va),d[s:e][:80],[hex(r) for r in refs.get(va,[])][:8])
    elif c=='ref':
        for s in refs.get(x(sys.argv[2]),[]): print(hex(s),'in',hex(func(s)[0]))
    elif c=='func':
        print([hex(v) for v in func(x(sys.argv[2]))])
main()
