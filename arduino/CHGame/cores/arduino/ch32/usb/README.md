# USB CDC — part of the core, not a library

Derived from [jobitjoseph/CH32X035_USBSerial](https://github.com/jobitjoseph/CH32X035_USBSerial)
(MIT, see `LICENSE.usbcdc`), commit `a889492`.

This lives in the **core** rather than in `libraries/` on purpose. `main.cpp`
starts USB CDC before `setup()`, and Arduino only compiles a library that a
sketch `#include`s — so as a library, an empty sketch would fail to link and,
worse, a sketch that simply never mentioned `Serial` would come up with no USB
port and no way to be re-flashed except the BOOT button.

`wch_usbcdc_*.c` match the copy the bootloader builds from
(`bootloader/vendor/usbcdc/`) apart from three application-only changes: the
1200-baud upload handshake, register-level pin and clock setup in `USB_init()`
(the vendor helpers cost ~400 B of the sketch's flash), and the build guards
for the **Tools → USB** menu. The descriptors are untouched, and keeping them in
step is what makes the two modes look identical to the host, and therefore
keep one stable COM port across an upload.

**Tools → USB → Upload only** (`USE_CHGAME_USB_BOOTONLY`) compiles the three
`wch_usbcdc_*.c` files but not `CHGameUSBSerial.cpp`. `main.cpp` calls
`CDC_init()` directly, so the board still enumerates and still honours the
upload handshake, both of which happen in the interrupt's control path. Bytes
the host sends are NAKed once the 64 B endpoint buffer is full, exactly as for
a sketch that never reads `Serial`, which the uploader already allows for.
`Serial` is deliberately a compile error in this mode (see `WSerial.h`).
