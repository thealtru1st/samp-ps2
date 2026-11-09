#!/bin/bash
# EE build check. Nothing here touches payload/ or private/: all output goes to
# $HOME/samp-browser-ee.
#  1. compiles the module with the exact flags from the brief
#  2. does the integration on a scratch copy of the tree and links a whole payload
#     with link.ld, so those steps are proven rather than just listed
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
source $PROJ/tools/env.sh
cd $PROJ/wip/browser
O=$HOME/samp-browser-ee; rm -rf $O; mkdir -p $O
CC=mips64r5900el-ps2-elf-gcc
FLAGS="-O2 -G0 -mno-abicalls -fno-pic -ffreestanding -fno-builtin -fno-stack-protector -fno-strict-aliasing -Wall"
MOD="sampq dnsq httpget browser browser_ee net"
for f in $MOD; do
    $CC $FLAGS -Wextra -Wno-unused-parameter -I. -I../../payload -c $f.c -o $O/$f.o
done
mips64r5900el-ps2-elf-size $(for f in $MOD; do echo $O/$f.o; done)

# The integration, on a copy.
T=$O/tree; mkdir -p $T/tools
cp -r ../../payload $T/
cp ../../tools/mkpnach.py ../../tools/patch_ui2.py $T/tools/
cp sampq.[ch] dnsq.[ch] httpget.[ch] browser.[ch] browser_ee.[ch] net.[ch] $T/payload/
if grep -q browser_ee_init $T/payload/main.c; then
    echo "payload/main.c is already wired up"
else
    echo "applying tools/patch_ui2.py to the copy"
    (cd $T && python3 -I tools/patch_ui2.py)
fi

cd $T/payload
OBJS=""
for f in *.c *.S; do
    o=$O/p_${f%.*}.o
    $CC $FLAGS -Wno-unused -I. -c $f -o $o
    OBJS="$OBJS $o"
done
$CC -nostdlib -nostartfiles -T link.ld -Wl,--gc-sections -Wl,-Map=$O/payload.map -o $O/payload.elf $OBJS -lgcc
echo "whole payload with the browser linked:"
mips64r5900el-ps2-elf-size $O/payload.elf | tail -1
mips64r5900el-ps2-elf-objcopy -O binary $O/payload.elf $O/payload.bin
ls -la $O/payload.bin | awk '{print "payload.bin", $5, "bytes"}'
for s in browser_tick browser_on_udp browser_ee_init net_tcp_open menu_tick; do
    mips64r5900el-ps2-elf-nm $O/payload.elf | grep -q " $s\$" && echo "kept: $s" || echo "MISSING: $s"
done
echo "browser_tick call sites:"
grep -n "browser_tick" *.c | grep -v "^browser" || true
