#!/bin/bash
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
set -e
cd $PROJ
GW=$(ip route | awk '/default/{print $3; exit}')
gcc -O1 -g -Wall -Wno-unused -Wno-builtin-declaration-mismatch -Ipayload -o ~/samp-host host/hostmain.c payload/samp.c payload/rak.c payload/bitstream.c payload/authkey.c payload/samp_rpc.c
exec ~/samp-host $GW 7777 "${1:-PS2_Test}" "${2:-12}" ${3:+"$3"} ${4:+"$4"} ${5:+"$5"}
