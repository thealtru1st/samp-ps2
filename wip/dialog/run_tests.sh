#!/bin/bash
# Host tests for the feat_dialog modules: tools/w wip/dialog/run_tests.sh [fuzz rounds]
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
cd $PROJ
O=$HOME/samp-dialog-build; mkdir -p $O
gcc -O1 -g -std=gnu99 -Wall -Wno-unused -Wno-builtin-declaration-mismatch -fsanitize=address,undefined \
    -fno-sanitize-recover=undefined -fno-strict-aliasing -Ipayload -Iwip/dialog -o $O/test_dialog \
    wip/dialog/test_dialog.c wip/dialog/fake_gfx.c \
    payload/feat_dialog.c payload/feat_dialog_box.c payload/feat_dialog_td.c payload/feat_dialog_menu.c \
    payload/feat_dialog_hud.c payload/feat_dialog_text.c payload/samp_rpc.c payload/bitstream.c
gcc -O1 -g -std=gnu99 -Wall -fsanitize=address,undefined -Ipayload -o $O/test_proj \
    wip/dialog/test_proj.c payload/feat_dialog_proj.c -lm
gcc -O1 -g -std=gnu99 -Wall -Wno-unused -Wno-builtin-declaration-mismatch -fsanitize=address,undefined \
    -Ipayload -Iwip/dialog -o $O/test_nobox wip/dialog/test_nobox.c wip/dialog/fake_gfx.c \
    payload/feat_dialog.c payload/feat_dialog_box.c payload/feat_dialog_td.c payload/feat_dialog_menu.c \
    payload/feat_dialog_hud.c payload/feat_dialog_text.c payload/samp_rpc.c payload/bitstream.c
$O/test_proj
$O/test_nobox
$O/test_dialog "${1:-200000}"
