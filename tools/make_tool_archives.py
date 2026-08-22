"""Archive the chgame-upload binaries and emit their Arduino tool definition.

The base URL matters: the index pins a SHA-256 and an absolute URL for every
archive, so these must resolve to exactly where the release assets land. It is a
REQUIRED argument rather than a default precisely because a default is how an
index ends up published still pointing at a local test server.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import tarfile
import tempfile

ROOT = pathlib.Path(r"D:\LocalProjects\CH32SerialBoot")
DIST = ROOT / "dist"
TOOLS = DIST / "tools"
VERSION = "0.1.0"

HOSTS = [
    "x86_64-mingw32",
    "x86_64-pc-linux-gnu",
    "aarch64-linux-gnu",
    "x86_64-apple-darwin",
    "arm64-apple-darwin",
]


def package_tool(base_url: str) -> dict:
    """Archive each host's binary and build the Arduino tool definition."""
    systems = []
    out_dir = DIST / "tool-archives"
    out_dir.mkdir(parents=True, exist_ok=True)

    for host in HOSTS:
        src = TOOLS / host
        if not src.is_dir():
            raise SystemExit(f"missing build for {host}; run host/go/build.sh first")

        name = f"chgame-upload-{VERSION}-{host}.tar.bz2"
        archive = out_dir / name
        root = f"chgame-upload-{VERSION}"

        with tempfile.TemporaryDirectory() as td:
            staged = pathlib.Path(td) / root
            shutil.copytree(src, staged)
            with tarfile.open(archive, "w:bz2") as tf:
                # Arduino strips a single root directory on extraction, so the
                # binary lands directly in the tool folder.
                tf.add(staged, arcname=root)

        blob = archive.read_bytes()
        systems.append({
            "host": host,
            # GitHub release assets are FLAT - there are no subdirectories - so the
            # URL must not carry the local tool-archives/ path segment even
            # though that is where the file sits on disk.
            "url": f"{base_url.rstrip('/')}/{name}",
            "archiveFileName": name,
            "checksum": "SHA-256:" + hashlib.sha256(blob).hexdigest(),
            "size": str(len(blob)),
        })
        print(f"  {host:22s} {len(blob):>8,} B")

    return {"name": "chgame-upload", "version": VERSION, "systems": systems}


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Archive chgame-upload per host and emit its tool definition.")
    # REQUIRED, not defaulted. The index pins an absolute URL and a SHA-256 for
    # every archive, and a default is exactly how an index gets published still
    # pointing at a local test server.
    ap.add_argument("--base-url", required=True,
                    help="where the release assets will live, eg "
                         "https://github.com/OWNER/REPO/releases/download/v0.2.0")
    args = ap.parse_args()

    tool = package_tool(args.base_url)
    out = ROOT / "tools" / "chgame_upload_tool.json"
    out.write_text(json.dumps(tool, indent=2) + chr(10), newline=chr(10))
    print()
    print("wrote " + out.name + " pointing at " + args.base_url)


if __name__ == "__main__":
    main()
