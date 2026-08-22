#!/usr/bin/env python3
"""chgame_upload — development uploader for the CHGame bootloader.

Phase 1 prototype (see docs). The shipping tool is a single static Go binary;
this exists to get the protocol right first, and it stays afterwards as the
reference implementation the test suite drives.

    python chgame_upload.py probe
    python chgame_upload.py info
    python chgame_upload.py flash firmware.bin [--verify] [--run]
    python chgame_upload.py run
    python chgame_upload.py selfupdate bootloader.bin
"""
from __future__ import annotations

import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from chgame.client import (Client, find_ports, ensure_bootloader,  # noqa: E402
                           upload_touch, wait_for_bootloader)
from chgame.protocol import MODE_NAMES, StatusError   # noqa: E402
from chgame import upload as up                       # noqa: E402
from chgame.provision import provision, WchispNotFound  # noqa: E402


def resolve_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    ports = find_ports()
    if not ports:
        sys.exit("no CHGame device found (looked for 16C0:27DD). Is it plugged in?")
    if len(ports) > 1:
        sys.exit(f"several CHGame devices found: {', '.join(ports)} — pass --port")
    return ports[0]


def cmd_probe(args) -> int:
    ports = find_ports()
    if not ports:
        print("no CHGame device found (16C0:27DD)")
        return 1
    for p in ports:
        try:
            with Client(p, timeout=args.timeout, debug=args.debug) as c:
                h = c.hello()
                print(f"{p}: {MODE_NAMES.get(h.mode, h.mode)}, "
                      f"protocol v{h.proto_version}, bootloader v{h.boot_version}")
        except Exception as e:
            print(f"{p}: present, but did not answer HELLO ({type(e).__name__}: {e})")
    return 0


def cmd_info(args) -> int:
    port = resolve_port(args.port)
    with Client(port, timeout=args.timeout, debug=args.debug) as c:
        print(f"port          : {port}")
        print(c.hello().describe())
    return 0


def cmd_provision(args) -> int:
    """Write the bootloader (and optionally a sketch) through the factory ISP.

    Invoked by Arduino's "Burn Bootloader" and "Upload Using Programmer".
    """
    try:
        provision(pathlib.Path(args.bootloader),
                  pathlib.Path(args.app) if args.app else None,
                  wchisp=args.wchisp)
    except WchispNotFound as e:
        sys.exit(str(e))
    except RuntimeError as e:
        sys.exit(str(e))
    return 0


def cmd_noop(args) -> int:
    """Deliberate no-op for Arduino's separate chip-erase step.

    Burn Bootloader runs erase.pattern before bootloader.pattern. Provisioning
    goes through wchisp, whose flash command already erases the whole code flash
    before writing and verifies afterwards, so a separate erase would just add a
    second full erase cycle for no benefit. The recipe still has to exist, so it
    exists and says why.
    """
    print("erase: skipped - provisioning erases the whole code flash as it writes")
    return 0


def cmd_touch(args) -> int:
    """Reboot a running sketch into the bootloader and report where it landed."""
    port = resolve_port(args.port)
    upload_touch(port)
    boot = wait_for_bootloader(timeout=max(args.timeout, 10.0), port_hint=port)
    print(f"bootloader is up on {boot}" + ("" if boot == port else f" (was {port})"))
    return 0


def cmd_run(args) -> int:
    port = resolve_port(args.port)
    with Client(port, timeout=args.timeout, debug=args.debug) as c:
        h = c.hello()
        if h.mode != 1:
            sys.exit(f"{port} is in {MODE_NAMES.get(h.mode, h.mode)} mode, not the bootloader")
        if h.app_state != 0:
            sys.exit("bootloader reports the application image is not valid; refusing to RUN")
        c.run()
    print("sent RUN — device should now be running the application")
    return 0



def _bar(done: int, total: int) -> None:
    width = 32
    filled = int(width * done / total) if total else width
    pct = 100 * done // total if total else 100
    print(f"\r  [{'#' * filled}{'.' * (width - filled)}] {pct:3d}%  {done}/{total} B",
          end="", flush=True)
    if done >= total:
        print()


def cmd_flash(args) -> int:
    image = pathlib.Path(args.image).read_bytes()
    port = resolve_port(args.port)

    # Erasing the whole application region can take a while; BEGIN must not be
    # judged by the ordinary per-command timeout.
    # Do the whole transition here rather than relying on the IDE. One actor
    # owning the port through app -> bootloader -> app is what makes this
    # reliable; two actors racing for it is the classic native-USB upload bug.
    boot_port = ensure_bootloader(port, timeout=max(args.timeout, 10.0))
    if boot_port != port:
        print(f"note    : bootloader appeared on {boot_port} (was {port})")
    port = boot_port

    with Client(port, timeout=max(args.timeout, 10.0), debug=args.debug) as c:
        h = c.hello()
        if h.mode != 1:
            sys.exit(f"{port} is in {MODE_NAMES.get(h.mode, h.mode)} mode, not the bootloader")

        print(f"port    : {port}")
        print(f"image   : {args.image}  ({len(image)} bytes)")
        print(f"region  : 0x{h.app_start:04X} + {h.app_max_size} bytes")
        if len(image) > h.app_max_size:
            sys.exit(f"image does not fit: {len(image)} > {h.app_max_size}")

        r = up.upload(c, image, progress=None if args.quiet else _bar,
                      verify_readback=args.verify)

        print(f"crc32   : 0x{r['crc32']:08X}")
        print(f"erase   : {r['erase_s']:.2f} s")
        print(f"write   : {r['write_s']:.2f} s  ({r['kbps']:.1f} KiB/s, "
              f"{r['chunk']} B chunks)")
        if args.verify:
            print(f"readback: {'MATCHES' if r['readback_ok'] else 'MISMATCH'}")
            if not r["readback_ok"]:
                return 1
        print(f"total   : {r['total_s']:.2f} s  -- image accepted and marked valid")

        if args.run:
            c.run()
            print("sent RUN")

    if args.run:
        # Confirm the sketch actually came back, so a silent failure to launch
        # is reported here rather than discovered later by the user.
        try:
            back = wait_for_application(timeout=6.0)
            print(f"running : application is up on {back}")
        except TimeoutError:
            print("warning : the application did not re-enumerate within 6s")
    return 0


def wait_for_application(timeout: float = 6.0) -> str:
    """Wait for the sketch to come back up.

    An application is identified by a CHGame port that does NOT answer HELLO.
    That is deliberate: sketches do not implement the bootloader protocol, and
    they must not. A sketch owns its Serial stream, so a protocol responder
    living in the core would eat bytes the sketch was meant to receive and
    inject frames into its output. The mode field exists for the bootloader to
    identify ITSELF; absence of a reply is what identifies the application.
    """
    import time as _t
    deadline = _t.monotonic() + timeout
    while _t.monotonic() < deadline:
        for cand in find_ports():
            try:
                with Client(cand, timeout=0.8) as c:
                    if c.hello().mode == 1:
                        continue            # still the bootloader
            except Exception:
                return cand                 # present but silent => the sketch
        _t.sleep(0.25)
    raise TimeoutError("application did not re-enumerate")


def cmd_selfupdate(args) -> int:
    image = pathlib.Path(args.image).read_bytes()
    port = resolve_port(args.port)

    print("This replaces the BOOTLOADER itself.")
    print("  - the installed application is destroyed (it is the staging area)")
    print("  - if power is lost during the promotion, recovery is the BOOT")
    print("    button and the factory ISP (docs/recovery.md)")
    if not args.yes:
        if input("proceed? [y/N] ").strip().lower() not in ("y", "yes"):
            return 1

    port = ensure_bootloader(port, timeout=max(args.timeout, 10.0))
    with Client(port, timeout=max(args.timeout, 10.0), debug=args.debug) as c:
        r = up.selfupdate(c, image, progress=None if args.quiet else _bar)
        print(f"staged and promoted {r['bytes']} bytes; device is resetting")
    return 0


def main() -> int:
    # Shared options are attached to BOTH the top level and every subcommand, so
    # they work on either side of the verb. Getting this wrong is a needless
    # papercut in a tool that gets typed hundreds of times during bring-up.
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--port")
    common.add_argument("--timeout", type=float, default=2.0)
    common.add_argument("--debug", action="store_true",
                        help="dump frames in both directions")
    # Arduino's platform.txt expands {upload.verbose} to one of these; accept
    # both so the same recipe works at either verbosity.
    common.add_argument("--verbose", action="store_true", help="more detail")
    common.add_argument("--quiet", action="store_true", help="less detail")

    ap = argparse.ArgumentParser(description=__doc__, parents=[common],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("probe", parents=[common], help="list CHGame ports and what mode each is in")
    sub.add_parser("info", parents=[common], help="full HELLO report from one device")
    sub.add_parser("run", parents=[common], help="tell the bootloader to launch the application")
    sub.add_parser("noop", parents=[common],
                   help="no-op, satisfies Arduino's separate erase step")
    sub.add_parser("touch", parents=[common],
                   help="1200-baud touch: reboot a running sketch into the bootloader")

    pr = sub.add_parser("provision", parents=[common],
                        help="first flash / recovery through the factory ISP (needs BOOT)")
    pr.add_argument("--bootloader", required=True)
    pr.add_argument("--app", help="optionally write a sketch at the same time")
    pr.add_argument("--wchisp", help="path to wchisp (else CHGAME_WCHISP or PATH)")

    f = sub.add_parser("flash", parents=[common], help="upload an application image")
    f.add_argument("image")
    f.add_argument("--verify", action="store_true",
                   help="read the region back and compare, independently of the device CRC")
    f.add_argument("--run", action="store_true", help="launch the application afterwards")

    s = sub.add_parser("selfupdate", parents=[common],
                       help="replace the bootloader itself (developer command)")
    s.add_argument("image")
    s.add_argument("--yes", action="store_true", help="skip the confirmation prompt")
    args = ap.parse_args()

    try:
        return {"probe": cmd_probe, "info": cmd_info, "run": cmd_run,
                "touch": cmd_touch, "flash": cmd_flash, "noop": cmd_noop,
                "provision": cmd_provision,
                "selfupdate": cmd_selfupdate}[args.cmd](args)
    except StatusError as e:
        sys.exit(f"device refused: {e}")
    except TimeoutError as e:
        sys.exit(f"no response: {e}")


if __name__ == "__main__":
    raise SystemExit(main())
