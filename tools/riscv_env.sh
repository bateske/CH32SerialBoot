# Shared toolchain settings. Sourced by the firmware build scripts.
# The RISC-V toolchain ships with the CH32 Arduino core; override with CHGAME_TOOLCHAIN.
TC="${CHGAME_TOOLCHAIN:-/c/Users/kevin/AppData/Local/Arduino15/packages/CH32_Arduino/tools/riscv-none-embed-gcc/8.2.0/bin}"
CC="$TC/riscv-none-embed-gcc"
OBJCOPY="$TC/riscv-none-embed-objcopy"
SIZE="$TC/riscv-none-embed-size"

ARCH="-march=rv32imacxw -mabi=ilp32"
DEFS="-DCH32X035 -DSYSCLK_FREQ_48MHz_HSI=48000000 -DF_CPU=48000000"
DEFS="$DEFS -DCHGAME_DIAG=${CHGAME_DIAG:-0}"   # LED boot report; CHGAME_DIAG=1 to re-enable
WARN="-Wall -Wextra -Wundef -Werror=implicit-function-declaration"
OPT="-Os -flto -ffunction-sections -fdata-sections -fno-common -msmall-data-limit=8 -msave-restore"

check_toolchain() {
  [ -x "$CC" ] || [ -x "$CC.exe" ] || { echo "toolchain not found at $TC" >&2; exit 1; }
}
