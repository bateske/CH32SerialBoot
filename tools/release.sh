#!/usr/bin/env bash
# Build and publish a CHGame board package release.
#
#   ./tools/release.sh 0.2.1 OWNER/REPO          # build and publish with gh
#   ./tools/release.sh 0.2.1 OWNER/REPO --dry-run # build only, publish nothing
#
# Produces the platform archive, the per-host uploader archives and the Boards
# Manager index, then attaches all of them to a GitHub release tagged v<version>
# whose notes are the matching section of CHANGELOG.md.
#
# The version must be the one in arduino/CHGame/platform.txt. That is the number
# Boards Manager compares with what a user has installed to decide whether to
# offer an update, so a release without a bump there prompts nobody.
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
  echo "   eg: $0 0.2.1 bateske/CH32SerialBoot" >&2
  exit 1
fi

TAG="v$VERSION"
BASE_URL="https://github.com/$REPO/releases/download/$TAG"
# What the README tells users to add. /latest/ follows every normal release, so
# an installed package is offered the update as soon as this one is published.
INDEX_URL="https://github.com/$REPO/releases/latest/download/package_chgame_index.json"

PLATFORM_VERSION="$(sed -n 's/^version=//p' arduino/CHGame/platform.txt | tr -d '[:space:]')"
if [ "$VERSION" != "$PLATFORM_VERSION" ]; then
  echo "arduino/CHGame/platform.txt says version=$PLATFORM_VERSION, not $VERSION." >&2
  echo "Bump it first: Boards Manager compares that number to decide whether to offer an update." >&2
  exit 1
fi

# The release notes are this version's section of CHANGELOG.md, so a change is
# recorded once, in the file, and the release page follows.
NOTES="$(awk -v v="$VERSION" '$0 ~ "^## " v "( |$)" {p=1; next} /^## / {p=0} p' CHANGELOG.md)"
if [ -z "$(printf '%s' "$NOTES" | tr -d '[:space:]')" ]; then
  echo "CHANGELOG.md has no '## $VERSION' section; write the release notes there first." >&2
  exit 1
fi

if [ -z "$DRY_RUN" ]; then
  command -v gh >/dev/null || { echo "gh CLI not found; install it or upload the assets by hand" >&2; exit 1; }
  # gh creates the tag on GitHub at the remote's default branch, so what gets
  # published must already be pushed.
  if [ -n "$(git status --porcelain)" ]; then
    echo "working tree is not clean; commit before releasing" >&2
    exit 1
  fi
  git fetch -q origin
  if [ "$(git rev-parse HEAD)" != "$(git rev-parse origin/main)" ]; then
    echo "HEAD is not origin/main; push first, the release tag is created on the remote" >&2
    exit 1
  fi
fi

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

echo
echo "=== release notes ($TAG, from CHANGELOG.md) ==="
printf '%s\n' "$NOTES"

if [ -n "$DRY_RUN" ]; then
  echo
  echo "dry run - nothing published."
  echo "Boards Manager URL once released:"
  echo "  $INDEX_URL"
  exit 0
fi

NOTES_FILE="$(mktemp)"
trap 'rm -f "$NOTES_FILE"' EXIT
{
  printf '%s\n\n' "$NOTES"
  printf 'Add this to Arduino IDE, Preferences -> Additional Boards Manager URLs:\n\n'
  printf '    %s\n\n' "$INDEX_URL"
  printf 'Already installed? Boards Manager offers this version as an update.\n'
} > "$NOTES_FILE"

echo
echo "=== publishing $TAG to $REPO ==="
if gh release view "$TAG" --repo "$REPO" >/dev/null 2>&1; then
  gh release upload "$TAG" "${ASSETS[@]}" --repo "$REPO" --clobber
  gh release edit "$TAG" --repo "$REPO" --notes-file "$NOTES_FILE"
else
  # A normal release, never --prerelease: /releases/latest/ skips pre-releases,
  # and that alias is the URL users have.
  gh release create "$TAG" "${ASSETS[@]}" --repo "$REPO" \
     --title "CHGame $VERSION" --notes-file "$NOTES_FILE"
fi

echo
echo "=== refreshing the index on earlier releases ==="
# Someone who added an explicit version URL instead of /releases/latest/ is only
# offered this release if the index at that URL lists it.
OLD_TAGS="$(gh release list --repo "$REPO" --json tagName --jq '.[].tagName' | grep '^v' | grep -vx "$TAG" || true)"
for old in $OLD_TAGS; do
  gh release upload "$old" dist/package_chgame_index.json --repo "$REPO" --clobber && echo "  $old"
done

echo
echo "done. Boards Manager URL:"
echo "  $INDEX_URL"
echo
echo "Now commit tools/chgame_upload_tool.json, which points at $TAG, and run"
echo "'git fetch --tags' to pick up the tag gh created."
echo
echo "Verify from a clean state before announcing it:"
echo "  arduino-cli core uninstall CHGame:ch32v"
echo "  rm -rf \"\$ARDUINO15/packages/CHGame\""
echo "  arduino-cli core update-index --additional-urls $INDEX_URL"
echo "  arduino-cli core install CHGame:ch32v --additional-urls $INDEX_URL"
