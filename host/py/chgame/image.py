"""Build a flat provisioning image: bootloader + application + metadata.

wchisp writes from address 0 and erases the whole code flash, so factory
provisioning and recovery need a single blob with everything in its final
place. The metadata page is written here exactly as the bootloader's own END
command writes it, so a provisioned device is indistinguishable from one that
was flashed over USB.
"""
from __future__ import annotations

import struct
import zlib

from . import layout as L


def build_metadata(app: bytes, app_version: int = 0) -> bytes:
    meta = struct.pack(
        "<8I",
        L.META_MAGIC, L.META_VERSION, len(app), zlib.crc32(app) & 0xFFFFFFFF,
        app_version, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    )
    return meta + bytes([L.ERASED]) * (L.PAGE_SIZE - len(meta))


def pad_to_word(data: bytes) -> bytes:
    return data + bytes([L.ERASED]) * (-len(data) % 4)


def build_image(boot: bytes, app: bytes | None = None, app_version: int = 0) -> bytes:
    """Compose the full 62 KB flash image.

    With no application, the device comes up in the bootloader and waits — which
    is the correct state for a freshly provisioned board: it enumerates as a CDC
    port immediately and the first sketch can be uploaded over USB with no
    further use of the BOOT button.
    """
    if len(boot) > L.BOOT_SIZE:
        raise ValueError(
            f"bootloader is {len(boot)} bytes, over the {L.BOOT_SIZE}-byte reservation")

    image = bytearray([L.ERASED]) * L.FLASH_SIZE
    image[0:len(boot)] = boot

    if app is not None:
        app = pad_to_word(app)
        if len(app) > L.APP_MAX_SIZE:
            raise ValueError(
                f"application is {len(app)} bytes, over the {L.APP_MAX_SIZE}-byte region")
        image[L.APP_START:L.APP_START + len(app)] = app
        meta = build_metadata(app, app_version)
        image[L.META_ADDR:L.META_ADDR + len(meta)] = meta

    return bytes(image)


def trim(image: bytes) -> bytes:
    """Drop trailing erased bytes; there is no need to write 0xFF."""
    end = len(image)
    while end > 0 and image[end - 1] == L.ERASED:
        end -= 1
    return image[:end]
