#!/bin/bash
# Host tests for the combat stream, sanitizers on. Run: tools/w wip/combat/run_tests.sh
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
cd $PROJ
O=$HOME/samp-combat-build; rm -rf $O; mkdir -p $O
FLAGS="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-strict-aliasing -Wall -Wno-unused -Wno-builtin-declaration-mismatch -DCG_HOST"
gcc $FLAGS -Ipayload -Iwip/combat -o $O/plain wip/combat/test_combat.c wip/combat/fake.c wip/combat/glue.c \
    payload/feat_combat.c payload/feat_combat_game.c payload/samp_rpc.c payload/bitstream.c -lm
$O/plain

# The same tests with wip/combat/integration.patch applied, which turns the #ifdef'd half
# on and proves the patch still applies. Then the EE compiler checks the patched tree.
cp -r payload $O/tree
(cd $O/tree && patch -s -p2 --binary < $PROJ/wip/combat/integration.patch)
T=$O/tree
gcc $FLAGS -I$T -Iwip/combat -o $O/patched wip/combat/test_combat.c wip/combat/fake.c wip/combat/glue.c \
    $T/feat_combat.c $T/feat_combat_game.c $T/samp_rpc.c $T/bitstream.c -lm
$O/patched $O/scm.txt
python3 wip/combat/check_scm.py $O/scm.txt .
source tools/env.sh
CF="-O2 -G0 -mno-abicalls -fno-pic -ffreestanding -fno-builtin -fno-stack-protector -fno-strict-aliasing -Wall -Wno-unused -I$T"
OBJS=""
for f in $T/*.c $T/*.S; do o=$O/ee-$(basename ${f%.*}).o; mips64r5900el-ps2-elf-gcc $CF -c "$f" -o "$o"; OBJS="$OBJS $o"; done
mips64r5900el-ps2-elf-gcc -nostdlib -nostartfiles -T $T/link.ld -Wl,--gc-sections -o $O/payload.elf $OBJS -lgcc
mips64r5900el-ps2-elf-size $O/payload.elf | tail -1
