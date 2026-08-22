#ifndef CHGAME_BOOT_H
#define CHGAME_BOOT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Request the CHGame bootloader and reset. Never returns.
 *
 * Sets the retained magic in .boot_magic (pinned by the linker script to the
 * same SRAM address the bootloader reads) and performs a system reset. The
 * bootloader sees the request, clears it, and stays in update mode.
 *
 * Called automatically by the USB layer on the 1200-baud upload handshake.
 * A sketch may also call it directly to offer its own "enter bootloader" path. */
void chgame_enter_bootloader(void);

#ifdef __cplusplus
}
#endif
#endif
