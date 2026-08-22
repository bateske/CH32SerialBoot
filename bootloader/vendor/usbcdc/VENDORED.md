# Vendored: CH32X035_USBSerial (internal CDC layer)

Source: https://github.com/jobitjoseph/CH32X035_USBSerial
Commit: a8894928ed81aaf564d2b165d5022e6eda0eac6d (2025-10-30)
License: MIT â€” see LICENSE (Copyright (c) 2025 Jobit Joseph)

Only `src/internal/` is vendored. That layer is plain C over the CH32X035 USBFS
registers with no Arduino dependency, which is what the standalone bootloader
needs. The Arduino-facing wrapper (`CH32X035_USBSerial.{h,cpp}`) is vendored
separately under `arduino/CHGame/libraries/`.

## Local modifications

Any change to these files MUST be listed here.

| File | Change | Why |
|---|---|---|
| `wch_usbcdc_cdc.c` | `CDC_write()` flushes only on a full 64-byte packet instead of after every byte | Upstream capped throughput near 1 KB/s; a 55 KB upload would have taken ~1 minute |
| `wch_usbcdc_cdc.c` | added `CDC_write_nb()` / `CDC_read_nb()` | The bootloader protocol loop must not be wedged by a host that stopped reading |
| `wch_usbcdc_cdc.c` | added `CDC_enumerated()` / `CDC_dtr()` / `CDC_baud()` accessors | Needed for mode detection and the 1200-baud upload handshake |
| `wch_usbcdc_internal.h` | declared the above | |
| `wch_usbcdc_config.h` | **contents replaced** by a forwarder to `shared/chgame_usb_identity.h` | One identity shared by bootloader and application. An -I shadow does NOT work here — quoted includes search the including file's own directory first, so the vendor copy always won and the change silently did nothing |

## Known issues in upstream that we must address

All three are now addressed — see the modification table above. Kept here as a
record of what to re-check when re-syncing with upstream.

1. ~~`CDC_write()` flushed after every byte~~ — fixed.
2. ~~`CDC_read()`/`CDC_write()` spin-wait~~ — non-blocking variants added; the
   originals are left intact for the Arduino-facing wrapper.
3. ~~Identity strings were Jobit's defaults~~ — replaced wholesale by the shared
   header.
