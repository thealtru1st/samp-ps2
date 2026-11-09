#!/bin/bash
# Two host clients against a PRIVATE server already running vehtest.pwn, never the shared one:
#   tools/w wip/vehicle/live_run.sh <port>
# Builds a copy of payload with integration.patch applied. Logs go to
# ~/samp-vehicle-test/driver.log and watcher.log.
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
cd $PROJ
O=$HOME/samp-vehicle-test; mkdir -p $O/src
GW=$(ip route | awk '/default/{print $3; exit}')
PORT=${1:?port of the private server}
[ "$PORT" != 7777 ] || { echo "vehtest needs its own server, not 7777"; exit 1; }
cp payload/*.c payload/*.h $O/src/
cp wip/vehicle/patched/samp.c wip/vehicle/patched/samp.h $O/src/
gcc -O1 -g -fsanitize=address,undefined -Wall -Wno-unused -Wno-builtin-declaration-mismatch -Wno-address \
    -I$O/src -Iwip/vehicle -o $O/veh_live wip/vehicle/live_client.c wip/vehicle/fake_game.c \
    $O/src/samp.c $O/src/rak.c $O/src/bitstream.c $O/src/authkey.c $O/src/samp_rpc.c -lm
$O/veh_live $GW $PORT watcher 28 > $O/watcher.log 2>&1 &
sleep 1
$O/veh_live $GW $PORT driver 26 > $O/driver.log 2>&1 || true
wait
echo done
