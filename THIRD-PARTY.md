# Third-party components

| Component | Licence | How it is used |
|---|---|---|
| [CH32X035_USBSerial](https://github.com/jobitjoseph/CH32X035_USBSerial) | MIT | USB CDC implementation, vendored into the bootloader and the core |
| [CH32_Arduino_Core](https://github.com/jobitjoseph/CH32_Arduino_Core) | MIT / WCH terms | The Arduino core this platform forks |
| [openwch/arduino_core_ch32](https://github.com/openwch/arduino_core_ch32) | WCH terms | Upstream of the above; SPL peripheral sources and startup |
| WCH CH32X035 SPL and startup code | WCH terms | Vendored under `bootloader/vendor/spl` |
| [wchisp](https://github.com/ch32-rs/wchisp) | **GPL-2.0** | Invoked as a separate process for factory provisioning and recovery |
| [go.bug.st/serial](https://github.com/bugst/go-serial) | BSD-3-Clause | Serial transport in the Go uploader |

## Notes on wchisp and GPL-2.0

`wchisp` is executed as a separate process and is not linked into any binary
here, so nothing in this repository becomes a derivative work of it.

The Boards Manager index currently **references** upstream's GitHub release
assets rather than re-hosting them, so this project does not redistribute
wchisp. If those assets are mirrored for availability — which is advisable, since
an upstream change breaks installs — that becomes redistribution, and the
GPL-2.0 obligations apply: ship the licence text and a written offer for the
corresponding source.

## Vendored code

Vendored copies are not silent forks. Each directory carries a `VENDORED.md`
recording the upstream URL, the exact commit, and a table of every local
modification with the reason for it. The CH32X035 USB CDC layer is the main one:
see `bootloader/vendor/usbcdc/VENDORED.md`.

WCH's own SPL and startup sources carry WCH's licence header, which permits use
with WCH microcontrollers. That is the intended use here.
