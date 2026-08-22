#!/usr/bin/env bash
# Refresh the platform's bundled artefacts from the current build, then install
# it into Arduino15 for testing. Run after any bootloader or uploader change.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PLAT="$ROOT/arduino/CHGame"
mkdir -p "$PLAT/bootloaders/CHGame" "$PLAT/tools"
cp bootloader/build/bootloader.bin "$PLAT/bootloaders/CHGame/chgame_bootloader.bin"
# The uploader is no longer bundled inside the platform: it is a tool
# dependency, so Boards Manager installs it separately. For the development
# install we drop the freshly built Windows binary where the tool would live.
rm -rf "$PLAT/tools"

ARDUINO15="${CHGAME_ARDUINO15:-/c/Users/kevin/AppData/Local/Arduino15}"
DST="$ARDUINO15/packages/CHGame/hardware/ch32v/0.1.0"
TOOLDST="$ARDUINO15/packages/CHGame/tools/chgame-upload/0.1.0"
rm -rf "$DST"; mkdir -p "$(dirname "$DST")"
cp -r "$PLAT" "$DST"

# Development install only: reuse the toolchain already present from the
# CH32_Arduino package. The Board Manager package declares a real toolDependency
# instead, so this file must NOT be part of the released archive.
cat > "$DST/platform.local.txt" <<'LOCAL'
compiler.path=C:/Users/kevin/AppData/Local/Arduino15/packages/CH32_Arduino/tools/riscv-none-embed-gcc/8.2.0/bin/
LOCAL

if [ -f "$ROOT/dist/tools/x86_64-mingw32/chgame-upload.exe" ]; then
  mkdir -p "$TOOLDST"
  cp "$ROOT/dist/tools/x86_64-mingw32/chgame-upload.exe" "$TOOLDST/"
  echo "uploader synced to $TOOLDST"
fi

echo "platform synced to $DST"
echo "  bootloader: $(stat -c %s "$PLAT/bootloaders/CHGame/chgame_bootloader.bin") bytes"
