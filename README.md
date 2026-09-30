# CHGame

Arduino board support for the **CHGame** handheld console (WCH CH32X035G8U6),
with a USB CDC bootloader so uploads work like an Arduino Leonardo: plug in,
pick the port, press Upload.

**No driver installation. No Zadig. No WinUSB. No BOOT button. No power cycle.**

The board appears as an ordinary serial port while your sketch runs. Pressing
Upload reboots it into its own bootloader, flashes, verifies and restarts — on
the same COM port throughout, in about half a second.

```
Board: CHGame
Port:  COM8

[Upload]

Sketch uses 11260 bytes (22%) of program storage space.
port    : COM8
erase   : 0.14 s
write   : 0.38 s  (29.6 KiB/s)
total   : 0.56 s  -- image accepted and marked valid
running : application is up on COM8
```

---

## Installing

In Arduino IDE, add this to **Preferences → Additional Boards Manager URLs**:

```
https://github.com/bateske/CH32SerialBoot/releases/latest/download/package_chgame_index.json
```

Then **Tools → Board → Boards Manager**, search for *CHGame*, install.

That is the only thing to install. The toolchain, the uploader and `wchisp` all
arrive as dependencies of the board package.

New versions show up in Boards Manager as an update to the installed package.
What changed in each one is in [CHANGELOG.md](CHANGELOG.md).

> **Status: development preview.** The USB VID/PID are shared test identifiers
> (`16C0:27DD`) and **must** be replaced with a real allocation before any public
> release. See [Before shipping](#before-shipping).

## Using it

Select **CHGame** as the board, pick the port, press Upload. `Serial` is the
native USB CDC port, exactly like a Leonardo:

```cpp
void setup() {
  Serial.begin(115200);          // baud is ignored over USB
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_BTN_A, INPUT_PULLUP);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH); delay(100);
  digitalWrite(LED_BUILTIN, LOW);  delay(900);
  Serial.println(digitalRead(PIN_BTN_A) ? "up" : "DOWN");
}
```

Board pin names: `LED_BUILTIN`, `PIN_BUZZER`, `PIN_BTN_UP/DOWN/LEFT/RIGHT/A/B/SELECT/START`,
`PIN_LCD_CS/DC/RST`, `PIN_SD_CS`, `PIN_GPIO1..4`. Buttons are active-low — use
`INPUT_PULLUP` and treat `LOW` as pressed. A physical UART is available on the
expansion header as `Serial1`.

An empty sketch stays uploadable. The upload handshake lives in the USB
interrupt, not in sketch code, so a sketch that never mentions `Serial` — or one
stuck in a blocking loop — can still be replaced.

### Peripherals menu

**Tools → Peripherals** chooses how much of the core is compiled in. The
default, **Game**, leaves out Serial1, `tone()`, `analogWrite()` PWM and
HardwareTimer, which saves about 4 KB of the 50,944 B a sketch can use. USB
Serial, SPI, Wire, `analogRead()`, `attachInterrupt()` and CHGameSound all work
as normal. Pick **Full** if a sketch needs Serial1 on the H1 header, `tone()`,
PWM or HardwareTimer. From the CLI the setting is `periph=game` or
`periph=full` in the FQBN.

### Optimize menu

**Tools → Optimize → Smallest + LTO** adds link-time optimisation to `-Os`: the
core, the libraries and the sketch are optimised together as one program. It
is usually 1–5 KB smaller than plain `-Os` (CHChess fits only with it, and
CHBlackjack goes from 50,048 B to 47,528 B). Plain `-Os` stays the default
because links are slower and the `.map` file is harder to read. From the CLI
the setting is `opt=oslto` in the FQBN.

### USB menu

**Tools → USB → Upload only** removes `Serial` from the build, about 0.6–0.7 KB
of flash, for a sketch that needs the room more than it needs to print. The
board still shows up as a COM port and Upload still works with no button
presses, even if the sketch is stuck in `setup()`: uploading never used the
sketch's serial data, only the USB connection and the 1200-baud handshake, and
both stay. A sketch that uses `Serial` in this mode fails to build, on purpose,
so nothing that depends on it silently stops working. **Serial** stays the
default. From the CLI the setting is `usb=serial` or `usb=uploadonly` in the
FQBN.

### Memory report

The RAM figure after a build is measured against 18,416 B: the 20 KB SRAM less
the 16 B boot block and the 2 KB stack, which the linker reserves separately
at the top. So "leaving N bytes for local variables" is really what is left
for the heap (`malloc`, `String`); function locals live in the 2 KB stack,
which is not counted. The "Low memory available" warning appears once less
than about 900 B is left.

To add compiler flags from the command line without editing the board files,
use `--build-property build.extra_flags=...` with `arduino-cli compile`.

### Startup

Your sketch starts within a few milliseconds of power-on. The bootloader's only
work on a normal boot is a CRC-32 over the image and the jump, and bringing up
USB does not block: enumeration completes in the USB interrupt while `setup()`
is already running, so a game can be initialising its display and drawing frames
before the COM port appears on the host.

`Serial` is therefore usable, but not yet connected, during early `setup()`.
Writes to a port no host has enumerated are **discarded, never blocked** — a
board on batteries with no USB attached runs at full speed instead of stalling
on its first `println`. Writes to a host that has stopped reading are dropped
after a short timeout for the same reason, so a closed serial monitor can never
stall a game loop.

If a sketch genuinely needs the port before it continues, ask for it:

```cpp
void setup() {
  Serial.begin(115200);
  while (!Serial);            // or Serial.waitForPC(3000) to give up after 3 s
  Serial.println("a terminal is listening");
}
```

`Serial.enumerated()` reports whether the host has configured the device, and
`Serial.waitForPC(ms)` waits for a terminal to actually open the port. Sketches
that would rather stall than lose output can build with
`-DCHGAME_USB_TX_TIMEOUT_MS=n`.

## First flash of a blank board

A brand-new board has no bootloader, so it needs one pass through the chip's
factory ISP. This is the **only** time the BOOT button is used.

1. **Tools → Board →** CHGame
2. **Tools → Programmer →** *WCH factory ISP (hold BOOT, power cycle)*
3. Hold **BOOT**, switch power **off** then **on** while still holding, release
4. **Tools → Burn Bootloader**

The board then comes up in its bootloader, enumerates as a COM port, and the
first sketch goes on with a normal Upload. BOOT is not needed again.

**Sketch → Upload Using Programmer** writes the bootloader *and* the current
sketch in one ISP pass — the recovery path for a board whose bootloader was
damaged.

> On Windows the factory ISP is a vendor-class device and needs WinUSB bound to
> it (via Zadig). **This applies only to the first flash and to recovery.** The
> bootloader and your sketches are standard USB CDC, driven by the inbox driver
> on every OS, so an end user with a provisioned board installs nothing.

## How it works

Three layers, described fully in [docs/boot-flow.md](docs/boot-flow.md):

```
 0x0000  CHGame bootloader        12 KB   USB CDC, protocol, flash writer
 0x3000  Your sketch           50944 B    linked here, not at flash origin
 0xF700  Application metadata     256 B   magic, length, CRC-32 -- written LAST
```

On reset the bootloader checks a retained RAM marker, then validates the
application by CRC-32 against its metadata page. It launches the sketch only if
that check passes, so an interrupted update can never produce a half-written
image that looks launchable. Verified with real power cuts at 1%, 3% and 94%
through an upload.

Pressing Upload triggers an **Arduino 1200-baud touch**: the uploader opens the
port at 1200 baud and drops DTR, the USB interrupt recognises that as an upload
request, sets a magic value in retained RAM and resets.

The application and the bootloader present **identical USB descriptors** on
purpose. A Leonardo uses different PIDs for the two, so they get different COM
port numbers and the IDE has to guess which transient port to grab — the classic
source of Leonardo upload flakiness. Identical descriptors mean one stable port
the whole way through. The cost is that the two modes are indistinguishable from
the descriptors alone, so mode is discovered by protocol probe instead.

## Documentation

| Document | Contents |
|---|---|
| [docs/protocol.md](docs/protocol.md) | Wire protocol — normative |
| [docs/boot-flow.md](docs/boot-flow.md) | Boot decision, handover, the privilege-mode trap |
| [docs/memory-map.md](docs/memory-map.md) | Flash and RAM layout |
| [docs/hardware-pinmap.md](docs/hardware-pinmap.md) | QFN28 pin assignments and how they were derived |
| [docs/recovery.md](docs/recovery.md) | Factory ISP recovery |
| [docs/ch32x035-gotchas.md](docs/ch32x035-gotchas.md) | Five hardware traps that cost a day each |
| [docs/building.md](docs/building.md) | Building and releasing from source |

If you are doing anything with a CH32X035 bootloader, **read the gotchas
document first**. Every entry has the same shape: the wrong behaviour looked like
success, or looked like somebody else's fault.

## Repository layout

```
bootloader/     Standalone bootloader firmware (C) and its vendored dependencies
arduino/CHGame/ Arduino platform: board definition, core, variant, linker script
host/go/        chgame-upload - the shipping uploader, one static binary per host
host/py/        Python reference implementation, used by the test suite
test/hil/       Hardware-in-the-loop tests: protocol safety, power cuts, soak
test/sketches/  Sketches the tests build and upload
tools/          Build, packaging and release scripts
shared/         USB identity shared by bootloader and application
docs/           Documentation
```

## Building from source

See [docs/building.md](docs/building.md). In short:

```bash
./tools/build_all.sh          # bootloader + test app + combined image
./host/go/build.sh            # uploader, all five host platforms
./tools/sync_platform.sh      # install the platform into Arduino15 for testing
```

## Testing

The hardware tests need a CHGame board attached.

```bash
python test/hil/test_protocol.py    # 27 protocol and flash-safety checks
python test/hil/test_soak.py --cycles 100
python test/hil/test_powercut.py arm --hold-at 95    # needs a human at the switch
```

`test_protocol.py` is the one that matters: it asserts the bootloader region is
byte-identical after every malformed frame, bad CRC, overrun attempt, aborted
transaction and locked developer command it can throw at the device.

## Before shipping

- [ ] **Replace the USB VID/PID.** `16C0:27DD` is the shared V-USB CDC pair — it
      collides with unrelated devices and port auto-discovery cannot rely on it.
      [pid.codes](https://pid.codes) issues free PIDs under VID `0x1209` for
      open-source projects. Change `shared/chgame_usb_identity.h` and the two
      constants in `host/go/client.go`.
- [ ] **Set `CHGAME_ALLOW_SELFUPDATE=0`** so the developer self-update commands
      are compiled out.
- [ ] **Mirror the wchisp binaries.** The index currently references upstream's
      GitHub release assets; if those move, installs break.
- [ ] Run the 100-cycle soak and the clean-machine test on macOS and Linux.

## Credits and licence

This project is MIT licensed — see [LICENSE](LICENSE).

The USB CDC implementation derives from
[jobitjoseph/CH32X035_USBSerial](https://github.com/jobitjoseph/CH32X035_USBSerial)
(MIT), and the Arduino core from
[jobitjoseph/CH32_Arduino_Core](https://github.com/jobitjoseph/CH32_Arduino_Core),
itself derived from [openwch/arduino_core_ch32](https://github.com/openwch/arduino_core_ch32).
Vendored copies record their origin commit and every local modification in
`VENDORED.md` alongside the source.

[wchisp](https://github.com/ch32-rs/wchisp) (GPL-2.0) is used for factory
provisioning and recovery. It is invoked as a separate process and is not linked
into anything here.

The Machine-mode handover requirement was independently confirmed by
[krzysztofgawrys/ch32x035-dfu-boot](https://github.com/krzysztofgawrys/ch32x035-dfu-boot),
which solves it a different way.
