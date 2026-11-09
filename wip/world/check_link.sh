#!/bin/bash
# feat.c keeps streams off until they are merged, and --gc-sections then drops them, so
# tools/build.sh alone says nothing about whether this stream links. This builds a private
# copy with FEAT_WORLD on, and every other stream as feat.c has it:
#   tools/w wip/world/check_link.sh
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
source $PROJ/tools/env.sh
T=$HOME/samp-world-link; rm -rf $T; mkdir -p $T/o
cp $PROJ/payload/* $T/ 2>/dev/null || true
sed -i 's/#define FEAT_WORLD   0/#define FEAT_WORLD   1/' $T/feat.c
cd $T
CC=mips64r5900el-ps2-elf-gcc
CFLAGS="-O2 -G0 -mno-abicalls -fno-pic -ffreestanding -fno-builtin -fno-stack-protector -fno-strict-aliasing -Wall -Wno-unused -I."
OBJS=""
for f in *.c *.S; do [ -e "$f" ] || continue; o=o/${f%.*}.o; $CC $CFLAGS -c "$f" -o "$o"; OBJS="$OBJS $o"; done
$CC -nostdlib -nostartfiles -T link.ld -Wl,--gc-sections -Wl,-Map=o/payload.map -o o/payload.elf $OBJS -lgcc
mips64r5900el-ps2-elf-size o/payload.elf | tail -1
mips64r5900el-ps2-elf-nm o/payload.elf | grep -E " [Tt] (feat_world_tick|wobj_tick|wg_kill_help|wlabel_draw|wactor_tick)$"
echo "weak and unresolved:"
mips64r5900el-ps2-elf-nm o/payload.elf | grep -E " [wU] " || true
