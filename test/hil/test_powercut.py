#!/usr/bin/env python3
"""M4: interrupted-update recovery under real power loss.

Simulated disconnects prove the host handles a vanished port. Only pulling the
actual power proves the DEVICE is safe, because only that can interrupt a flash
page mid-program. This is the one test that cannot be automated on this bench.

Usage:

    python test/hil/test_powercut.py arm --at 50     # start, prompt for the cut
    python test/hil/test_powercut.py verify          # after power is restored

The property under test: an interrupted update must leave the device in the
bootloader with NO valid application. It must never launch a partially written
image, and it must never need the BOOT button to recover.
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

import chgame_map as M                          # noqa: E402
from chgame.client import Client, find_ports    # noqa: E402
from chgame import protocol as P                # noqa: E402

# Big enough that the write phase lasts long enough to interrupt, and patterned
# so a partial write is obvious in a readback.
IMAGE = bytes((i * 31 + 7) & 0xFF for i in range(32768))


def arm(port: str, at_pct: int, seconds: float, start_delay: float = 0.0,
        hold_at: int | None = None) -> int:
    crc = zlib.crc32(IMAGE) & 0xFFFFFFFF
    with Client(port, timeout=15.0) as c:
        h = c.hello()
        if h.mode != P.MODE_BOOTLOADER:
            sys.exit("device is not in the bootloader - press A first")

        step = 256
        total = len(IMAGE)
        chunks = total // step
        delay = seconds / chunks

        print(f"image {total} B, {chunks} chunks, paced over ~{seconds:.0f}s")
        if start_delay:
            # The operator needs time to read the instructions before the write
            # phase starts; this output is captured, not live.
            print(f"holding {start_delay:.0f}s before the write phase...")
            time.sleep(start_delay)
        print("BEGIN (erasing)...")
        c.check(P.CMD_BEGIN, struct.pack("<II", total, crc))
        print("erased; the application is ALREADY marked invalid from this point.\n")

        # hold_at removes the timing problem: write up to that percentage, then
        # keep the transaction open indefinitely so the operator can cut power
        # whenever. Tests the "nearly complete image, no metadata" state, which
        # is the one a well-timed cut is hardest to hit by hand.
        stop_at = int(chunks * hold_at / 100) if hold_at is not None else None
        target = int(chunks * at_pct / 100)
        warned = False
        sent = 0
        try:
            for i in range(chunks):
                if i == target and not warned:
                    warned = True
                    print("\n" + "=" * 58)
                    print(f"   CUT POWER NOW  (at {at_pct}% - flip the switch off)")
                    print("=" * 58 + "\n")
                c.check(P.CMD_WRITE, struct.pack("<I", sent) + IMAGE[sent:sent + step])
                sent += step
                pct = 100 * sent // total
                print(f"\r  {pct:3d}%  {sent}/{total} B", end="", flush=True)
                if stop_at is not None and i + 1 >= stop_at:
                    print(f"\n\nholding at {pct}% with the transaction OPEN.")
                    print("cut power whenever you are ready.")
                    deadline = time.monotonic() + 300
                    while time.monotonic() < deadline:
                        c.check(P.CMD_STATUS)   # keeps the link live; raises when power goes
                        time.sleep(0.5)
                    print("timed out waiting for a power cut")
                    c.check(P.CMD_ABORT)
                    return 1

                time.sleep(delay)
        except Exception as e:
            print(f"\n\ndevice went away at {100*sent//total}% ({sent} B): "
                  f"{type(e).__name__}")
            print("power the board back ON, then run:  "
                  "python test/hil/test_powercut.py verify")
            return 0

        print("\n\nupload completed without interruption - power was not cut.")
        print("aborting so no dummy image is left marked valid.")
        c.check(P.CMD_ABORT)
        return 1


def verify() -> int:
    ports = find_ports()
    print(f"ports: {ports or '(none)'}")
    if not ports:
        print("FAIL: no CHGame device. The bootloader should have enumerated on")
        print("      its own, because the application is invalid.")
        return 1

    with Client(ports[0], timeout=5.0) as c:
        h = c.hello()
        ok = True

        in_boot = h.mode == P.MODE_BOOTLOADER
        print(f"  {'PASS' if in_boot else 'FAIL'}  came back in the BOOTLOADER "
              f"without touching BOOT")
        ok &= in_boot

        invalid = h.app_state != 0
        print(f"  {'PASS' if invalid else 'FAIL'}  application reported INVALID "
              f"({P.APP_STATE_NAMES.get(h.app_state)})")
        ok &= invalid

        st = c.request(P.CMD_RUN)
        refused = st[0] != P.ST_OK
        print(f"  {'PASS' if refused else 'FAIL'}  RUN refused "
              f"({P.STATUS_NAMES.get(st[0])}) - a partial image is never launched")
        ok &= refused

        # The bootloader itself must be untouched by an interrupted app update.
        boot = c.check(P.CMD_READ, struct.pack("<IH", 0, 8))[1:9]
        boot_ok = boot[:4] != b"\xff\xff\xff\xff"
        print(f"  {'PASS' if boot_ok else 'FAIL'}  bootloader region intact "
              f"({boot[:4].hex(' ')})")
        ok &= boot_ok

        # And it must still be able to accept a fresh upload.
        try:
            c.check(P.CMD_BEGIN, struct.pack("<II", 1024, 0))
            c.check(P.CMD_ABORT)
            print("  PASS  still accepts a new update transaction")
        except Exception as e:
            print(f"  FAIL  cannot start a new update: {type(e).__name__}")
            ok = False

    print("\nRECOVERY OK" if ok else "\nRECOVERY FAILED")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("arm")
    a.add_argument("--at", type=int, default=50, help="percent at which to prompt")
    a.add_argument("--seconds", type=float, default=25.0, help="pace the write phase")
    a.add_argument("--start-delay", type=float, default=0.0,
                   help="wait before starting to write, so the operator can get ready")
    a.add_argument("--hold-at", type=int, default=None,
                   help="write to this percent then hold the transaction open indefinitely")
    a.add_argument("--port")
    v = sub.add_parser("verify")
    v.add_argument("--port")
    args = ap.parse_args()

    if args.cmd == "verify":
        return verify()

    port = args.port or (find_ports() or [None])[0]
    if not port:
        sys.exit("no CHGame device found")
    return arm(port, args.at, args.seconds, args.start_delay, args.hold_at)


if __name__ == "__main__":
    raise SystemExit(main())
