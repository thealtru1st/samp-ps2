#!/usr/bin/env python3
"""Minimal PINE client (TCP 127.0.0.1:slot). Used to read the payload status block."""
import socket, struct, sys, time
class Pine:
    def __init__(s,port=28011,timeout=3):
        s.c=socket.create_connection(('127.0.0.1',port),timeout=timeout)
    def _x(s,body):
        s.c.sendall(struct.pack('<I',len(body)+4)+body)
        h=b''
        while len(h)<4: h+=s.c.recv(4-len(h))
        n=struct.unpack('<I',h)[0]-4; r=b''
        while len(r)<n: r+=s.c.recv(n-len(r))
        if r[0]!=0: raise IOError('PINE fail')
        return r[1:]
    def r32(s,a): return struct.unpack('<I',s._x(struct.pack('<BI',2,a)))[0]
    def r8(s,a): return s._x(struct.pack('<BI',0,a))[0]
    def w32(s,a,v): s._x(struct.pack('<BII',6,a,v))
    def w8(s,a,v): s._x(struct.pack('<BIB',4,a,v))
    def read(s,a,n):
        out=bytearray()
        for i in range(0,n,4): out+=struct.pack('<I',s.r32(a+i))
        return bytes(out[:n])
    def status(s): return struct.unpack('<I',s._x(struct.pack('<B',0xF)))[0]
    def title(s):
        r=s._x(struct.pack('<B',0xB)); return r[4:].rstrip(b'\0').decode('latin1')
ST=0x83F00000
def show(p):
    w=[p.r32(ST+i*4) for i in range(16)]
    print('magic %08x frames %d stage %d err %08x'%tuple(w[:4]))
    print(' v:',' '.join('%08x'%x for x in w[4:]))
    pos=p.r32(ST+64+4096)
    if pos: print(p.read(ST+64,min(pos,4096)).decode('latin1'))
if __name__=='__main__':
    p=Pine(); print('status',p.status()); show(p)
