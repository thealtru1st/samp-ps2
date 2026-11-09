#!/bin/bash
# Compile-check samp_rpc.c with host gcc and the EE toolchain.
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
source $PROJ/tools/env.sh
cd $PROJ
O=$HOME/samp-rpc-build; mkdir -p $O
/usr/bin/gcc -O2 -Wall -Wextra -Wno-unused-parameter -std=gnu99 -Ipayload -c wip/rpc/samp_rpc.c -o $O/host.o
echo "host gcc ok"
mips64r5900el-ps2-elf-gcc -O2 -G0 -mno-abicalls -fno-pic -ffreestanding -fno-builtin -fno-stack-protector -fno-strict-aliasing -Wall -Ipayload -c wip/rpc/samp_rpc.c -o $O/ee.o
echo "EE gcc ok"
mips64r5900el-ps2-elf-size $O/ee.o
echo "undefined symbols:"
mips64r5900el-ps2-elf-nm -u $O/ee.o
