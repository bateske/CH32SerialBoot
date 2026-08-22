# Flash and RAM map

Authoritative source: `bootloader/src/chgame_map.h`. `tools/gen_ld.py` regenerates
both linker scripts from it on every build, so the header and the scripts cannot
drift.

The bootloader reservation started at 8 KB and grew to 12 KB once the flash
writer and the developer self-update landed; it is currently about 59% used.
Shrinking it later is cheap - change one constant and rebuild - but it costs a
reflash of every device, since it moves `APP_START`. Host tools parse that header
via `tools/chgame_map.py`; the linker scripts mirror it by hand and are checked
by `ASSERT`s at link time. Do not hard-code these numbers anywhere else.

## Flash — 62 KB user area, 256-byte pages

```
0x0000  +--------------------------------+
        | CHGame bootloader     12 KB    |  link_boot.ld
0x3000  +--------------------------------+  CHGAME_APP_START
        | Application         50944 B    |  link_chgame_app.ld
        |                                |  upload.maximum_size = 50944
0xF700  +--------------------------------+  CHGAME_META_ADDR
        | Application metadata   256 B   |  one page, written LAST
0xF800  +--------------------------------+  end of user flash
```

The part has 62 KB of user Code FLASH (datasheet, CH32X035G8U6) plus a separate
3328-byte System FLASH holding the factory ISP. `wchisp` reports "64KiB" from its
ID table; that is the nominal family size, not the usable user area.

Erase and program granularity is 256 bytes (`FLASH_ErasePage_Fast` /
`FLASH_ProgramPage_Fast`), so every boundary above is page aligned.

The core's `EEPROM` library stores into the **option bytes** (`OB_BASE`), not the
flash tail, so it does not collide with the metadata page.

### Overflow protection

Both linker scripts end in a hard assertion, so an oversized image fails the
*link* rather than silently corrupting a neighbour:

- `link_boot.ld`: `ASSERT( _etext <= 0x3000 )`
- `link_chgame_app.ld`: `ASSERT( _etext <= 0xF700 )`

## Metadata page

`chgame_meta_t` — magic, metadata version, image length, CRC-32/ISO-HDLC over
the image, application version. Written only after a complete image verifies,
which is what makes an interrupted update recoverable by construction.

## RAM — 20 KB

```
0x20000000  +-----------------------------+
            | retained boot request  16 B |  .boot_magic, NOLOAD
0x20000010  +-----------------------------+
            | .ramfunc / .data / .bss     |
            | ...                         |
            | stack (2 KB, grows down)    |
0x20005000  +-----------------------------+
```

The 16-byte block is carved out at the same address by *both* linker scripts and
marked `NOLOAD`, so startup neither loads nor zeroes it and it survives
`NVIC_SystemReset()`. It holds the boot-request marker (magic plus its
complement, so uninitialised SRAM cannot forge a request) and a warm-reset boot
counter used as a reset-loop detector on the bench.

`.ramfunc` exists so flash erase/program routines execute from SRAM: the part
stalls instruction fetch while the flash controller is busy, so running the
writer from flash is a classic way to hang the core.
