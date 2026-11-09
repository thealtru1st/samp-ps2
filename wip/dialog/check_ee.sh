#!/bin/bash
# What the EE build of the feat_dialog modules pulls in: tools/w wip/dialog/check_ee.sh
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
source $PROJ/tools/env.sh
O=$HOME/samp-build-dialog
for f in feat_dialog feat_dialog_box feat_dialog_td feat_dialog_menu feat_dialog_hud feat_dialog_text feat_dialog_gfx feat_dialog_proj; do
    echo "$f $(mips64r5900el-ps2-elf-size $O/$f.o | tail -1 | awk '{print "text",$1,"data",$2,"bss",$3}')"
    mips64r5900el-ps2-elf-nm -u $O/$f.o | grep -E " __|df[0-9]" | tr -s ' ' | tr '\n' ' '
    echo
done
echo weak: $(mips64r5900el-ps2-elf-nm $O/payload.elf | grep -E "menu_text_open|game_remote_pos|game_remote_handle")
echo "--- weak call site: the null test must survive"
mips64r5900el-ps2-elf-objdump -dr $O/feat_dialog_box.o | grep -n -A3 -B1 "menu_text_open" | cut -c1-70 | head -24
