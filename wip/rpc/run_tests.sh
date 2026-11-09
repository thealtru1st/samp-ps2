#!/bin/bash
# Host unit tests for samp_rpc.c: tools/w wip/rpc/run_tests.sh
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
cd $PROJ
O=$HOME/samp-rpc-build; mkdir -p $O
# The wire tests are generated from open.mp's own encoders, at the commit they were
# written against. OPENMP_SRC points at an existing checkout; otherwise one is fetched.
OMP=${OPENMP_SRC:-$PROJ/ref/openmp-src}
OMP_COMMIT=72bf89ce4c8164f7f7b8271ffc7323bc45ea005f
if [ ! -d "$OMP/Shared/NetCode" ]; then
    echo "fetching open.mp $OMP_COMMIT into $OMP for the wire tests"
    git clone -q --filter=blob:none --no-checkout https://github.com/openmultiplayer/open.mp "$OMP"
    git -C "$OMP" checkout -q $OMP_COMMIT
fi
python3 wip/rpc/gen_wire_test.py "$OMP/Shared/NetCode" wip/rpc/samp_rpc.h $O/wire_gen.inc
WRAP=""
for f in bs_r8 bs_r16 bs_r32 bs_rf bs_rbool bs_rcomp16 bs_rbytes bs_w8 bs_w16 bs_w32 bs_wf bs_wbool bs_wbytes; do
    WRAP="$WRAP -Wl,--wrap=$f"
done
gcc -O1 -g -std=gnu99 -Wall -Wno-unused -Wno-builtin-declaration-mismatch -fsanitize=address,undefined \
    -fno-sanitize-recover=undefined -Ipayload -Iwip/rpc -I$O \
    -o $O/test_rpc wip/rpc/test_rpc.c wip/rpc/samp_rpc.c payload/bitstream.c $WRAP -lm
$O/test_rpc
