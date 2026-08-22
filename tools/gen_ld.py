#!/usr/bin/env python3
"""Regenerate the linker scripts' MEMORY block and overflow ASSERT from chgame_map.h.

The C header is the single source of truth for the flash/RAM layout. The linker
scripts need the same numbers, and GNU ld cannot include a C header, so this
rewrites the generated regions in place. Run from tools/build_all.sh so the two
can never drift.

Only the marked regions are touched; everything else in the scripts is hand
written and preserved.
"""
from __future__ import annotations

import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import chgame_map as M  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent

BOOT_LD = ROOT / "bootloader" / "ld" / "link_boot.ld"
APP_LD = ROOT / "arduino" / "CHGame" / "system" / "CH32X035" / "SRC" / "Ld" / "link_chgame_app.ld"

RAM_START = M.MAP["CHGAME_RAM_BASE"] + M.MAP["CHGAME_MAGIC_SIZE"]
RAM_LEN = M.MAP["CHGAME_RAM_SIZE"] - M.MAP["CHGAME_MAGIC_SIZE"]


def memory_block(flash_origin: int, flash_len: int) -> str:
    return (
        "MEMORY\n"
        "{\n"
        f"    FLASH (rx)  : ORIGIN = 0x{flash_origin:08X}, LENGTH = {flash_len}\n"
        f"    MAGIC (rw)  : ORIGIN = 0x{M.MAP['CHGAME_MAGIC_ADDR']:08X}, "
        f"LENGTH = {M.MAP['CHGAME_MAGIC_SIZE']}\n"
        f"    RAM   (xrw) : ORIGIN = 0x{RAM_START:08X}, LENGTH = {RAM_LEN}\n"
        "}\n"
    )


def patch(path: pathlib.Path, flash_origin: int, flash_len: int,
          assert_limit: int, assert_msg: str) -> bool:
    text = path.read_text()
    before = text

    text = re.sub(r"MEMORY\n\{.*?\n\}\n", memory_block(flash_origin, flash_len),
                  text, count=1, flags=re.S)

    text = re.sub(r"ASSERT\(\s*_etext <= 0x[0-9A-Fa-f]+,[^)]*\)",
                  f'ASSERT( _etext <= 0x{assert_limit:X}, "{assert_msg}" )',
                  text, count=1)

    if text != before:
        path.write_text(text)
        return True
    return False


def main() -> int:
    changed = []

    if patch(BOOT_LD, M.MAP["CHGAME_BOOT_START"], M.BOOT_SIZE, M.APP_START,
             "BOOTLOADER OVERFLOW: code+rodata would spill into the application region"):
        changed.append(BOOT_LD.name)

    if patch(APP_LD, M.APP_START, M.APP_MAX_SIZE, M.META_ADDR,
             "APPLICATION OVERFLOW: code+rodata reaches the metadata page"):
        changed.append(APP_LD.name)

    print(f"layout: bootloader 0x0000+{M.BOOT_SIZE}  "
          f"app 0x{M.APP_START:04X}+{M.APP_MAX_SIZE}  "
          f"meta 0x{M.META_ADDR:04X}")
    print("linker scripts: " + (", ".join(changed) + " updated" if changed else "already in sync"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
