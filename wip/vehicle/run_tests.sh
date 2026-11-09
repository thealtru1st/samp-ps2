#!/bin/bash
# Unit tests for feat_vehicle.c on the host, under ASan and UBSan:
#   tools/w wip/vehicle/run_tests.sh
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
cd $PROJ
O=$HOME/samp-vehicle-test; mkdir -p $O
gcc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wno-unused \
    -Wno-builtin-declaration-mismatch -Wno-address -Ipayload -Iwip/vehicle -o $O/test_vehicle \
    wip/vehicle/test_vehicle.c wip/vehicle/fake_game.c payload/samp_rpc.c payload/bitstream.c -lm
exec $O/test_vehicle
