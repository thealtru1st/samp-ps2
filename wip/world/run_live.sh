#!/bin/bash
# Joins the private server running worldtest.pwn: tools/w wip/world/run_live.sh <port>
# The host is the WSL default gateway. Never the shared server.
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
cd $PROJ
O=$HOME/samp-world-build; mkdir -p $O
PORT=${1:?port of the private server}
[ "$PORT" != 7777 ] || { echo "worldtest needs its own server, not 7777"; exit 1; }
GW=$(ip route | awk '/default/{print $3; exit}')
gcc -O1 -g -Wall -Wno-unused -Wno-builtin-declaration-mismatch -Ipayload -Iwip/world -o $O/live_world \
    wip/world/live_world.c wip/world/fake_game.c \
    payload/feat_world.c payload/feat_world_obj.c payload/feat_world_pick.c \
    payload/feat_world_radar.c payload/feat_world_label.c payload/feat_world_actor.c \
    payload/samp.c payload/rak.c payload/bitstream.c payload/authkey.c payload/samp_rpc.c -lm
exec $O/live_world $GW "$PORT" WorldTest
