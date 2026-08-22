#!/usr/bin/env bash
# Build and publish a CHGame board package release.
#
#   ./tools/release.sh 0.2.0 OWNER/REPO          # build and publish with gh
#   ./tools/release.sh 0.2.0 OWNER/REPO --dry-run # build only, publish nothing
#
# Produces the platform archive, the per-host uploader archives and the Boards
# Manager index, then attaches all of them to a GitHub release tagged v<version>.
#
# The index pins a SHA-256 for every archive and references them by absolute URL,
# so the release assets must end up at exactly the URLs baked in here. That is
# why the tag and the base URL are derived from the same version argument rather
# than passed separately - getting them out of step produces an index that
# installs nothing, with a checksum error as the only clue.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VERSION="${1:-}"
REPO="${2:-}"
DRY_RUN=""
[ "${3:-}" = "--dry-run" ] && DRY_RUN=1

if [ -z "$VERSION" ] || [ -z "$REPO" ]; then
  echo "usage: $0 <version> <owner/repo> [--dry-run]" >&2
  echo "   eg: $0 0.2.0 bateske/CHGame" >&2
  exit 1
fi

TAG="v$VERSION"
BASE_URL="https://github.com/$REPO/releases/download/$TAG"

echo "=== building bootloader ==="
./tools/build_all.sh >/dev/null
BOOT_SIZE=$(stat -c %s bootloader/build/bootloader.bin)
RESV=$(python tools/chgame_map.py --boot-size)
echo "bootloader: $BOOT_SIZE / $RESV bytes"

echo
echo "=== refreshing the platform's bundled bootloader ==="
mkdir -p arduino/CHGame/bootloaders/CHGame
cp bootloader/build/bootloader.bin arduino/CHGame/bootloaders/CHGame/chgame_bootloader.bin

echo
echo "=== building the uploader for every host ==="
./host/go/build.sh

echo
echo "=== packaging ==="
python tools/make_tool_archives.py --base-url "$BASE_URL"
python tools/make_package.py --version "$VERSION" --base-url "$BASE_URL"

echo
echo "=== release assets ==="
ASSETS=(dist/package_chgame_index.json "dist/CHGame-ch32v-$VERSION.tar.bz2")
while IFS= read -r f; do ASSETS+=("$f"); done < <(find dist/tool-archives -name '*.tar.bz2')
for a in "${ASSETS[@]}"; do printf '  %-60s %9s bytes\n' "$a" "$(stat -c %s "$a")"; done

if [ -n "$DRY_RUN" ]; then
  echo
  echo "dry run - nothing published."
  echo "Boards Manager URL once released:"
  echo "  $BASE_URL/package_chgame_index.json"
  exit 0
fi

command -v gh >/dev/null || { echo "gh CLI not found; install it or upload the assets by hand" >&2; exit 1; }

echo
echo "=== publishing $TAG to $REPO ==="
if gh release view "$TAG" --repo "$REPO" >/dev/null 2>&1; then
  gh release upload "$TAG" "${ASSETS[@]}" --repo "$REPO" --clobber
else
  gh release create "$TAG" "${ASSETS[@]}" --repo "$REPO" \
     --title "CHGame $VERSION" \
     --notes "Board package $VERSION.

Add this to Arduino IDE, Preferences -> Additional Boards Manager URLs:

    $BASE_URL/package_chgame_index.json"
fi

echo
echo "done. Boards Manager URL:"
echo "  $BASE_URL/package_chgame_index.json"
echo
echo "Verify from a clean state before announcing it:"
echo "  arduino-cli core uninstall CHGame:ch32v"
echo "  rm -rf \"\$ARDUINO15/packages/CHGame\""
echo "  arduino-cli core update-index --additional-urls $BASE_URL/package_chgame_index.json"
echo "  arduino-cli core install CHGame:ch32v --additional-urls $BASE_URL/package_chgame_index.json"
