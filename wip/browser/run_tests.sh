#!/bin/bash
# Host tests. Run with: tools/w wip/browser/run_tests.sh [real-list]
#   real-list  also does one real DNS + HTTP fetch of the masterlist through the
#              rig. Nothing fetched is ever queried.
# Everything that talks to the network stays on loopback, apart from the local
# server at the default gateway.
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
cd $PROJ/wip/browser
GW=$(ip route | awk '/default/{print $3; exit}')
DNS=$(awk '/^nameserver/{print $2; exit}' /etc/resolv.conf)
O=$HOME/samp-browser-host; mkdir -p $O
SRC="sampq.c dnsq.c httpget.c browser.c"
WARN="-Wall -Wextra -Wno-unused-parameter -Wno-builtin-declaration-mismatch"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
INC="-I. -Itest -I../../payload"
REF=../../ref/pcsx2-src/pcsx2
HTTP_PORT=18080; UDP_BASE=21000; N_FAKE=300

echo "--- capture real replies from $GW:7777"
python3 test/capture.py $GW 7777 fixtures

echo "--- unit tests (ASan + UBSan)"
gcc -g -O1 $WARN $SAN $INC -o $O/test_unit test/test_unit.c $SRC
$O/test_unit fixtures

echo "--- build rig (real PCSX2 session code)"
SHIM="-Itest/pcsx2shim -I$REF -D__POSIX__ -D_GNU_SOURCE"
D=$REF/DEV9
PC="$D/Sessions/BaseSession.cpp $D/Sessions/TCP_Session/TCP_Session.cpp $D/Sessions/TCP_Session/TCP_Session_In.cpp
    $D/Sessions/TCP_Session/TCP_Session_Out.cpp $D/Sessions/UDP_Session/UDP_Session.cpp
    $D/Sessions/UDP_Session/UDP_FixedPort.cpp $D/Sessions/UDP_Session/UDP_Common.cpp
    $D/PacketReader/IP/IP_Packet.cpp $D/PacketReader/IP/IP_Options.cpp
    $D/PacketReader/IP/TCP/TCP_Packet.cpp $D/PacketReader/IP/TCP/TCP_Options.cpp
    $D/PacketReader/IP/UDP/UDP_Packet.cpp $D/PacketReader/IP/UDP/DNS/DNS_Packet.cpp
    $D/PacketReader/IP/UDP/DNS/DNS_Classes.cpp $D/InternalServers/DNS_Server.cpp"
mkdir -p $O/rig
OBJS=""
for f in $PC test/rig_smap.cpp; do
    o=$O/rig/$(basename ${f%.cpp}).o
    if [ ! -e $o ] || [ $f -nt $o ]; then
        g++ -std=c++20 -g -O1 -w $SHIM -Itest -c $f -o $o &
    fi
    OBJS="$OBJS $o"
done
wait
gcc -g -O1 $WARN $SAN -DRIG $INC -c test/test_net.c -o $O/rig/test_net.o
for f in $SRC net.c; do gcc -g -O1 $WARN $SAN $INC -c $f -o $O/rig/c_${f%.c}.o; done
g++ -g $SAN -o $O/test_rig $O/rig/test_net.o $O/rig/c_*.o $OBJS -lpthread

echo "--- build host transport test"
gcc -g -O1 $WARN $SAN $INC -o $O/test_host test/test_net.c test/host_xport.c $SRC

start_fixtures() {
    if [ -n "$FIX" ]; then kill $FIX; wait $FIX 2>/dev/null || true; fi
    python3 test/fixture_servers.py $HTTP_PORT $UDP_BASE $N_FAKE &
    FIX=$!
    sleep 1
}
trap '[ -n "$FIX" ] && kill $FIX 2>/dev/null' EXIT
start_fixtures
BIG=$(python3 -c "
import random
b=random.Random(1234).randbytes(200000)
h=0xCBF29CE484222325
for x in b: h=((h^x)*0x100000001B3)&0xFFFFFFFFFFFFFFFF
print('%x'%h)")

echo "--- rig"
$O/test_rig $GW 7777 $DNS $HTTP_PORT $UDP_BASE $N_FAKE $BIG $1
echo "--- rig, browser ticked before net_poll as tools/patch_ui2.py does"
start_fixtures
RIG_TICK_FIRST=1 $O/test_rig $GW 7777 $DNS $HTTP_PORT $UDP_BASE $N_FAKE $BIG
echo "--- host transport"
start_fixtures # the lossy fixtures count what they dropped, so they need a fresh start
$O/test_host $GW 7777 $DNS $HTTP_PORT $UDP_BASE $N_FAKE $BIG
echo "ALL PASSED"
