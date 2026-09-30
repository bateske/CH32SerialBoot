# Changelog

The version is the `version=` line in `arduino/CHGame/platform.txt`. That is the
number Boards Manager compares with what a user has installed, so every release
bumps it. `tools/release.sh <version> bateske/CH32SerialBoot` publishes the
release and uses the matching section of this file as the GitHub release notes.

## 0.2.4 (2026-09-30)

### Added

- **USB → Upload only** board menu option (`usb=uploadonly`): compiles out
  `Serial` (the USB serial class, Stream/Print and the CDC data path) but
  keeps the USB device and the 1200-baud upload handshake, so the COM port
  still appears and Upload still works with no button presses. About
  0.6–0.7 KB smaller: an empty sketch 3,968 → 3,288 B, CHChess (LTO)
  49,444 → 48,824 B. Using `Serial` in this mode is a compile error naming the
  menu setting, rather than a stub that silently prints nothing, and it no
  longer falls back to `Serial1` with Peripherals → Full. **Serial** is the
  default, and FQBNs without `usb=` build exactly as before. Proposed from
  HypeRunner, which is close to the flash limit. Tested on the board with
  `-Os` and LTO: the port appears, uploading over an Upload-only sketch works
  with no button presses (also one stuck in `setup()`), and typing into its
  port in a serial monitor neither crashes it nor stops the next upload.

### Changed

- Every sketch is smaller and has more RAM: an empty sketch 4,668 → 3,968 B
  flash and 704 → 544 B RAM with `-Os`, CHChess (LTO) 50,036 → 49,444 B and
  18,000 → 17,844 B. Two changes:
  - `USB_init()` sets up its clocks and the USB pins with direct register
    writes instead of the vendor `RCC_*ClockCmd()`/`GPIO_Init()` helpers.
    The register values are identical. It saves ~350–480 B when nothing else
    uses the helpers, less when `pinMode()` pulls in `GPIO_Init` anyway.
  - The startup code no longer registers `__libc_fini_array` with `atexit()`.
    `main()` never returns, so global destructors never ran anyway, and the
    registration linked newlib's exit handling and a 140 B table in RAM
    (~224 B flash, 144 B RAM).
  - Checked on the board: Serial output, re-uploading, global constructors,
    and the USB pin and clock registers read back identical to 0.2.3.

## 0.2.3 (2026-09-30)

### Added

- **Optimize → Smallest + LTO** (`opt=oslto`): `-Os` plus link-time
  optimisation across the core, libraries and sketch. Usually 1–5 KB smaller
  than `-Os`: CHBlackjack 50,048 → 47,528 B, CHSDtoUSB 23,788 → 20,144 B, an
  empty sketch 4,668 → 4,248 B, and CHChess fits only with it (50,036 B; it
  overflows by 5.2 KB without). Static RAM drops too, by 200–800 B. Built
  cleanly on 15 sketches and the core's library examples, and tested on the
  board: USB serial, re-uploading through the 1200-baud handshake,
  `millis()`/`micros()`/`delay()`, the `osSystickHandler` hook and SRAM
  functions all behave as with `-Os`. `-Os` stays the default.

### Changed

- The RAM report is now measured against 18,416 B, the space the linker
  actually allows for static data (20 KB SRAM less the boot block and the
  fixed 2 KB stack), instead of 20,480 B. The percentage now matches when the
  link would fail; "left for local variables" is what remains for the heap.
  The "Low memory available" warning fires at 95% of that instead of 75%, so
  it no longer appears on every sketch with a framebuffer.

### Fixed

- Wire called `GetTick()` without a prototype, so it was implicitly declared
  as returning `int` while the core defines it as `uint64_t`. It happened to
  work, but LTO flagged it as a type mismatch that could be misoptimised.

## 0.2.2 (2026-09-28)

### Changed

- New **Peripherals** board menu (Tools menu in the IDE, `periph=` in an
  FQBN). The default, "Game", compiles out Serial1, `tone()`, `analogWrite()`
  PWM and HardwareTimer; "Full" keeps them. USB Serial, SPI, Wire,
  `analogRead()`, `attachInterrupt()` and CHGameSound (which has its own timer
  path) are the same in both. With "Game" an empty sketch is 4,668 B instead
  of 8,712 B, CHSDtoUSB is 22,528 B instead of 27,100 B, and CHBlackjack
  builds again at 48,632 B instead of overflowing the app region by 2,140 B.
- Why the menu exists: those sizes were what everyone was used to, but they
  came from a hand-edited `platform.local.txt` in the installed 0.1.0 folder,
  added for CH32Doom and never part of a release. Installing 0.2.1 replaced
  that folder and the defines with it, and core.a is linked `--whole-archive`,
  so the unused peripherals' constructors and interrupt handlers cost about
  4 KB in every sketch. The defines now live in their own build property
  (`build.flags.periph`) appended to the compiler flags, so a sketch or CLI
  user overriding `compiler.*.extra_flags` no longer drops them.

## 0.2.1 (2026-09-28)

First Boards Manager release since 0.1.0; it also carries the 0.2.0 changes
below.

### Fixed

- `micros()` ran backwards inside every millisecond and jumped about 2 ms
  forward at each tick. The CH32X035 SysTick is configured to count up, but
  `getCurrentMicros()` was inherited from a down-counting STM32 SysTick and used
  `CMP + 1 - CNT` for the sub-millisecond part. Anything timed with `micros()`,
  including `pulseIn()`, saw elapsed times that were wrong by up to 2 ms and
  often negative. `millis()` and `delay()` were not affected.
- A SysTick tick that lands while interrupts are off, or while an interrupt that
  outranks SysTick is running, is now counted by `micros()` instead of being
  lost until the handler runs. The tick handler updates its flag and count as
  one unit so a nested interrupt cannot count a tick twice.

### Added

- `test/sketches/MicrosMonotonic`: on-hardware check that `micros()` never
  steps backwards and stays in step with `millis()`, with interrupts on and off.
  Verified on a board: 0 backwards steps in 143k samples, against 103k with the
  old core.
- `test/native/sim_micros.py`: host-side model of the SysTick and of both
  versions of the code, showing why each part of the fix is there.

## 0.2.0 (2026-09-04)

Not published to Boards Manager; included in 0.2.1.

### Changed

- `Serial.begin()` no longer blocks until the host enumerates the device. USB
  enumeration completes in the USB interrupt while `setup()` runs, so a sketch
  is running within a few milliseconds of power-on instead of about 1.1 s.
- Writes to a port no host has opened are discarded rather than blocking, and
  writes to a host that has stopped reading time out, so a closed serial
  monitor or a board on batteries can never stall a sketch.
  `Serial.enumerated()` and `Serial.waitForPC(ms)` are there for sketches that
  want to wait, and `-DCHGAME_USB_TX_TIMEOUT_MS=n` sets the transmit timeout.

### Added

- `test/sketches/BootTiming`: measures time to `setup()` and to enumeration.

## 0.1.0 (2026-08-21)

First release: driverless USB CDC bootloader for the CH32X035, the CHGame
Arduino board package with the toolchain, `wchisp` and `chgame-upload`
delivered as Boards Manager dependencies, host tooling and documentation.
