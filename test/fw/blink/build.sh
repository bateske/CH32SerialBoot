#!/usr/bin/env bash
# Build the M1 relocated blink application (links at CHGAME_APP_START).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
source "$ROOT/tools/riscv_env.sh"
check_toolchain

SPL="$ROOT/bootloader/vendor/spl"
BSRC="$ROOT/bootloader/src"          # for chgame_map.h and the it.h shadow
LD="$ROOT/arduino/CHGame/system/CH32X035/SRC/Ld/link_chgame_app.ld"
# BLINK_PULSES lets the test suite build a visibly different image.
PULSES="${BLINK_PULSES:-3}"
OUT="$HERE/build/p$PULSES"

INC="-I$HERE -I$BSRC -I$SPL -I$SPL/Core -I$SPL/Peripheral/inc"
CFLAGS="$ARCH $DEFS -DCHGAME_IMAGE_ID=2 -DBLINK_PULSES=$PULSES $INC $WARN $OPT -std=gnu11 -g"

rm -rf "$OUT"; mkdir -p "$OUT/obj"
OBJS=()
o="$OUT/obj/startup.o"; OBJS+=("$o")
"$CC" $ARCH $DEFS -I"$SPL/Startup" -x assembler-with-cpp -c "$SPL/Startup/startup_ch32x035.S" -o "$o"
for f in "$HERE/blink.c" "$BSRC/fault.c" "$BSRC/startup_glue.c" "$BSRC/bootreq.c" \
         "$SPL/system_ch32x035.c" "$SPL/Core/core_riscv.c" \
         "$SPL/Peripheral/src/ch32x035_rcc.c" "$SPL/Peripheral/src/ch32x035_gpio.c" \
         "$SPL/Peripheral/src/ch32x035_misc.c"; do
  o="$OUT/obj/$(basename "$f").o"; OBJS+=("$o")
  "$CC" $CFLAGS -c "$f" -o "$o"
done

"$CC" $ARCH $OPT -T "$LD" -nostartfiles -Xlinker --gc-sections \
      --specs=nano.specs --specs=nosys.specs \
      -Wl,-Map,"$OUT/blink.map" -o "$OUT/blink.elf" "${OBJS[@]}"
"$OBJCOPY" -O binary "$OUT/blink.elf" "$OUT/blink.bin"
echo "blink.bin ($PULSES pulses) = $(stat -c %s "$OUT/blink.bin") bytes, links at $(
  "$TC/riscv-none-embed-objdump" -h "$OUT/blink.elf" | awk '/\.init/{print $4; exit}')"
