# USB CDC — part of the core, not a library

Derived from [jobitjoseph/CH32X035_USBSerial](https://github.com/jobitjoseph/CH32X035_USBSerial)
(MIT, see `LICENSE.usbcdc`), commit `a889492`.

This lives in the **core** rather than in `libraries/` on purpose. `main.cpp`
starts USB CDC before `setup()`, and Arduino only compiles a library that a
sketch `#include`s — so as a library, an empty sketch would fail to link and,
worse, a sketch that simply never mentioned `Serial` would come up with no USB
port and no way to be re-flashed except the BOOT button.

`internal/wch_usbcdc_*.c` is byte-identical to the copy the bootloader builds
from (`bootloader/vendor/usbcdc/`) apart from the 1200-baud upload handshake,
which only makes sense in the application. Keeping them in step is what makes
the two modes present identical USB descriptors, and therefore keep one stable
COM port across an upload.
