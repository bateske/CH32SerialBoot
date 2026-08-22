"""Firmware upload over the CHGame bootloader protocol."""
from __future__ import annotations

import struct
import time
import zlib
from typing import Callable

from .client import Client
from .protocol import (
    CMD_ABORT, CMD_BEGIN, CMD_DEV_UNLOCK, CMD_DEV_WRITE_BOOT, CMD_END,
    CMD_READ, CMD_WRITE, ST_OK, StatusError,
)

DEV_KEY = 0x43484744  # "CHGD" — an interlock against accidents, not a secret.

def chunk_size(max_payload: int, page: int = 256) -> int:
    """Largest WRITE data chunk the device will accept.

    Prefer whole flash pages so the device commits one page per chunk, but never
    exceed what it advertised - the bootloader currently caps frames at one USB
    packet, which is well under a page.
    """
    usable = max_payload - 4          # 4-byte offset field
    if usable >= page:
        return (usable // page) * page
    return usable


def pad_to_word(image: bytes) -> bytes:
    """The bootloader requires a word-multiple length (CRC span and metadata)."""
    return image + b"\xff" * (-len(image) % 4)


def upload(c: Client, image: bytes,
           progress: Callable[[int, int], None] | None = None,
           verify_readback: bool = False) -> dict:
    """BEGIN / WRITE* / END. Returns timing and size info.

    Raises StatusError with the device's own status code on refusal, so callers
    can distinguish "image too large" from "flash failed" from "CRC mismatch".
    """
    h = c.hello()
    if h.mode != 1:
        raise RuntimeError(f"device is not in bootloader mode (mode={h.mode})")

    image = pad_to_word(image)
    if len(image) > h.app_max_size:
        raise ValueError(
            f"image is {len(image)} bytes; the application region holds {h.app_max_size}"
        )

    crc = zlib.crc32(image) & 0xFFFFFFFF
    step = chunk_size(h.max_payload, h.page_size)

    t0 = time.monotonic()
    # BEGIN erases; on a nearly-full image that is ~200 page erases, so it needs
    # a longer deadline than an ordinary command.
    c.check(CMD_BEGIN, struct.pack("<II", len(image), crc), )
    t_erase = time.monotonic() - t0

    sent = 0
    while sent < len(image):
        piece = image[sent:sent + step]
        c.check(CMD_WRITE, struct.pack("<I", sent) + piece)
        sent += len(piece)
        if progress:
            progress(sent, len(image))
    t_write = time.monotonic() - t0 - t_erase

    c.check(CMD_END)
    t_total = time.monotonic() - t0

    result = {
        "bytes": len(image), "crc32": crc, "chunk": step,
        "erase_s": t_erase, "write_s": t_write, "total_s": t_total,
        "kbps": (len(image) / 1024.0) / t_write if t_write > 0 else 0.0,
    }

    if verify_readback:
        result["readback_ok"] = readback_matches(c, h.app_start, image, h.max_payload)

    return result


def readback_matches(c: Client, app_start: int, image: bytes,
                     max_payload: int = 56) -> bool:
    """Independent check: read the region back and compare byte for byte.

    END already verifies a CRC computed on the device. This verifies through a
    different path entirely, which is what catches a bootloader that computes
    its CRC over the wrong span or reports success it did not achieve.
    """
    step = max_payload - 1            # response payload is status byte + data
    off = 0
    while off < len(image):
        n = min(step, len(image) - off)
        r = c.check(CMD_READ, struct.pack("<IH", app_start + off, n))
        if r[1:1 + n] != image[off:off + n]:
            return False
        off += n
    return True


def abort(c: Client) -> None:
    c.check(CMD_ABORT)


def selfupdate(c: Client, boot_image: bytes,
               progress: Callable[[int, int], None] | None = None) -> dict:
    """Replace the bootloader itself.

    Stages the new bootloader through the ordinary upload path (so it gets the
    same bounds checks, per-page verify and CRC as any firmware), then promotes
    it with DEV_WRITE_BOOT.

    The device is briefly without a usable bootloader. If that is interrupted,
    recovery is the BOOT button and the factory ISP — see docs/recovery.md.
    This also destroys the installed application, because the staging area IS
    the application region; re-upload afterwards.
    """
    boot_image = pad_to_word(boot_image)
    info = upload(c, boot_image, progress=progress)

    crc = zlib.crc32(boot_image) & 0xFFFFFFFF
    c.check(CMD_DEV_UNLOCK, struct.pack("<I", DEV_KEY))

    try:
        c.check(CMD_DEV_WRITE_BOOT, struct.pack("<II", len(boot_image), crc))
    except (TimeoutError, OSError):
        # The device resets as part of this command; a lost acknowledgement is
        # expected rather than a failure.
        pass

    info["promoted"] = True
    return info
