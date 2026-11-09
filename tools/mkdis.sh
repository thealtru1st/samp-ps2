#!/bin/bash
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
source $PROJ/tools/env.sh
cd $PROJ/private
mips64r5900el-ps2-elf-gcc --version | head -1
mips64r5900el-ps2-elf-objdump -D SLES_529.27 2>/dev/null | grep -E '^\s+[0-9a-f]+:' > ~/elf.dis
wc -l ~/elf.dis
sed -n 1,50p ~/elf.dis | cut -c1-80
