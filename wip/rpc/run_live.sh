#!/bin/bash
# Builds the live client and joins a real server, one player per run:
#   tools/w wip/rpc/run_live.sh [port] [name] [seconds] [mode]
# The host is the WSL default gateway. Keeps runs under 30 seconds.
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
cd $PROJ
O=$HOME/samp-rpc-build; mkdir -p $O
GW=$(ip route | awk '/default/{print $3; exit}')
gcc -O1 -g -Wall -Wno-unused -Wno-builtin-declaration-mismatch -Ipayload -Iwip/rpc -o $O/rpc_live \
    wip/rpc/live_client.c wip/rpc/samp_rpc.c payload/rak.c payload/bitstream.c
exec $O/rpc_live $GW "${1:-7777}" "${2:-RPCTest}" "${3:-15}" "${4:-plain}"
