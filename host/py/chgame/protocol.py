"""CHGame bootloader wire protocol — host side.

Mirrors bootloader/src/proto.{h,c}. docs/protocol.md is normative; the shared
vectors in test/protocol/vectors.json keep this, the C bootloader and the Web
Serial implementation from drifting apart.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

SOF = b"CG"
VERSION = 1
MAX_PAYLOAD = 512  # mirrors PROTO_MAX_PAYLOAD in proto.h
RESPONSE_BIT = 0x80

CMD_HELLO = 0x01
CMD_BEGIN = 0x02
CMD_WRITE = 0x03
CMD_END = 0x04
CMD_RUN = 0x05
CMD_STATUS = 0x06
CMD_ABORT = 0x07
CMD_READ = 0x08
CMD_DEV_UNLOCK = 0x40
CMD_DEV_WRITE_BOOT = 0x41

ST_OK = 0x00
STATUS_NAMES = {
    0x00: "OK",
    0x01: "ERR_BADCMD",
    0x02: "ERR_STATE",
    0x03: "ERR_RANGE",
    0x04: "ERR_SIZE",
    0x05: "ERR_CRC",
    0x06: "ERR_FLASH",
    0x07: "ERR_LOCKED",
    0x08: "ERR_FRAME",
    0x09: "ERR_NOTIMPL",
}

MODE_BOOTLOADER = 1
MODE_APPLICATION = 2
MODE_NAMES = {1: "bootloader", 2: "application"}

APP_STATE_NAMES = {0: "valid", 1: "no metadata", 2: "bad length", 3: "bad CRC"}


class ProtocolError(Exception):
    pass


class StatusError(ProtocolError):
    def __init__(self, cmd: int, status: int):
        self.cmd, self.status = cmd, status
        super().__init__(
            f"command 0x{cmd:02X} returned {STATUS_NAMES.get(status, hex(status))}"
        )


def crc16(data: bytes, crc: int = 0xFFFF) -> int:
    """CRC-16/CCITT-FALSE."""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def build_frame(cmd: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload {len(payload)} exceeds {MAX_PAYLOAD}")
    body = struct.pack("<BBH", VERSION, cmd, len(payload)) + payload
    return SOF + body + struct.pack("<H", crc16(body))


def parse_frame(buf: bytes) -> tuple[int, bytes]:
    """Parse one complete frame. Returns (cmd_without_response_bit, payload)."""
    if len(buf) < 8 or not buf.startswith(SOF):
        raise ProtocolError(f"not a frame: {buf[:16]!r}")
    ver, cmd, length = struct.unpack_from("<BBH", buf, 2)
    if ver != VERSION:
        raise ProtocolError(f"protocol version {ver}, expected {VERSION}")
    end = 6 + length
    body, got = buf[2:end], struct.unpack_from("<H", buf, end)[0]
    want = crc16(body)
    if got != want:
        raise ProtocolError(f"frame CRC 0x{got:04X}, computed 0x{want:04X}")
    return cmd & ~RESPONSE_BIT, buf[6:end]


@dataclass
class Hello:
    proto_version: int
    mode: int
    app_state: int
    boot_version: int
    app_start: int
    app_max_size: int
    page_size: int
    max_payload: int
    uid: bytes

    @classmethod
    def parse(cls, payload: bytes) -> "Hello":
        status = payload[0]
        if status != ST_OK:
            raise StatusError(CMD_HELLO, status)
        (pv, mode, app_state, bootver, app_start, app_max,
         page, maxpl) = struct.unpack_from("<BBBHIIHH", payload, 1)
        return cls(pv, mode, app_state, bootver, app_start, app_max,
                   page, maxpl, payload[18:30])

    def describe(self) -> str:
        return (
            f"mode          : {MODE_NAMES.get(self.mode, self.mode)}\n"
            f"protocol      : v{self.proto_version}\n"
            f"bootloader    : v{self.boot_version}\n"
            f"application   : {APP_STATE_NAMES.get(self.app_state, self.app_state)}\n"
            f"app region    : 0x{self.app_start:04X} .. 0x{self.app_start + self.app_max_size:04X}"
            f"  ({self.app_max_size} bytes)\n"
            f"flash page    : {self.page_size} bytes\n"
            f"max payload   : {self.max_payload} bytes\n"
            f"chip UID      : {self.uid.hex().upper()}"
        )
