#!/bin/bash
# Breaks each adapter-specific rule on purpose and checks the rig notices. A mutant
# that survives means the suite is not really testing that rule.
# Needs a previous run_tests.sh for its objects. Works on a copy under $HOME.
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}
cd $PROJ/wip/browser
GW=$(ip route | awk '/default/{print $3; exit}')
DNS=$(awk '/^nameserver/{print $2; exit}' /etc/resolv.conf)
O=$HOME/samp-browser-host; M=$O/mut
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
HTTP_PORT=18080; UDP_BASE=21000; N_FAKE=300
BIG=$(python3 -c "
import random
b=random.Random(1234).randbytes(200000)
h=0xCBF29CE484222325
for x in b: h=((h^x)*0x100000001B3)&0xFFFFFFFFFFFFFFFF
print('%x'%h)")

mutant() { # name file sed-expression
    rm -rf $M; mkdir -p $M/test
    cp *.c *.h $M/; cp test/*.c test/*.h $M/test/
    sed -i "$3" $M/$2
    if cmp -s $2 $M/$2; then echo "NOT APPLIED: $1"; return; fi
    INC="-I$M -I$M/test -I../../payload"
    W="-w"
    gcc -g -O1 $W $SAN -DRIG $INC -c $M/test/test_net.c -o $M/test_net.o || { echo "BUILD FAILED: $1"; return; }
    OBJ=""
    for f in sampq dnsq httpget browser net; do
        gcc -g -O1 $W $SAN $INC -c $M/$f.c -o $M/$f.o; OBJ="$OBJ $M/$f.o"
    done
    PC=$(ls $O/rig/*.o | grep -v -e '/c_' -e 'test_net.o')
    g++ -g $SAN -o $M/test_rig $M/test_net.o $OBJ $PC -lpthread
    python3 test/fixture_servers.py $HTTP_PORT $UDP_BASE $N_FAKE > /dev/null &
    FIX=$!
    sleep 1
    timeout 170 $M/test_rig $GW 7777 $DNS $HTTP_PORT $UDP_BASE $N_FAKE $BIG > $M/out.txt 2>&1
    R=$?
    kill $FIX; wait $FIX 2>/dev/null
    if [ $R = 0 ]; then
        echo "SURVIVED: $1"
    else
        echo "caught ($R): $1 -- $(grep -m1 -E 'FAIL|ERROR' $M/out.txt | cut -c1-90)"
    fi
}

mutant "data sent with no bare handshake ACK" net.c 's/if (!t.hs_acked) {/if (0) {/'
mutant "ACKs allowed after our FIN" net.c 's/if (t.state != T_OPEN || t.fin_sent)/if (t.state != T_OPEN)/'
mutant "no window update when the reader drains" net.c 's/if (t.need_ack || (room > t.adv.*$/if (t.need_ack)/'
mutant "FIN skips the gap check" net.c 's/if ((n || (flags \& TCP_FIN)) \&\& behind < 0) {/if (n \&\& behind < 0) {/'
mutant "rxbuf back to 1536" net.c 's/rxbuf\[2048\]/rxbuf[1536]/'
mutant "no player limit on 'c'" browser.h 's/#define BROWSER_PLAYER_QUERY_LIMIT 64/#define BROWSER_PLAYER_QUERY_LIMIT 1000/'
mutant "bad TCP checksum" net.c 's/sum += 6 + tlen;/sum += 7 + tlen;/'
echo "mutation run finished"
