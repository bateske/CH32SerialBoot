# Building and releasing

## Prerequisites

| Tool | Needed for | Notes |
|---|---|---|
| RISC-V GCC (`riscv-none-embed-gcc` 8.2.0) | bootloader, sketches | Installed by the CHGame board package, or by the upstream CH32 core |
| Go 1.21+ | the uploader | Only to rebuild `chgame-upload`; not needed to use the board |
| Python 3.10+ | build scripts, tests | Not needed by end users — the shipping uploader is a binary |
| `wchisp` | first flash and recovery | Installed by the board package |
| `arduino-cli` | compiling sketches, packaging | |

A portable Go toolchain can be dropped in `.toolchains/go` (gitignored) rather
than installed system-wide; `host/go/build.sh` finds it there automatically.

## Bootloader

```bash
./tools/build_all.sh
```

This regenerates the linker scripts from `bootloader/src/chgame_map.h`, builds
the bootloader and the test blink application, and composes
`build/chgame_full.bin` for flashing through the factory ISP.

`CHGAME_DIAG=1 ./tools/build_all.sh` re-enables the LED boot report — a blinked
boot counter and image verdict on every reset. Off by default because it costs
time on the normal boot path; invaluable when USB will not come up. See the
diagnosing section of `ch32x035-gotchas.md`.

**The layout has one source of truth**: `bootloader/src/chgame_map.h`.
`tools/gen_ld.py` rewrites the `MEMORY` block and overflow assertions in both
linker scripts from it, and `host/py/chgame/layout.py` mirrors it for host tools
that ship without the header. Change the header, not the copies.

## Uploader

```bash
./host/go/build.sh          # -> dist/tools/<host>/chgame-upload[.exe]
```

Cross-compiles for all five hosts Arduino supports from a single machine, with
CGO disabled. macOS avoids the cgo-only IOKit enumerator via a build-tagged
fallback in `ports_darwin.go`; Arduino always passes `-port` explicitly, so
VID/PID discovery is only a convenience for running the tool by hand.

## Platform, for local testing

```bash
./tools/sync_platform.sh
```

Copies the current bootloader binary and platform into
`Arduino15/packages/CHGame/`, plus the freshly built Windows uploader. Adds a
`platform.local.txt` pointing at an existing toolchain so a full toolchain does
not have to be duplicated — **that file is excluded from the released archive**,
where a real tool dependency provides the compiler.

## Releasing

```bash
./tools/build_all.sh
./host/go/build.sh
python tools/make_tool_archives.py
python tools/make_package.py --version 0.2.0 \
    --base-url https://github.com/bateske/CH32SerialBoot/releases/download/v0.2.0
```

Produces in `dist/`:

- `CHGame-ch32v-<version>.tar.bz2` — the platform archive
- `tool-archives/chgame-upload-<version>-<host>.tar.bz2` — one per host
- `package_chgame_index.json` — the Boards Manager index

Upload **all** of those to a GitHub release tagged `v<version>`. The index
references the archives by URL with pinned SHA-256 checksums, so the URLs must
resolve exactly as given to `--base-url`.

`tools/release.sh` does the whole sequence, including creating the release with
`gh`.

### What the index contains

One packager, `CHGame`, carrying three tools so that a single Boards Manager URL
installs everything:

| Tool | Source |
|---|---|
| `riscv-none-embed-gcc` | definition copied verbatim from the upstream CH32 index — same binaries, already proven |
| `wchisp` | upstream GitHub release assets, checksums pinned |
| `chgame-upload` | built here, archived per host |

Two hosting caveats worth fixing before a public release: the wchisp entry
references somebody else's release assets, so availability is not guaranteed;
and the toolchain entry does the same. Mirroring both into your own release makes
installs reproducible. wchisp is GPL-2.0 — mirroring it means redistributing it,
so ship its licence and a source offer alongside.

### Verifying a release

```bash
arduino-cli core uninstall CHGame:ch32v
rm -rf "$ARDUINO15/packages/CHGame"
arduino-cli core update-index --additional-urls <index-url>
arduino-cli core install CHGame:ch32v --additional-urls <index-url>
arduino-cli compile -b CHGame:ch32v:CHGame -u -p <port> test/sketches/BlinkSerial
```

Removing the package directory matters: `arduino-cli` will not re-download a
tool it already has at the same version, so an in-place upgrade can silently test
a stale binary. Bump the version, or delete the directory.

## Test suite

```bash
python test/hil/test_protocol.py           # 27 protocol and flash-safety checks
python test/hil/test_soak.py --cycles 100  # unattended, ~5 minutes
python test/hil/test_powercut.py arm --hold-at 95
python test/hil/test_powercut.py verify    # after restoring power
```

The soak alternates between two different images deliberately: uploading the same
bytes repeatedly would pass even if the device quietly ignored the write and kept
running the previous sketch.

The power-cut test is the one thing that cannot be automated here — only
physically removing power can interrupt a flash page mid-program.
