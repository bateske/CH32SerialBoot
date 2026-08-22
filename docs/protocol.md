# CHGame bootloader protocol

Normative description of the wire format spoken over USB CDC between a host and
the CHGame bootloader. Three implementations must agree:

| Implementation | Location |
|---|---|
| Device (C) | `bootloader/src/proto.{h,c}` |
| Host (Go) — shipping | `host/go/protocol.go` |
| Host (Python) — reference and tests | `host/py/chgame/protocol.py` |

A browser implementation for the Web Serial updater will be a fourth. The whole
point of a small binary protocol over plain CDC is that all four can be the same
protocol: CDC is driven by an inbox class driver on every OS, so nothing here
requires a driver install, WinUSB, WebUSB or a helper application.

## Frame

```
+------+------+-----+-----+----------+---------------+--------+
| 0x43 | 0x47 | ver | cmd | len (LE) | payload[len]  | crc16  |
| 'C'  | 'G'  |  1  |     |  u16     |               |  u16   |
+------+------+-----+-----+----------+---------------+--------+
                \_______________ CRC-16/CCITT-FALSE _______/
```

- **CRC-16/CCITT-FALSE**: poly `0x1021`, init `0xFFFF`, no reflection, no final
  XOR. Computed over `ver`, `cmd`, `len` and `payload` — not over the start
  marker. Check value for `"123456789"` is `0x29B1`.
- **Maximum payload**: 512 bytes. A `WRITE` therefore carries a whole 256-byte
  flash page plus its 4-byte offset, so the device commits exactly one page per
  frame.
- Frames span several 64-byte USB packets. That is ordinary USB and works; see
  gotcha 3 in `ch32x035-gotchas.md` for why this sentence is here.

Responses echo the command with **bit 7 set** and carry a status code as
`payload[0]`.

### Framing errors

A frame that fails CRC is answered with `ERR_FRAME`. The receiver then resynchronises
by scanning for the next `'C' 'G'`.

A frame that stops arriving part-way through is abandoned after
**250 ms** (`PROTO_RX_TIMEOUT_MS`) and the parser returns to hunting for a start
marker. Without that timeout a truncated frame silently consumes the first bytes
of the next one — its length field absorbs the `'C'` — so a host that died
mid-frame poisons the first command of the next session.

## Commands

| Code | Name | Payload | Response payload |
|---|---|---|---|
| `0x01` | `HELLO` | — | status, identification (below) |
| `0x02` | `BEGIN` | `size u32`, `crc32 u32` | status |
| `0x03` | `WRITE` | `offset u32`, `data[]` | status |
| `0x04` | `END` | — | status |
| `0x05` | `RUN` | — | status, then the device launches the app |
| `0x06` | `STATUS` | — | status, progress, diagnostics |
| `0x07` | `ABORT` | — | status |
| `0x08` | `READ` | `addr u32`, `len u16` | status, `data[len]` |
| `0x40` | `DEV_UNLOCK` | `key u32` | status |
| `0x41` | `DEV_WRITE_BOOT` | `size u32`, `crc32 u32` | status, then resets |

### Status codes

| Code | Name | Meaning |
|---|---|---|
| `0x00` | `OK` | |
| `0x01` | `ERR_BADCMD` | unknown command |
| `0x02` | `ERR_STATE` | not valid in the current state (e.g. `WRITE` before `BEGIN`, or a non-sequential offset) |
| `0x03` | `ERR_RANGE` | address or length outside the permitted region |
| `0x04` | `ERR_SIZE` | size rejected (zero, unaligned, too large, or a malformed payload) |
| `0x05` | `ERR_CRC` | image CRC mismatch |
| `0x06` | `ERR_FLASH` | erase or program failed, including a failed read-back |
| `0x07` | `ERR_LOCKED` | developer command without a valid unlock |
| `0x08` | `ERR_FRAME` | frame CRC mismatch |
| `0x09` | `ERR_NOTIMPL` | command recognised but not implemented |

### HELLO response

After the status byte:

| Offset | Type | Field |
|---|---|---|
| 1 | u8 | protocol version |
| 2 | u8 | mode: 1 = bootloader, 2 = application |
| 3 | u8 | application state: 0 valid, 1 no metadata, 2 bad length, 3 bad CRC |
| 4 | u16 | bootloader version |
| 6 | u32 | `APP_START` |
| 10 | u32 | `APP_MAX_SIZE` |
| 14 | u16 | flash page size |
| 16 | u16 | maximum payload |
| 18 | 12 B | chip unique ID |

A host must take the region and page size from `HELLO` rather than assuming
them. That is what lets the bootloader reservation change without every tool
needing an update.

**Mode 2 is never sent.** Applications deliberately do not implement this
protocol: a sketch owns its `Serial` stream, so a responder living in the core
would eat bytes the sketch was meant to receive and inject frames into its
output. An application is identified by being **present but silent** — a CHGame
port that does not answer `HELLO`.

## Upload sequence

```
HOST                                    BOOTLOADER

HELLO ------------------------------->
      <------------------------------- OK, region, page size, max payload

BEGIN size,crc32 -------------------->  erase metadata page FIRST,
                                        then erase the application pages
      <------------------------------- OK

WRITE offset,data -------------------->  stage, commit whole pages,
      <------------------------------- OK   read back and verify each page
   ... repeated, offsets strictly sequential ...

END ---------------------------------->  recompute CRC-32 FROM FLASH,
      <------------------------------- OK   then write the metadata page

RUN ---------------------------------->  ack, detach USB, jump to the app
```

Three properties are worth calling out because they are what make an interrupted
update safe:

1. **`BEGIN` erases the metadata page before anything else.** From that instant
   the application is marked invalid, so a power loss anywhere in the rest of the
   update leaves the bootloader in charge rather than a half-written image that
   still looks launchable.
2. **`END` computes the CRC from flash**, not from anything the device buffered.
   The only claim worth making is about what is actually stored.
3. **Validity comes from the metadata record and nothing else.** A 95%-complete
   image is refused exactly as firmly as an empty one. Verified by real power
   cuts — see the appendix in `ch32x035-gotchas.md`.

`WRITE` offsets must be strictly sequential. That is not a limitation worth
relaxing: it removes any need to track which parts of the region have been
written, makes "bytes accepted" an exact resume point, and catches a host bug
that skips or repeats a chunk immediately rather than producing an image that
fails CRC for no visible reason.

## Bounds checking

Every address computation is validated as `offset + len <= size` **and**
`APP_START + offset + len <= APP_END`, in unsigned arithmetic with an explicit
wrap check, before any flash is touched.

The checks are duplicated at two levels on purpose. The protocol layer checks,
and `flash.c` checks again at the point of no return — a caller bug must not be
able to erase the bootloader. `test/hil/test_protocol.py` asserts the boot region
is byte-identical after every malformed frame, bad CRC, overrun attempt, aborted
transaction and locked developer command it can throw at the device.

## Developer commands

`DEV_UNLOCK` and `DEV_WRITE_BOOT` let the bootloader replace itself over USB,
which turns a bootloader iteration from a physical BOOT-button-plus-power-cycle
into a single command. The new image is first staged through the **ordinary**
upload path into the application region — so it gets the same bounds checking,
per-page verification and CRC as any firmware — and only then promoted.

The unlock key is an interlock against **accidents**, not a security control.
It is plainly visible in the binary to anyone who cares to look, and that is
fine: the threat being managed is "an upload tool bug bricks the bootloader",
not "an attacker with physical USB access". Do not describe it as protection.

The whole facility compiles out when `CHGAME_ALLOW_SELFUPDATE` is 0, which is
what a shipping build should do.

There is an unavoidable window, roughly the erase-and-program time of the boot
region, in which a power loss leaves the device with no usable bootloader. That
is recoverable exactly one way: the BOOT button and the factory ISP. It is why
that path must never be compromised.
