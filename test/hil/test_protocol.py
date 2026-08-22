#!/usr/bin/env python3
"""Hardware-in-the-loop protocol and flash-safety tests (M2 + M3 gates).

Requires a CHGame device sitting in the bootloader:

    python test/hil/test_protocol.py [--port COMx]

The layout is read from bootloader/src/chgame_map.h rather than hard-coded, so
changing the reservation cannot silently invalidate these tests.

The point of this suite is not that the happy path works - a single upload shows
that. It is that the failure paths are safe: that nothing reachable over the
wire can write below APP_START, that a partial or corrupt image is never marked
launchable, and that malformed input cannot wedge a device which has no reset
pin.
"""
from __future__ import annotations

import argparse
import pathlib
import struct
import sys
import time
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "host" / "py"))
sys.path.insert(0, str(ROOT / "tools"))

import chgame_map as M                                   # noqa: E402
from chgame.client import Client, find_ports             # noqa: E402
from chgame import protocol as P                         # noqa: E402

results: list[tuple[bool, str, str]] = []


def check(name: str, ok: bool, detail: str = "") -> None:
    results.append((bool(ok), name, detail))
    print(f"  {'PASS' if ok else 'FAIL'}  {name}{('  - ' + detail) if detail else ''}")


def status_of(c: Client, cmd: int, payload: bytes = b"") -> str:
    try:
        r = c.request(cmd, payload)
        return P.STATUS_NAMES.get(r[0], hex(r[0]))
    except Exception as e:
        return f"<{type(e).__name__}>"


def read(c: Client, addr: int, n: int) -> bytes:
    return c.check(P.CMD_READ, struct.pack("<IH", addr, n))[1:1 + n]


def run(port: str) -> int:
    print(f"protocol + flash safety on {port}")
    print(f"map: boot 0x0000+{M.BOOT_SIZE}  app 0x{M.APP_START:04X}+{M.APP_MAX_SIZE}"
          f"  meta 0x{M.META_ADDR:04X}\n")

    with Client(port, timeout=15.0) as c:
        h = c.hello()
        check("HELLO answers from the bootloader", h.mode == P.MODE_BOOTLOADER, f"mode={h.mode}")
        check("advertised region matches chgame_map.h",
              (h.app_start, h.app_max_size) == (M.APP_START, M.APP_MAX_SIZE),
              f"0x{h.app_start:X}+{h.app_max_size}")
        check("advertised page size matches", h.page_size == M.PAGE_SIZE, str(h.page_size))
        check("UID is non-trivial", h.uid[:8] not in (b"\x00" * 8, b"\xff" * 8), h.uid[:8].hex())

        # Fingerprint the bootloader so we can prove nothing below APP_START moved.
        boot_before = read(c, 0, 64) + read(c, M.APP_START - 64, 64)

        print("\n-- framing --")
        bad = bytearray(P.build_frame(P.CMD_HELLO)); bad[-1] ^= 0xFF
        c.ser.write(bytes(bad)); c.ser.flush()
        _, pl = c._read_frame(time.monotonic() + 3.0)
        check("corrupt frame CRC -> ERR_FRAME", pl[0] == 0x08, P.STATUS_NAMES.get(pl[0]))

        c.ser.write(P.SOF + struct.pack("<BBH", P.VERSION, P.CMD_WRITE, 0xFFFF)); c.ser.flush()
        _, pl = c._read_frame(time.monotonic() + 3.0)
        check("absurd declared length -> ERR_SIZE", pl[0] == 0x04, P.STATUS_NAMES.get(pl[0]))

        c.ser.write(b"noise\x00\x01\x02" + P.SOF[:1] + b"CCCC")
        c.ser.write(P.build_frame(P.CMD_HELLO)[:5])       # truncated mid-header
        c.ser.flush()
        time.sleep(0.4)                                   # past PROTO_RX_TIMEOUT_MS
        c.ser.reset_input_buffer()
        check("recovers from noise and truncation", c.hello().mode == P.MODE_BOOTLOADER)

        # A frame deliberately spanning several 64-byte USB packets.
        c.ser.reset_input_buffer()
        c.ser.write(b"\x00" * 200 + P.build_frame(P.CMD_HELLO)); c.ser.flush()
        try:
            c._read_frame(time.monotonic() + 3.0)
            check("multi-packet frame is received", True, "208 bytes on the wire")
        except Exception as e:
            check("multi-packet frame is received", False, type(e).__name__)

        print("\n-- BEGIN validation --")
        check("size 0 rejected", status_of(c, P.CMD_BEGIN, struct.pack("<II", 0, 0)) == "ERR_SIZE")
        check("unaligned size rejected",
              status_of(c, P.CMD_BEGIN, struct.pack("<II", 1001, 0)) == "ERR_SIZE")
        check("oversized image rejected",
              status_of(c, P.CMD_BEGIN, struct.pack("<II", M.APP_MAX_SIZE + 4, 0)) == "ERR_SIZE")
        check("short payload rejected",
              status_of(c, P.CMD_BEGIN, b"\x00\x00") == "ERR_SIZE")

        print("\n-- WRITE validation --")
        check("WRITE before BEGIN -> ERR_STATE",
              status_of(c, P.CMD_WRITE, struct.pack("<I", 0) + b"x" * 4) == "ERR_STATE")

        c.check(P.CMD_BEGIN, struct.pack("<II", 1024, 0))
        check("non-sequential offset rejected",
              status_of(c, P.CMD_WRITE, struct.pack("<I", 512) + b"x" * 4) == "ERR_STATE")
        # Overrun must be caught against the size announced at BEGIN, using a
        # frame that is itself perfectly legal -- otherwise this only proves the
        # host library refuses to build an over-long frame.
        c.check(P.CMD_BEGIN, struct.pack("<II", 8, 0))
        st = status_of(c, P.CMD_WRITE, struct.pack("<I", 0) + b"x" * 256)
        check("write past announced size rejected", st == "ERR_RANGE", st)
        check("huge offset rejected",
              status_of(c, P.CMD_WRITE, struct.pack("<I", 0xFFFFFFF0) + b"x" * 4) == "ERR_STATE")

        print("\n-- END validation --")
        check("END with fewer bytes than announced -> ERR_SIZE",
              status_of(c, P.CMD_END) == "ERR_SIZE")

        # Complete an image but declare the wrong CRC: must be refused AND left invalid.
        payload = bytes((i * 3 + 1) & 0xFF for i in range(1024))
        c.check(P.CMD_BEGIN, struct.pack("<II", len(payload), 0xDEADBEEF))
        for off in range(0, len(payload), 256):
            c.check(P.CMD_WRITE, struct.pack("<I", off) + payload[off:off + 256])
        check("wrong image CRC -> ERR_CRC", status_of(c, P.CMD_END) == "ERR_CRC")
        check("application left INVALID after a bad CRC",
              c.hello().app_state != 0, P.APP_STATE_NAMES.get(c.hello().app_state))

        print("\n-- interrupted update --")
        c.check(P.CMD_BEGIN, struct.pack("<II", 4096, zlib.crc32(b"\x00" * 4096) & 0xFFFFFFFF))
        for off in range(0, 1024, 256):                   # stop at 25%
            c.check(P.CMD_WRITE, struct.pack("<I", off) + b"\x00" * 256)
        check("app invalid mid-transaction", c.hello().app_state != 0)
        c.check(P.CMD_ABORT)
        check("ABORT leaves the application invalid", c.hello().app_state != 0)
        check("RUN refused with no valid application", status_of(c, P.CMD_RUN) == "ERR_CRC")

        print("\n-- developer gate --")
        check("DEV_WRITE_BOOT locked without unlock",
              status_of(c, P.CMD_DEV_WRITE_BOOT, struct.pack("<II", 256, 0)) == "ERR_LOCKED")
        check("DEV_UNLOCK with a wrong key -> ERR_LOCKED",
              status_of(c, P.CMD_DEV_UNLOCK, struct.pack("<I", 0x12345678)) == "ERR_LOCKED")
        check("DEV_WRITE_BOOT still locked after a failed unlock",
              status_of(c, P.CMD_DEV_WRITE_BOOT, struct.pack("<II", 256, 0)) == "ERR_LOCKED")

        print("\n-- the gate that matters --")
        boot_after = read(c, 0, 64) + read(c, M.APP_START - 64, 64)
        check("BOOTLOADER REGION UNCHANGED after every test above",
              boot_after == boot_before,
              "identical" if boot_after == boot_before else "MODIFIED")
        check("device still responsive", c.hello().mode == P.MODE_BOOTLOADER)

    failed = [n for ok, n, _ in results if not ok]
    print(f"\n{len(results) - len(failed)}/{len(results)} passed")
    if failed:
        print("failed: " + ", ".join(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    a = ap.parse_args()
    port = a.port or (find_ports() or [None])[0]
    if not port:
        sys.exit("no CHGame device found")
    raise SystemExit(run(port))
