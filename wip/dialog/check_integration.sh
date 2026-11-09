#!/bin/bash
# Applies the Integration list to a private copy of payload/ and links it for the EE,
# with every other stream's current code. Touches nothing shipped.
#   tools/w wip/dialog/check_integration.sh
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
source $PROJ/tools/env.sh
O=$HOME/samp-dialog-build/integ
rm -rf $O; mkdir -p $O/src $O/obj
cp $PROJ/payload/*.c $PROJ/payload/*.h $PROJ/payload/*.S $PROJ/payload/link.ld $O/src/
python3 $PROJ/wip/dialog/integrate.py $PROJ/payload $O/src
cd $O/src
CC=mips64r5900el-ps2-elf-gcc
CFLAGS="-O2 -G0 -mno-abicalls -fno-pic -ffreestanding -fno-builtin -fno-stack-protector -fno-strict-aliasing -Wall -Wno-unused -I."
OBJS=""
for f in *.c *.S; do o=$O/obj/${f%.*}.o; $CC $CFLAGS -c "$f" -o "$o"; OBJS="$OBJS $o"; done
$CC -nostdlib -nostartfiles -T link.ld -Wl,--gc-sections -o $O/payload.elf $OBJS -lgcc
mips64r5900el-ps2-elf-size $O/payload.elf | tail -1
mips64r5900el-ps2-elf-nm $O/payload.elf | grep -E " (menu_text_open|game_remote_pos|game_remote_handle|feat_dialog_pad|feat_dialog_modal)$"
