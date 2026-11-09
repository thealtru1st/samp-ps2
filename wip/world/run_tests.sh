#!/bin/bash
# Host unit tests for the world stream, under ASan and UBSan, plus an address check of the
# game layer against the ELF:  tools/w wip/world/run_tests.sh
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
cd $PROJ
O=$HOME/samp-world-build; mkdir -p $O
gcc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wno-unused \
    -Wno-builtin-declaration-mismatch -Ipayload -Iwip/world \
    -o $O/test_world wip/world/test_world.c wip/world/fake_game.c \
    payload/feat_world.c payload/feat_world_obj.c payload/feat_world_pick.c \
    payload/feat_world_radar.c payload/feat_world_label.c payload/feat_world_actor.c \
    payload/bitstream.c payload/samp_rpc.c -lm
$O/test_world
python3 wip/world/check_addrs.py
