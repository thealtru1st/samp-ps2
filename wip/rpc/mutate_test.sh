#!/bin/bash
# Proves run_tests.sh can fail: each mutation below is a realistic transcription
# slip in samp_rpc.c, and the test must catch every one.
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
cd $PROJ
O=$HOME/samp-rpc-build/mut; mkdir -p $O
WRAP=""
for f in bs_r8 bs_r16 bs_r32 bs_rf bs_rbool bs_rcomp16 bs_rbytes bs_w8 bs_w16 bs_w32 bs_wf bs_wbool bs_wbytes; do
    WRAP="$WRAP -Wl,--wrap=$f"
done
try() {
    sed "$1" wip/rpc/samp_rpc.c > $O/samp_rpc.c
    if cmp -s $O/samp_rpc.c wip/rpc/samp_rpc.c; then echo "NOT APPLIED: $1"; return; fi
    gcc -O1 -std=gnu99 -w -Ipayload -Iwip/rpc -I$HOME/samp-rpc-build -o $O/t wip/rpc/test_rpc.c $O/samp_rpc.c payload/bitstream.c $WRAP -lm
    if timeout 120 $O/t > $O/out 2>&1; then echo "SURVIVED: $1"; else echo "caught ($(grep -c FAIL $O/out) fails): $1"; fi
}
try 's/u.icon.style = bs_r8(b);/u.icon.style = bs_r16(b);/'
try 's/u.td_select.enable = bs_rbool(b);/u.td_select.enable = bs_r8(b);/'
try '/u.scm.event = bs_r32(b);/d'
try 's/u.camera.cut = bs_r8(b);/u.camera.cut = bs_r32(b);/'
try 's/huff_child\[n - 256\]\[0\] = lo;/huff_child[n - 256][0] = hi;/'
try 's/while (cur >= 0 \&\& huff_weight\[cur\] < huff_weight\[node\])/while (cur >= 0 \&\& huff_weight[cur] <= huff_weight[node])/'
try 's/t->align = (flags >> 1) \& 7;/t->align = flags \& 7;/'
try 's/\*health = h == 15 ? 100 : h \* 7;/*health = h * 7;/'
try 's/bs_w16(b, s->anim_flags);/bs_w8(b, s->anim_flags);/'
try 's/bs_w8(b, passenger != 0);/bs_w16(b, passenger != 0);/'
