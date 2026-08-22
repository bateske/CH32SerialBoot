# Recovery — getting back to the WCH factory ISP

The CH32X035's factory USB ISP bootloader lives in a separate, non-erasable boot
area. Nothing this project writes can damage it, so this path always works no
matter how badly the custom bootloader or application is broken. **Never remove
or obstruct it.**

## The hardware condition

From the board netlist: `SW9` (the DOWNLOAD/BOOT button) bridges the `UDP` net
(USB D+, `PC17`, MCU pin 27) to `3v3` through `R10` (4.7 kΩ). The CH32X035
samples USB D+ at reset; finding it pulled high through a resistor is what
selects the factory ISP instead of user flash.

The consequence: **BOOT must be held across a reset**, not merely pressed.

## Procedure

1. Hold the BOOT button down.
2. Cycle power with the slide switch (off, then on) while still holding BOOT.
3. Release BOOT.
4. The device enumerates as `1A86:55E0` instead of the usual CDC COM port.

Then, from the repo root in Git Bash:

```bash
./tools/flash.sh
```

which probes for the ISP device and writes `build/chgame_full.bin`.

## Requirements on the host

`wchisp` talks to the ISP device over WinUSB, so on Windows the `1A86:55E0`
device needs the WinUSB driver bound to it (Zadig, or the WCH driver package).
**This requirement applies only to recovery and factory provisioning.** Normal
end-user flashing goes over the CHGame bootloader's own USB CDC interface, which
every OS drives with an inbox class driver and no installation at all.

On this development machine: `wchisp` 0.2.3 at `D:\WCHISP\wchisp.exe`, WinUSB
already bound.

## Verifying without flashing

```bash
/d/WCHISP/wchisp.exe probe
/d/WCHISP/wchisp.exe info
```

## Confirmed on hardware

Milestone 0 gate passed 2026-08-21 on the development board:

```
$ wchisp probe
Found 1 devices
Device #0: CH32X035G8U6[0x5623]

$ wchisp info
Chip: CH32X035G8U6[0x5623]
Chip UID: CD-AB-88-D8-60-BE-B5-42
BTVER(bootloader ver): 02.60
RDPR 0xA5 -> Unprotected      WRP 0xFFFFFFFF -> Unprotected
RST_MOD 0b11 -> RST alternate function disabled, PA21/PC3/PB7 are GPIO
```

Note `RST_MOD`: there is no external reset pin in this configuration, and PB7 is
free for `BTN_SELECT` as the schematic intends. The only ways to reset the part
are the power switch and `NVIC_SystemReset()` — which is precisely why the
software boot-request path has to be reliable.

The chip UID also explains the USB serial number seen in app mode
(`JJFFFFFFFF42B5BE60D888ABCD`): the CDC library builds it from the 96-bit UID.

## Why this path cannot be destroyed

The CH32X035 datasheet (section on memory organisation) splits flash in two:

- **62 KB Code FLASH** — the user area, everything this project writes.
- **3328 B System FLASH** — the BOOT area holding the factory ISP program.

User-area erase and program operations cannot reach the System FLASH, so no
bootloader bug, interrupted update, or corrupt image can remove the recovery
path. `wchisp erase`/`flash` likewise only touch the 62 KB user area — the run
above erased 63 sectors and left the ISP intact.

Note that `wchisp` reports "Code Flash: 64KiB" from its chip ID table. That is
the nominal part-family size; the datasheet is explicit that the G8U6 user area
is 62 KB, which is what `bootloader/src/chgame_map.h` and the stock Arduino core
linker script both assume.
