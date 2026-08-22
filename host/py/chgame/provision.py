"""Factory provisioning and recovery through the WCH factory ISP.

This is the ONLY path that uses the BOOT button, and it exists for exactly two
situations named in the brief: the first flash of a blank device, and recovery
from a damaged bootloader. Everything else goes over USB CDC with no driver and
no button.

It is wired to Arduino's standard mechanisms rather than a bespoke workflow:

    Tools -> Burn Bootloader          -> bootloader only
    Sketch -> Upload Using Programmer -> bootloader + this sketch, in one write

Both require the device to already be in factory ISP mode, which on this board
means holding BOOT across a power cycle (BOOT pulls USB D+ high through R10 and
the part samples it at reset). See docs/recovery.md.
"""
from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import tempfile

from .image import build_image, trim


class WchispNotFound(Exception):
    pass


def find_wchisp(explicit: str | None = None) -> str:
    """Locate wchisp.

    Order: explicit argument, CHGAME_WCHISP, PATH, then a couple of conventional
    install locations. The shipping Board Manager package declares wchisp as a
    tool dependency so this search normally succeeds on the first entry.
    """
    candidates: list[str] = []
    if explicit:
        candidates.append(explicit)
    if os.environ.get("CHGAME_WCHISP"):
        candidates.append(os.environ["CHGAME_WCHISP"])

    found = shutil.which("wchisp") or shutil.which("wchisp.exe")
    if found:
        candidates.append(found)

    candidates += [r"D:\WCHISP\wchisp.exe", r"C:\WCHISP\wchisp.exe",
                   "/usr/local/bin/wchisp"]

    for c in candidates:
        if not c:
            continue
        cand = pathlib.Path(c)
        if cand.is_file():
            return str(cand)
        # Arduino recipes may hand us a name without the platform extension.
        exe = cand.with_name(cand.name + ".exe")
        if exe.is_file():
            return str(exe)
    raise WchispNotFound(
        "wchisp not found. Set CHGAME_WCHISP to its full path, or put it on PATH.\n"
        "It is only needed for the first flash of a blank board and for recovery;\n"
        "normal uploads do not use it.")


def probe(wchisp: str) -> str:
    """Confirm a device is in factory ISP mode, and name it.

    Keyed on the POSITIVE signal - a "Device #" line - rather than on error
    wording. wchisp changed "Found 0 devices" to "Found 0 USB devices" between
    0.2.3 and 0.3.0, and matching the failure text meant a missing device
    sailed past this check and surfaced later as a raw wchisp error instead of
    the instruction the user actually needs.
    """
    r = subprocess.run([wchisp, "probe"], capture_output=True, text=True)
    out = r.stdout + r.stderr
    for line in out.splitlines():
        if "Device #" in line:
            return line.split("Device #", 1)[1].strip()
    # chr(10).join avoids embedding escape sequences, which is worth the mild
    # awkwardness: this message is the entire user-facing instruction for the
    # one step that cannot be automated.
    raise RuntimeError(chr(10).join([
        "no device is in factory ISP mode.",
        "",
        "  1. Hold the BOOT button down.",
        "  2. Switch the power OFF, then ON, while still holding BOOT.",
        "  3. Release BOOT.",
        "  4. Try Burn Bootloader again.",
        "",
        "  BOOT must be held ACROSS the power cycle: the chip samples it only",
        "  at reset, so pressing it while the board is already running does",
        "  nothing.",
    ]))


def provision(boot_path: pathlib.Path, app_path: pathlib.Path | None = None,
              wchisp: str | None = None, app_version: int = 0) -> None:
    tool = find_wchisp(wchisp)

    boot = boot_path.read_bytes()
    app = app_path.read_bytes() if app_path else None

    print(f"wchisp      : {tool}")
    print(f"device      : {probe(tool)}")
    print(f"bootloader  : {boot_path.name}  ({len(boot)} bytes)")
    if app is not None:
        print(f"application : {app_path.name}  ({len(app)} bytes)")
    else:
        print("application : none - the device will come up in the bootloader,")
        print("              enumerate as a COM port, and wait for a normal upload")

    image = trim(build_image(boot, app, app_version))

    with tempfile.TemporaryDirectory() as td:
        blob = pathlib.Path(td) / "chgame_provision.bin"
        blob.write_bytes(image)
        r = subprocess.run([tool, "flash", str(blob)], capture_output=True, text=True)
        out = r.stdout + r.stderr
        if r.returncode != 0 or "Verify OK" not in out:
            raise RuntimeError(f"wchisp failed:\n{out}")

    print(f"written     : {len(image)} bytes, verified")
    print("done        - the BOOT button is not needed again; use Upload from here on")
