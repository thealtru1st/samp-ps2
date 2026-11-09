import struct,re,sys
d=open(sys.argv[1],'rb').read()
print('magic',d[:4],'class',d[4],'data',d[5])
(e_type,e_mach,e_ver,e_entry,e_phoff,e_shoff,e_flags,e_ehsize,e_phentsize,e_phnum,e_shentsize,e_shnum,e_shstrndx)=struct.unpack_from('<HHIIIIIHHHHHH',d,16)
print('entry',hex(e_entry),'ph',e_phnum,'sh',e_shnum,'flags',hex(e_flags))
for i in range(e_phnum):
    t,off,va,pa,fs,ms,fl,al=struct.unpack_from('<8I',d,e_phoff+i*e_phentsize)
    print('PH',t,hex(off),hex(va),hex(fs),hex(ms),fl)
if e_shnum:
    sh=[struct.unpack_from('<10I',d,e_shoff+i*e_shentsize) for i in range(e_shnum)]
    st=sh[e_shstrndx]
    for s in sh:
        n=d[st[4]+s[0]:].split(b'\0')[0].decode()
        print('SH',repr(n),s[1],hex(s[3]),hex(s[4]),hex(s[5]))
for pat in [b'CPed',b'CVehicle',b'CWorld',b'CTheScripts',b'.cpp',b'SN Systems',b'GCC',b'ProDG',b'cdrom0:',b'host0:']:
    print(pat,len(re.findall(re.escape(pat),d)))
ss=set(re.findall(rb'[ -~]{8,}',d))
for s in sorted(x for x in ss if b'.cpp' in x or b'.irx' in x.lower() or b'.IRX' in x)[:50]: print(s[:100])
