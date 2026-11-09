#!/bin/bash
ip route | head -3
GW=$(ip route | awk '/default/{print $3; exit}')
echo "gateway=$GW"
cat /etc/resolv.conf | grep nameserver
for H in 127.0.0.1 $GW; do
python3 - "$H" <<'PY'
import socket,struct,sys
h=sys.argv[1]
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.settimeout(1.5)
pkt=b'SAMP'+bytes([127,0,0,1])+struct.pack('<H',7777)+b'i'
try:
    s.sendto(pkt,(h,7777)); d,a=s.recvfrom(2048); print(h,'REACHABLE',len(d))
except Exception as e: print(h,'no',e)
PY
done
