#!/usr/bin/env bash
# Cross-compile chgame-upload for every host Arduino supports.
#
# CGO stays disabled so all five targets build from one machine: that is the
# whole reason this is Go rather than a frozen Python script. See
# ports_darwin.go for how macOS avoids the cgo-only IOKit enumerator.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"

GO="${CHGAME_GO:-$HERE/../../.toolchains/go/bin/go.exe}"
[ -x "$GO" ] || GO="$(command -v go)"
export GOTOOLCHAIN=local CGO_ENABLED=0

OUT="${1:-$HERE/../../dist/tools}"
mkdir -p "$OUT"

build() {  # goos goarch arduino-host-triplet
  local ext=""; [ "$1" = "windows" ] && ext=".exe"
  local dir="$OUT/$3"
  mkdir -p "$dir"
  GOOS=$1 GOARCH=$2 "$GO" build -ldflags "-s -w" -o "$dir/chgame-upload$ext" .
  printf '  %-22s %9s bytes\n' "$3" "$(stat -c %s "$dir/chgame-upload$ext")"
}

echo "building chgame-upload:"
build windows amd64 x86_64-mingw32
build linux   amd64 x86_64-pc-linux-gnu
build linux   arm64 aarch64-linux-gnu
build darwin  amd64 x86_64-apple-darwin
build darwin  arm64 arm64-apple-darwin
