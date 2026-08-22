# CH32X035 gotchas found the hard way

Four bugs cost roughly a day of bring-up between them. All four share a shape:
**the wrong behaviour looked like success, or looked like someone else's fault.**
Recorded so they are paid for once.

---

## 1. `mret` in WCH's startup drops you to User mode

`startup_ch32x035.S` writes `mstatus = 0x88` (MIE | MPIE) and never sets `MPP`,
so its closing `mret` returns to **User mode**.

Harmless for a normal sketch, which afterwards only touches memory-mapped
peripherals. Fatal for a bootloader: the *application's* startup then runs in
User mode and its first `csrw 0xbc0` (CORECFGR — Machine-only, CSR address bits
[9:8] = `0b11`) raises an illegal-instruction exception.

The symptom is maximally misleading. The bootloader looks healthy, the image
verifies, the handover happens, and the application dies before installing its
own `mtvec` — so the fault is caught by the *bootloader's* handler, making it
look like the bootloader crashed.

**Fix:** `bootloader/src/startup_chgame_boot.S` sets `mstatus = 0x1888`
(adds `MPP = 0b11`) so `mret` stays in Machine mode.

**False lead:** `csrw 0x800` (`__disable_irq`) executing fine looks like proof
of Machine mode. It is not — CSR `0x800` has privilege bits `0b00` and is
user-accessible. Check the CSR address, not whether *some* CSR write worked.

Independently confirmed by `krzysztofgawrys/ch32x035-dfu-boot`, which solves it
by using `jal main` instead of `mret`.

---

## 2. The flash controller wants `0x08000000`, not the execution alias

The flash array is addressed twice:

| Address | Used by |
|---|---|
| `0x00000000` | CPU instruction fetch; everything in `chgame_map.h` |
| `0x08000000` (`FLASH_BASE`) | the FLASH controller's `ADDR` register and page-buffer stores |

Reads work through either. **Programming does not.** Give the controller a
`0x0000xxxx` address and:

- `BSY` never asserts, so a `while (STATR & BSY)` busy-wait falls straight
  through, and the driver **reports success having done nothing**;
- a page-buffer store to the execution alias hangs.

**Fix:** `flash.c` applies the translation at the controller boundary
(`#define PROG(addr)`), so no caller has to remember it.

---

## 3. SysTick counts DOWN, and calibrating it at zero lies to you

`CTLR` bit 4 selects **down**-counting (WCH's `Delay_Ms` sets it and counts
toward `CMP`). Treating `CNT` as an up-counter makes `now - then` underflow, so
**every elapsed-time test reads as "ages ago"**.

That single bug produced two symptoms that looked like a broken USB stack:

- the protocol's inter-frame timeout reset the parser between every pair of USB
  packets, giving a hard cliff at **exactly 64 bytes** — the USB Full-Speed bulk
  max packet size, which is precisely the number you would expect if the *driver*
  could not do multi-packet transfers;
- `tx_byte`'s 500 ms send deadline expired instantly, silently dropping replies
  to commands that had actually succeeded.

It cost an unnecessary detour into blaming the vendored CDC driver. The tell
that should have been caught sooner: a 64-byte *packet* limit is real and normal,
but a 64-byte *transfer* limit is not — multi-packet transfers are ordinary USB.
When a measurement matches a hardware constant suspiciously well, check whether
your own code is manufacturing that constant.

**Fix:** `sys.c` measures the direction at startup and negates a down-counter so
callers can use plain unsigned subtraction. `STATUS` reports both the direction
and a live tick sample, so the host can verify the timebase from outside instead
of trusting the firmware's own arithmetic.

**Sub-gotcha:** `CTLR` bit 5 (`INIT`) zeroes `CNT` as the counter starts. Sample
the direction from zero and a down-counter immediately wraps to `0xFFFFFFFF`,
so the test measures across the wrap and concludes "up". Seed `CNT` to
`0x80000000` before measuring.

---

## 4. `.ramfunc` is not enough on its own

Putting the flash routines in SRAM solves half the problem. The **interrupt
vector table is still in flash**, so an interrupt taken while the flash
controller is busy makes the core fetch a vector from flash mid-operation — the
exact stall `.ramfunc` exists to avoid. With USB running, this is not a rare
race.

**Fix:** mask interrupts around each individual page operation, releasing
between pages so USB still gets serviced during a long erase.

---

## Diagnosing without SWD

There is no SWD probe on this board and no external reset pin (`RST_MOD = 0b11`).
What made these tractable:

- **The fault handler blinks `mcause` and `mepc`.** Reading `mepc = 0x21B8`
  straight off the LED is what identified gotcha 1. Encode the value in
  **binary** as long/short pulses — far easier to read reliably than counting up
  to sixteen flashes.
- **Encode which image caught the fault.** Knowing the *bootloader's* handler
  caught it proved the application had not yet reached `csrw mtvec`, which
  narrowed the fault to a handful of instructions.
- **Report internal state over the protocol** once USB is up — parser state,
  byte counters, the raw timebase. Gotcha 3 was only settled by having the device
  report its own tick counter and comparing against host wall-clock.

---

## Appendix: M4 interrupted-update results

Real power cuts on the development board, `test/hil/test_powercut.py`:

| Cut point | Result |
|---|---|
| 1% (immediately after erase, mid-write) | recovered |
| 3% (mid-write) | recovered |
| 94% (nearly complete image, no metadata) | recovered |

Every case came back **in the bootloader without touching BOOT**, reported the
application invalid, refused `RUN`, left the bootloader region intact, and
accepted a fresh upload.

The 94% case is the one worth keeping: a readback afterwards showed ~30 KB of
complete-looking image sitting in the application region. The bootloader still
refused it, because validity comes from the metadata record and nothing else.
That is what the metadata-first erase ordering in `do_begin()` buys - from the
instant an update starts, the old application is already marked invalid, so
there is no window in which a half-written image can look launchable.

---

## 5. `wchisp flash` erases only what it writes

Provisioning a bootloader-only image (7268 bytes) erases roughly the first 8 KB.
The application at `0x3000` and, critically, its **metadata page at `0xF700`**
survive untouched.

The freshly written bootloader then boots, finds metadata that still checksums,
and does exactly what it is designed to do: launches the stale application. Burn
Bootloader appears to have done nothing at all — the board comes back running the
sketch that was on it before.

The symptom is doubly misleading, because the stale application enumerates as a
CDC port, so it *looks* like a working provisioned device until you notice it
does not answer `HELLO`. The tell is a **write timeout**: a sketch that prints
but never calls `Serial.read()` lets its receive buffer fill and NAKs, so host
writes block. A bootloader always drains.

**Fix:** `provision()` runs `wchisp erase` before writing, so every provisioning
path gets a full chip erase — including Upload Using Programmer, which Arduino
does not precede with an erase step.

This is what Arduino's separate `erase.pattern` step before Burn Bootloader is
for. It was initially dismissed here as redundant on the assumption that writing
implies erasing. It does not.
