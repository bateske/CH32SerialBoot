#!/usr/bin/env bash
# Build everything and produce the flashable combined image.
#   ./tools/build_all.sh            -> bootloader + blink app + metadata
#   ./tools/build_all.sh --no-app   -> bootloader only (device stays in bootloader)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Keep the linker scripts in step with chgame_map.h before anything compiles.
python tools/gen_ld.py
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
