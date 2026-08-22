"""Serial transport for the CHGame bootloader protocol."""
from __future__ import annotations

import struct
import time

import serial
import serial.tools.list_ports

from .protocol import (
    SOF, CMD_HELLO, CMD_RUN, CMD_STATUS, ST_OK, Hello,
    ProtocolError, StatusError, build_frame, crc16,
)

# Development identifiers. See shared/wch_usbcdc_config.h — these are the shared
# V-USB CDC pair and MUST be replaced before shipping.
CHGAME_VID = 0x16C0
CHGAME_PID = 0x27DD


def find_ports(vid: int = CHGAME_VID, pid: int = CHGAME_PID) -> list[str]:
    """Candidate CHGame ports, most recently seen first."""
    return [p.device for p in serial.tools.list_ports.comports()
            if p.vid == vid and p.pid == pid]


def upload_touch(port: str, settle: float = 0.05) -> None:
    """Ask a running sketch to reboot into the bootloader (Arduino 1200-baud touch).

    Opening at 1200 baud sets the CDC line coding; dropping DTR is what actually
    fires it. The device acknowledges the control transfer, detaches, and resets
    - so the port disappearing here is success, not failure.
    """
    s = serial.Serial(port, 1200)
    try:
        s.dtr = False
        time.sleep(settle)
    finally:
        try:
            s.close()
        except Exception:
            pass


def wait_for_bootloader(timeout: float = 10.0, port_hint: str | None = None) -> str:
    """Wait for a device answering HELLO in bootloader mode; return its port.

    Because the application and the bootloader present identical USB descriptors,
    this is usually the SAME port the sketch was on - which is the whole point of
    that choice. It is still discovered rather than assumed, so a host that does
    reassign the port still works.
    """
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        candidates = find_ports()
        if port_hint and port_hint in candidates:
            candidates = [port_hint] + [p for p in candidates if p != port_hint]
        for cand in candidates:
            try:
                with Client(cand, timeout=1.0) as c:
                    if c.hello().mode == 1:
                        return cand
            except Exception as e:      # not up yet, or still the application
                last = e
        time.sleep(0.2)
    raise TimeoutError(f"no bootloader appeared within {timeout:.0f}s ({last})")


def ensure_bootloader(port: str, timeout: float = 10.0) -> str:
    """Return a port in bootloader mode, performing the touch if needed."""
    try:
        with Client(port, timeout=1.5) as c:
            if c.hello().mode == 1:
                return port
    except Exception:
        pass                            # not answering as either mode; try the touch

    upload_touch(port)
    return wait_for_bootloader(timeout, port_hint=port)


class Client:
    """Framed request/response over a CDC serial port.

    Baud rate is irrelevant over USB CDC — the peripheral ignores it — except
    for 1200, which is reserved as the upload-request signal.
    """

    def __init__(self, port: str, timeout: float = 2.0, debug: bool = False):
        self.port_name = port
        self.timeout = timeout
        self.debug = debug
        self.ser = serial.Serial(port, 115200, timeout=timeout, write_timeout=timeout)
        self.ser.reset_input_buffer()

    def close(self) -> None:
        try:
            self.ser.close()
        except Exception:
            pass

    def __enter__(self): return self
    def __exit__(self, *a): self.close()

    def _read_exact(self, n: int, deadline: float) -> bytes:
        buf = b""
        while len(buf) < n:
            if time.monotonic() > deadline:
                raise TimeoutError(f"timed out after {len(buf)} of {n} bytes")
            chunk = self.ser.read(n - len(buf))
            if chunk:
                buf += chunk
        return buf

    def _read_frame(self, deadline: float) -> tuple[int, bytes]:
        # Hunt for the two-byte start marker, discarding anything before it, so
        # stale bytes from a previous session cannot desynchronise us.
        window = b""
        while True:
            if time.monotonic() > deadline:
                raise TimeoutError("timed out waiting for a frame")
            b = self.ser.read(1)
            if not b:
                continue
            window = (window + b)[-2:]
            if window == SOF:
                break

        head = self._read_exact(4, deadline)
        ver, cmd, length = struct.unpack("<BBH", head)
        payload = self._read_exact(length, deadline) if length else b""
        got = struct.unpack("<H", self._read_exact(2, deadline))[0]

        want = crc16(head + payload)
        if got != want:
            raise ProtocolError(f"frame CRC 0x{got:04X}, computed 0x{want:04X}")
        if self.debug:
            print(f"  <- cmd 0x{cmd:02X} len {length} {payload.hex(' ')}")
        return cmd & 0x7F, payload

    def request(self, cmd: int, payload: bytes = b"", timeout: float | None = None):
        frame = build_frame(cmd, payload)
        if self.debug:
            print(f"  -> cmd 0x{cmd:02X} len {len(payload)} {frame.hex(' ')}")
        self.ser.write(frame)
        self.ser.flush()

        deadline = time.monotonic() + (timeout if timeout is not None else self.timeout)
        while True:
            rcmd, rpayload = self._read_frame(deadline)
            if rcmd == cmd:
                return rpayload
            # Ignore responses to a command we did not send (a late reply to an
            # earlier, timed-out request) rather than failing the current one.
            if self.debug:
                print(f"  (ignoring stale response to 0x{rcmd:02X})")

    def check(self, cmd: int, payload: bytes = b"") -> bytes:
        r = self.request(cmd, payload)
        if not r or r[0] != ST_OK:
            raise StatusError(cmd, r[0] if r else 0xFF)
        return r

    def hello(self) -> Hello:
        return Hello.parse(self.request(CMD_HELLO))

    def status(self) -> bytes:
        return self.check(CMD_STATUS)

    def run(self) -> None:
        """Ask the bootloader to launch the application. The port disappears."""
        try:
            self.check(CMD_RUN)
        except (TimeoutError, serial.SerialException):
            pass  # device may vanish before the ack lands
