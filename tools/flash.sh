#!/usr/bin/env bash
# Flash the combined image through the WCH factory ISP.
#
# The device must already be in factory ISP mode:
#   hold the BOOT button (it pulls USB D+ high through R10), then power-cycle
#   with the slide switch, then release BOOT.
#
# This is the RECOVERY and FACTORY-PROVISIONING path only. Normal uploads go
# over the CHGame bootloader's own CDC protocol and touch none of this.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WCHISP="${WCHISP:-/d/WCHISP/wchisp.exe}"
IMAGE="${1:-$ROOT/build/chgame_full.bin}"

[ -f "$IMAGE" ] || { echo "no image at $IMAGE — run tools/build_all.sh first" >&2; exit 1; }

echo "probing for a device in factory ISP mode..."
"$WCHISP" probe || {
  echo
  echo "No ISP device found. Hold BOOT, cycle the power switch, release BOOT, then retry." >&2
  exit 1
}
echo
"$WCHISP" flash "$IMAGE"
