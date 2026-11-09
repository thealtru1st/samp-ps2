#!/bin/bash
# Two players against a PRIVATE server running rpctest.pwn, never the shared one:
#   tools/w wip/rpc/run_script.sh <port>
# Output goes to ~/samp-rpc-build/script.log and peer.log.
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
cd $PROJ
O=$HOME/samp-rpc-build; mkdir -p $O
GW=$(ip route | awk '/default/{print $3; exit}')
PORT=${1:?port of the private server}
[ "$PORT" != 7777 ] || { echo "rpctest needs its own server, not 7777"; exit 1; }
gcc -O1 -g -Wall -Wno-unused -Wno-builtin-declaration-mismatch -Ipayload -Iwip/rpc -o $O/rpc_live \
    wip/rpc/live_client.c wip/rpc/samp_rpc.c payload/rak.c payload/bitstream.c
$O/rpc_live $GW $PORT RPCPeer 22 peer > $O/peer.log 2>&1 &
sleep 1
$O/rpc_live $GW $PORT RPCTest 26 script > $O/script.log 2>&1
wait
grep -h "BAD\|SUMMARY" $O/script.log $O/peer.log
