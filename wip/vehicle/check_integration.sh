#!/bin/bash
# Builds the whole payload for the EE and the host client with integration.patch applied,
# in a private directory. Shows the patch compiles and that the weak game.c hooks resolve.
#   tools/w wip/vehicle/check_integration.sh
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
source $PROJ/tools/env.sh
cd $PROJ
O=$HOME/samp-vehicle-int; rm -rf $O; mkdir -p $O/src $O/obj
cp payload/* $O/src/
cp wip/vehicle/patched/samp.c wip/vehicle/patched/samp.h wip/vehicle/patched/main.c wip/vehicle/patched/game.c wip/vehicle/patched/feat.c $O/src/
cd $O/src
CC=mips64r5900el-ps2-elf-gcc
CFLAGS="-O2 -G0 -mno-abicalls -fno-pic -ffreestanding -fno-builtin -fno-stack-protector -fno-strict-aliasing -Wall -Wno-unused -I."
OBJS=""
for f in *.c *.S; do o=$O/obj/${f%.*}.o; $CC $CFLAGS -c "$f" -o "$o"; OBJS="$OBJS $o"; done
$CC -nostdlib -nostartfiles -T link.ld -Wl,--gc-sections -Wl,-Map=$O/payload.map -o $O/payload.elf $OBJS -lgcc
mips64r5900el-ps2-elf-size $O/payload.elf | tail -1
mips64r5900el-ps2-elf-nm $O/payload.elf | grep -E ' (game_remote_handle|game_remote_hold|feat_vehicle_in_car|feat_vehicle_foot_sync|feat_vehicle_eject|feat_vehicle_reset)$'
cd $PROJ
/usr/bin/gcc -O1 -Wall -Wno-unused -Wno-builtin-declaration-mismatch -I$O/src -o $O/host wip/vehicle/patched/hostmain.c $O/src/samp.c $O/src/rak.c $O/src/bitstream.c $O/src/authkey.c $O/src/samp_rpc.c
echo host client builds
