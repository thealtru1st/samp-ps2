#!/bin/bash
# Host tests for rak.c's receive side under loss. Run: tools/w wip/rak/run_tests.sh [old-rak.c]
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
cd $PROJ
O=$HOME/samp-rak-build; mkdir -p $O
SRC=${1:-payload/rak.c}
gcc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-builtin -Wall -Wno-unused \
    -Wno-builtin-declaration-mismatch -Ipayload -DRAK_C="\"$PWD/$SRC\"" -o $O/test_hold \
    wip/rak/test_hold.c payload/bitstream.c payload/authkey.c
$O/test_hold
