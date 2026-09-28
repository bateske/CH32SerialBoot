#!/usr/bin/env python3
"""Build the CHGame Arduino Boards Manager package.

Produces, in dist/:

    CHGame-ch32v-<version>.tar.bz2   the platform archive
    package_chgame_index.json        the Boards Manager index

The index is self-contained: it carries its own copy of the RISC-V toolchain
tool definition (taken from the upstream CH32 index), so installing CHGame pulls
everything it needs from ONE Boards Manager URL. The brief is explicit that a
user should not have to install a generic CH32 core, a USB serial library, or a
toolchain separately.

Usage:
    python tools/make_package.py --version 0.1.0 --base-url https://example.com/chgame
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil
import tarfile
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
PLATFORM = ROOT / "arduino" / "CHGame"
DIST = ROOT / "dist"

# Files that exist only for the development install and must never ship.
EXCLUDE_NAMES = {"platform.local.txt", "__pycache__", ".gitignore"}
EXCLUDE_SUFFIX = {".pyc"}

UPSTREAM_INDEX = pathlib.Path(
    r"C:\Users\kevin\AppData\Local\Arduino15\package_ch32_index.json")


def platform_version() -> str:
    """The version= line of platform.txt.

    That is the number Boards Manager compares with an installed package to
    decide whether to offer an update, so it is the one place a release is
    versioned; the archive name, the index and the release tag all follow it.
    """
    for line in (PLATFORM / "platform.txt").read_text().splitlines():
        if line.startswith("version="):
            return line.split("=", 1)[1].strip()
    raise SystemExit("no version= line in " + str(PLATFORM / "platform.txt"))


def _keep(path: pathlib.Path) -> bool:
    if path.name in EXCLUDE_NAMES or path.suffix in EXCLUDE_SUFFIX:
        return False
    return not any(part in EXCLUDE_NAMES for part in path.parts)


def build_archive(version: str) -> pathlib.Path:
    DIST.mkdir(exist_ok=True)
    out = DIST / f"CHGame-ch32v-{version}.tar.bz2"
    root_name = f"CHGame-ch32v-{version}"

    with tempfile.TemporaryDirectory() as td:
        staged = pathlib.Path(td) / root_name
        shutil.copytree(PLATFORM, staged)
        for p in sorted(staged.rglob("*"), reverse=True):
            if not _keep(p):
                shutil.rmtree(p, ignore_errors=True) if p.is_dir() else p.unlink()

        # Arduino requires the archive to contain exactly one root directory.
        with tarfile.open(out, "w:bz2") as tf:
            tf.add(staged, arcname=root_name)
    return out


def toolchain_definition() -> dict:
    """Reuse the upstream toolchain tool definition verbatim.

    Copied rather than re-hosted: the binaries are unchanged, the URLs and
    checksums are already proven, and duplicating a 200 MB toolchain to change
    nothing would be silly. Carrying the DEFINITION in our index is what keeps
    the install to a single Boards Manager URL.
    """
    if not UPSTREAM_INDEX.exists():
        raise SystemExit(
            f"upstream index not found at {UPSTREAM_INDEX}\n"
            "Install the CH32_Arduino core once so the toolchain definition can be reused.")
    d = json.loads(UPSTREAM_INDEX.read_text())
    for pkg in d["packages"]:
        for tool in pkg.get("tools", []):
            if tool["name"] == "riscv-none-embed-gcc":
                return tool
    raise SystemExit("riscv-none-embed-gcc not found in the upstream index")


def wchisp_definition() -> dict:
    """wchisp, declared as a tool dependency so Boards Manager fetches it.

    Only the factory-ISP paths use it (Burn Bootloader, Upload Using
    Programmer); normal uploads never touch it. It is a dependency anyway
    because a board package that installs cleanly and then fails the first time
    you try to provision a board is a bad experience - the failure should be
    impossible, not merely explained.

    The index references the upstream GitHub release assets with pinned
    checksums rather than re-hosting them. wchisp is GPL-2.0; we invoke it as a
    separate process and do not redistribute it, so referencing avoids taking on
    redistribution obligations. The trade-off is availability - if upstream
    removes those assets the install breaks - so a public release should mirror
    them and ship the licence and a source offer alongside.
    """
    src = ROOT / "tools" / "wchisp_tool.json"
    if not src.exists():
        raise SystemExit(f"missing {src}; run the checksum step first")
    return json.loads(src.read_text())


def uploader_definition() -> dict:
    """chgame-upload, built by host/go/build.sh and archived per host.

    A tool dependency rather than a script inside the platform, because Arduino
    provides no interpreter. Cross-compiled for all five hosts from one machine
    with CGO disabled.
    """
    src = ROOT / "tools" / "chgame_upload_tool.json"
    if not src.exists():
        raise SystemExit(
            f"missing {src}; run host/go/build.sh then tools/make_tool_archives.py")
    return json.loads(src.read_text())


def check_uploader_urls(uploader: dict, base_url: str) -> None:
    """Refuse to emit an index whose uploader URLs point somewhere else.

    Caught exactly this in a dry run: the platform archive carried the release
    URL while the uploader still pointed at a local test server, which would
    have installed the board and then failed to fetch its own upload tool.
    """
    prefix = base_url.rstrip("/")
    bad = [s["url"] for s in uploader["systems"] if not s["url"].startswith(prefix)]
    if not bad:
        return
    msg = ["uploader archives were built for a different base URL:"]
    msg += ["  " + u for u in bad[:2]]
    msg.append("expected them under " + prefix)
    msg.append("Re-run: python tools/make_tool_archives.py --base-url " + prefix)
    raise SystemExit(chr(10).join(msg))


def build_index(version: str, archive: pathlib.Path, base_url: str) -> pathlib.Path:
    blob = archive.read_bytes()
    tool = toolchain_definition()
    wchisp = wchisp_definition()
    uploader = uploader_definition()
    check_uploader_urls(uploader, base_url)

    index = {
        "packages": [{
            "name": "CHGame",
            "maintainer": "CHGame",
            "websiteURL": "https://github.com/bateske/CH32SerialBoot",
            "email": "",
            "help": {"online": "https://github.com/bateske/CH32SerialBoot/issues"},
            "platforms": [{
                "name": "CHGame Boards",
                "architecture": "ch32v",
                "version": version,
                "category": "Contributed",
                "help": {"online": "https://github.com/bateske/CH32SerialBoot/issues"},
                "url": f"{base_url.rstrip('/')}/{archive.name}",
                "archiveFileName": archive.name,
                "checksum": "SHA-256:" + hashlib.sha256(blob).hexdigest(),
                "size": str(len(blob)),
                "boards": [{"name": "CHGame"}],
                "toolsDependencies": [
                    {"packager": "CHGame", "name": tool["name"], "version": tool["version"]},
                    {"packager": "CHGame", "name": wchisp["name"], "version": wchisp["version"]},
                    {"packager": "CHGame", "name": uploader["name"], "version": uploader["version"]},
                ],
            }],
            "tools": [tool, wchisp, uploader],
        }]
    }

    out = DIST / "package_chgame_index.json"
    out.write_text(json.dumps(index, indent=2) + "\n", newline="\n")
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default=None,
                    help="defaults to platform.txt, and must match it when given")
    ap.add_argument("--base-url", default="",
                    help="where the archive will be hosted; blank makes a local file:// index")
    args = ap.parse_args()

    version = platform_version()
    if args.version and args.version != version:
        raise SystemExit(
            f"--version {args.version} does not match platform.txt ({version}). "
            "Bump platform.txt: that is the number Boards Manager compares to offer updates.")

    archive = build_archive(version)
    base = args.base_url or DIST.as_uri()
    index = build_index(version, archive, base)

    print(f"archive : {archive}  ({archive.stat().st_size:,} bytes)")
    print(f"index   : {index}")
    print(f"base URL: {base}")
    print()
    index_url = f"{base.rstrip('/')}/{index.name}" if args.base_url else index.as_uri()
    print("Boards Manager URL:")
    print(f"  {index_url}")
    print()
    print("Install with:")
    print(f"  arduino-cli core update-index --additional-urls {index_url}")
    print(f"  arduino-cli core install CHGame:ch32v --additional-urls {index_url}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
