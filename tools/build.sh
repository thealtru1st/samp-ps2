#!/bin/bash
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
set -e
source $PROJ/tools/env.sh
cd $PROJ/payload
O=$HOME/samp-build${1:+-$1}; mkdir -p $O   # a name argument makes a private check build
CC=mips64r5900el-ps2-elf-gcc
CFLAGS="-O2 -G0 -mno-abicalls -fno-pic -ffreestanding -fno-builtin -fno-stack-protector -fno-strict-aliasing -Wall -Wno-unused -I."
OBJS=""
for f in *.c *.S; do [ -e "$f" ] || continue; o=$O/${f%.*}.o; $CC $CFLAGS -c "$f" -o "$o"; OBJS="$OBJS $o"; done
$CC -nostdlib -nostartfiles -T link.ld -Wl,--gc-sections -Wl,-Map=$O/payload.map -o $O/payload.elf $OBJS -lgcc
if [ -z "$1" ]; then
  mkdir -p $PROJ/private
  mips64r5900el-ps2-elf-objcopy -O binary $O/payload.elf $PROJ/private/payload.bin
  ls -la $PROJ/private/payload.bin
fi
mips64r5900el-ps2-elf-size $O/payload.elf | tail -1
