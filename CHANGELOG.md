# Changelog

The version is the `version=` line in `arduino/CHGame/platform.txt`. That is the
number Boards Manager compares with what a user has installed, so every release
bumps it. `tools/release.sh <version> bateske/CH32SerialBoot` publishes the
release and uses the matching section of this file as the GitHub release notes.

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
