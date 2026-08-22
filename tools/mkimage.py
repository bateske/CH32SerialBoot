#!/usr/bin/env python3
"""Combine a bootloader and an application into one flat image for wchisp.

wchisp writes from address 0 and erases the whole code flash, so factory
provisioning (and every bootloader iteration during development) needs a single
blob with everything in its final place:

    0x0000  bootloader
    0x2000  application
    0xF700  application metadata  (magic, length, CRC-32, version)

The metadata is what the bootloader consults to decide whether the application
is safe to launch, so it is written here exactly the way the bootloader's own
END command writes it — same struct, same CRC.
"""
import argparse
import pathlib
import struct
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import chgame_map as M  # noqa: E402

ERASED = 0xFF


def build_metadata(app: bytes, app_version: int = 0) -> bytes:
    meta = struct.pack(
        "<8I",
        M.META_MAGIC, M.META_VERSION, len(app), zlib.crc32(app) & 0xFFFFFFFF,
        app_version, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    )
    return meta + bytes([ERASED]) * (M.PAGE_SIZE - len(meta))


def build_image(boot: bytes, app: bytes | None, app_version: int = 0) -> bytes:
    if len(boot) > M.BOOT_SIZE:
        raise ValueError(f"bootloader is {len(boot)} bytes, over the {M.BOOT_SIZE}-byte reservation")

    image = bytearray([ERASED]) * M.FLASH_SIZE
    image[0:len(boot)] = boot

    if app is not None:
        app = app + bytes([ERASED]) * (-len(app) % 4)   # metadata length must be word aligned
        if len(app) > M.APP_MAX_SIZE:
            raise ValueError(f"application is {len(app)} bytes, over the {M.APP_MAX_SIZE}-byte region")
        image[M.APP_START:M.APP_START + len(app)] = app
        meta = build_metadata(app, app_version)
        image[M.META_ADDR:M.META_ADDR + len(meta)] = meta

    return bytes(image)


def trim(image: bytes) -> bytes:
    """Drop the trailing erased bytes; wchisp does not need to write 0xFF."""
    end = len(image)
    while end > 0 and image[end - 1] == ERASED:
        end -= 1
    return image[:end]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--boot", required=True, type=pathlib.Path, help="bootloader .bin")
    ap.add_argument("--app", type=pathlib.Path, help="application .bin (optional)")
    ap.add_argument("--app-version", type=lambda s: int(s, 0), default=0)
    ap.add_argument("-o", "--out", required=True, type=pathlib.Path)
    ap.add_argument("--no-trim", action="store_true",
                    help="keep the full 62 KB including trailing erased bytes")
    args = ap.parse_args()

    boot = args.boot.read_bytes()
    app = args.app.read_bytes() if args.app else None

    image = build_image(boot, app, args.app_version)
    if not args.no_trim:
        image = trim(image)
    args.out.write_bytes(image)

    print(f"bootloader : {len(boot):6d} bytes  @ 0x{0:04X}  "
          f"({len(boot) * 100 // M.BOOT_SIZE}% of the {M.BOOT_SIZE}-byte reservation)")
    if app is not None:
        padded = app + bytes([ERASED]) * (-len(app) % 4)
        print(f"application: {len(app):6d} bytes  @ 0x{M.APP_START:04X}  "
              f"({len(app) * 100 // M.APP_MAX_SIZE}% of the {M.APP_MAX_SIZE}-byte region)")
        print(f"metadata   : len={len(padded)} crc32=0x{zlib.crc32(padded) & 0xFFFFFFFF:08X} "
              f"@ 0x{M.META_ADDR:04X}")
    else:
        print("application: none — device will stay in the bootloader")
    print(f"-> {args.out} ({len(image)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
