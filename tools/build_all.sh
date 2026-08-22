#!/usr/bin/env bash
# Build everything and produce the flashable combined image.
#   ./tools/build_all.sh            -> bootloader + blink app + metadata
#   ./tools/build_all.sh --no-app   -> bootloader only (device stays in bootloader)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Keep the linker scripts in step with chgame_map.h before anything compiles.
python tools/gen_ld.py

# The Arduino core carries its own copy of the flash map, because the header
# lives outside the platform tree. Refresh it here so the two cannot drift:
# a stale copy would put the application-side boot magic at a different
# address from the one the bootloader reads, and uploads would simply stop
# working with no obvious cause.
cp bootloader/src/chgame_map.h arduino/CHGame/cores/arduino/ch32/chgame_map.h
echo

"$ROOT/bootloader/build.sh"
echo

if [ "${1:-}" = "--no-app" ]; then
  mkdir -p build
  python tools/mkimage.py --boot bootloader/build/bootloader.bin -o build/chgame_full.bin
else
  "$ROOT/test/fw/blink/build.sh"
  echo
  mkdir -p build
  python tools/mkimage.py --boot bootloader/build/bootloader.bin \
                          --app test/fw/blink/build/p3/blink.bin \
                          -o build/chgame_full.bin
fi
