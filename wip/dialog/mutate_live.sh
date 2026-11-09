#!/bin/bash
# Proves the live test notices: each mutation must fail it. Private server only.
#   tools/w wip/dialog/mutate_live.sh <port>
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
cd $PROJ
O=$HOME/samp-dialog-build; mkdir -p $O/mut
GW=$(ip route | awk '/default/{print $3; exit}')
PORT=${1:?port}
[ "$PORT" != 7777 ] || exit 1
mkdir -p $O/live && python3 wip/dialog/integrate.py payload $O/live
FILES="feat_dialog.c feat_dialog_box.c feat_dialog_td.c feat_dialog_menu.c feat_dialog_hud.c feat_dialog_text.c"
run() { # name, file, sed expression
    cp $(for f in $FILES; do echo payload/$f; done) $O/mut/
    sed -i "$3" $O/mut/$2
    if cmp -s $O/mut/$2 payload/$2; then echo "$1: MUTATION DID NOT APPLY"; return; fi
    gcc -O1 -g -std=gnu99 -w -Ipayload -Iwip/dialog -o $O/live_mut \
        wip/dialog/live_dialog.c wip/dialog/fake_gfx.c $O/live/samp.c \
        $(for f in $FILES; do echo $O/mut/$f; done) payload/samp_rpc.c payload/bitstream.c payload/rak.c payload/authkey.c
    STOP_ON_FAIL=1 $O/live_mut $GW $PORT Mutant > $O/mut.log 2>&1
    echo "$1: $(grep -E 'FAIL' $O/mut.log | head -1 || true) / $(grep 'LIVE RESULT' $O/mut.log)"
}
run "no spawn retry"        feat_dialog.c     's/rak_rpc(SRPC_C2S_REQUEST_SPAWN, 0, 0, RAK_RELIABLE, 0);/;/'
run "input listitem 0"      feat_dialog_box.c 's/respond(1, -1, out);/respond(1, 0, out);/'
run "list item off by one"  feat_dialog_box.c 's/respond(1, d.sel, text);/respond(1, d.sel + 1, text);/'
run "click id wrong"        feat_dialog_td.c  's/send_click(hover);/send_click(hover + 1);/'
