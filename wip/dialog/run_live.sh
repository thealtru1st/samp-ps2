#!/bin/bash
# Against the PRIVATE server only: start wip/dialog/server.ps1, then
#   tools/w wip/dialog/run_live.sh 7791
# Builds the real client with the samp.c integration applied to a temporary copy, so
# this also proves that change. Output: ~/samp-dialog-build/live.log
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
cd $PROJ
O=$HOME/samp-dialog-build; mkdir -p $O
GW=$(ip route | awk '/default/{print $3; exit}')
PORT=${1:?port of the private server}
[ "$PORT" != 7777 ] || { echo "needs its own server, not 7777"; exit 1; }
mkdir -p $O/live && python3 wip/dialog/integrate.py payload $O/live
gcc -O1 -g -std=gnu99 -Wall -Wno-unused -Wno-builtin-declaration-mismatch -fsanitize=address,undefined \
    -fno-strict-aliasing -Ipayload -Iwip/dialog -o $O/live_dialog \
    wip/dialog/live_dialog.c wip/dialog/fake_gfx.c $O/live/samp.c \
    payload/feat_dialog.c payload/feat_dialog_box.c payload/feat_dialog_td.c payload/feat_dialog_menu.c \
    payload/feat_dialog_hud.c payload/feat_dialog_text.c payload/samp_rpc.c payload/bitstream.c \
    payload/rak.c payload/authkey.c
$O/live_dialog $GW $PORT Peer_One peer > $O/peer.log 2>&1 &
PEER=$!
sleep 4
$O/live_dialog $GW $PORT Dialog_Tester > $O/live.log 2>&1 || true
kill $PEER 2>/dev/null || true
grep -E "FAIL|LIVE RESULT|closed:" $O/live.log
