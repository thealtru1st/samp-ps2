# Sourced by the build and test scripts (run inside WSL or Linux).
# PS2DEV: the ps2dev toolchain with mips64r5900el-ps2-elf-gcc (https://github.com/ps2dev/ps2dev).
export PS2DEV=${PS2DEV:-$HOME/ps2dev-sdk/ps2dev}
export PATH="$PS2DEV/ee/bin:/usr/bin:/bin"
export PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
