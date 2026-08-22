#!/usr/bin/env bash
# Build the CHGame bootloader. Run from Git Bash:  ./bootloader/build.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/build"

# Toolchain ships with the CH32 Arduino core; override with CHGAME_TOOLCHAIN.
TC="${CHGAME_TOOLCHAIN:-/c/Users/kevin/AppData/Local/Arduino15/packages/CH32_Arduino/tools/riscv-none-embed-gcc/8.2.0/bin}"
CC="$TC/riscv-none-embed-gcc"
OBJCOPY="$TC/riscv-none-embed-objcopy"
SIZE="$TC/riscv-none-embed-size"

[ -x "$CC" ] || [ -x "$CC.exe" ] || { echo "toolchain not found at $TC" >&2; exit 1; }

SPL="$HERE/vendor/spl"
SRC="$HERE/src"
USB="$HERE/vendor/usbcdc"
SHARED="$HERE/../shared"

ARCH="-march=rv32imacxw -mabi=ilp32"
DEFS="-DCH32X035 -DSYSCLK_FREQ_48MHz_HSI=48000000 -DF_CPU=48000000 -DCHGAME_DIAG=${CHGAME_DIAG:-0} -DCHGAME_IMAGE_ID=1"
# $SHARED holds chgame_usb_identity.h, pulled in by the vendored config forwarder.
INC="-I$SHARED -I$SRC -I$USB -I$SPL -I$SPL/Core -I$SPL/Peripheral/inc"
WARN="-Wall -Wextra -Wundef -Werror=implicit-function-declaration"
OPT="-Os -flto -ffunction-sections -fdata-sections -fno-common -msmall-data-limit=8 -msave-restore"
CFLAGS="$ARCH $DEFS $INC $WARN $OPT -std=gnu11 -g"

CSRC=(
  "$SRC/main.c" "$SRC/crc32.c" "$SRC/bootreq.c" "$SRC/appmeta.c"
  "$SRC/led.c" "$SRC/sys.c" "$SRC/jump.c" "$SRC/fault.c" "$SRC/startup_glue.c"
  "$SRC/crc16.c" "$SRC/proto.c" "$SRC/flash.c"
  "$USB/wch_usbcdc_cdc.c" "$USB/wch_usbcdc_descr.c" "$USB/wch_usbcdc_handler.c"
  "$SPL/system_ch32x035.c"
  "$SPL/Core/core_riscv.c"
  "$SPL/Peripheral/src/ch32x035_rcc.c"
  "$SPL/Peripheral/src/ch32x035_gpio.c"
  "$SPL/Peripheral/src/ch32x035_flash.c"
  "$SPL/Peripheral/src/ch32x035_misc.c"
)
ASRC=( "$SRC/startup_chgame_boot.S" )

rm -rf "$OUT"; mkdir -p "$OUT/obj"
OBJS=()
for f in "${ASRC[@]}"; do
  o="$OUT/obj/$(basename "$f").o"; OBJS+=("$o")
  "$CC" $ARCH $DEFS -I"$SPL/Startup" -x assembler-with-cpp -c "$f" -o "$o"
done
for f in "${CSRC[@]}"; do
  o="$OUT/obj/$(basename "$f").o"; OBJS+=("$o")
  "$CC" $CFLAGS -c "$f" -o "$o"
done

"$CC" $ARCH $OPT -T "$HERE/ld/link_boot.ld" -nostartfiles -Xlinker --gc-sections \
      --specs=nano.specs --specs=nosys.specs \
      -Wl,-Map,"$OUT/bootloader.map" -o "$OUT/bootloader.elf" "${OBJS[@]}"

"$OBJCOPY" -O binary "$OUT/bootloader.elf" "$OUT/bootloader.bin"

echo
"$SIZE" -A "$OUT/bootloader.elf" | sed -n '1,12p'
BYTES=$(stat -c %s "$OUT/bootloader.bin")
# Reservation comes from chgame_map.h, so this can never disagree with the layout.
RESV=$(cd "$HERE/.." && python tools/chgame_map.py --boot-size)
echo
printf 'bootloader.bin = %d bytes of the %d-byte reservation (%d%% used, %d free)\n' \
       "$BYTES" "$RESV" $((BYTES * 100 / RESV)) $((RESV - BYTES))
