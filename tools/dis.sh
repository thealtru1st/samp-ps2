#!/bin/bash
# dis.sh ADDR_HEX COUNT
n=${2:-60}
grep -n -m1 -E "^\s+$1:" ~/elf.dis | cut -d: -f1 | { read l; sed -n "${l},$((l+n-1))p" ~/elf.dis | sed -E 's/^\s+//' | cut -c1-70; }
