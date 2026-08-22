# Boot decision flow

```
reset
  |
  |-- host requested the bootloader?      -> clear the request, stay in bootloader forever
  |-- metadata valid AND CRC-32 matches?  -> hand over to the application at 0x3000
  '-- otherwise                           -> stay in bootloader forever
```

Neither "stay" branch has a timeout. A device that needs a power cycle to be
re-flashed is a support problem; a device that gives up waiting and jumps into a
half-written image is a brick. The application is launched only when its
metadata page says the image is complete *and* a CRC-32 over the whole image
agrees, so an interrupted update simply leaves the bootloader in charge.

## Handing over

`jump_to_app()` (bootloader/src/jump.c) leaves the machine looking as close to
post-reset as it can before jumping:

- global interrupts off,
- every NVIC source masked and its pending bit cleared (both words),
- SysTick stopped and its flag cleared,
- peripherals the bootloader enabled reset and unclocked,
- `fence.i`, then jump to `CHGAME_APP_START` (0x3000).

The application's own `handle_reset` re-establishes `gp`, `sp` and `mtvec`, so
nothing else needs to be pre-arranged.

## The privilege-mode trap (found the hard way on real hardware)

**The bootloader must be running in Machine mode when it jumps to the
application.** This is not optional and it is not obvious.

WCH's stock `startup_ch32x035.S` ends with:

```asm
li t0, 0x88          /* MIE | MPIE -- note: MPP untouched */
csrs mstatus, t0
...
csrw mepc, t0        /* t0 = main */
mret
```

`mret` sets the privilege level to `mstatus.MPP`, which that code never sets, so
**every image built with the stock startup drops to User mode on its way into
`main()`**.

For an ordinary sketch that is completely harmless: after startup it only ever
touches memory-mapped peripherals and user-level CSRs. It is fatal for a
bootloader, because the *application's* startup then runs in User mode, and its
first action is:

```asm
li t0, 31
csrw 0xbc0, t0       /* CORECFGR */
```

CSR address bits [9:8] encode the required privilege. `0xBC0` has `0b11` —
Machine-only — so this raises an **illegal instruction** exception (`mcause` 2)
at the very first instruction of the application's configuration sequence.

The symptom is maximally confusing: the bootloader looks healthy, the image
verifies, the handoff happens, and then the application dies before it can even
install its own `mtvec` — so the fault is caught by the *bootloader's* handler,
making it look like the bootloader crashed.

Two things made this diagnosable without SWD:

1. The fault handler reports `mcause` and `mepc` on the status LED, so the
   faulting address (`0x21B8`) was readable directly off the board.
2. The handler encodes which image caught the fault, which is what revealed that
   `mtvec` had not yet been re-pointed.

A false lead worth recording: `csrw 0x800` (`__disable_irq`) executes fine in the
bootloader, which looks like proof of Machine mode. It is not — `0x800` has
privilege bits `0b00` and is user-accessible.

### The fix

`bootloader/src/startup_chgame_boot.S` is a fork of the vendored startup whose
**only** change is:

```asm
li t0, 0x1888        /* MIE | MPIE | MPP(0b11) */
csrs mstatus, t0
```

so `mret` returns to Machine mode and the bootloader stays there.

The application deliberately keeps WCH's stock startup. It cold-starts in
Machine mode (courtesy of the above), configures its CSRs exactly as it would on
a bare board, and then drops to User mode via its own `mret` — identical to any
other sketch for this core. The divergence is one instruction, in one file, on
the bootloader side only.
