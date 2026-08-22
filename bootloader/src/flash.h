#ifndef CHGAME_FLASH_H
#define CHGAME_FLASH_H
#include <stdint.h>
#include "chgame_map.h"

/* RAM-resident flash driver.
 *
 * Every routine that touches the flash controller is placed in .ramfunc and
 * executes from SRAM. The CH32X035 stalls instruction fetch while the flash
 * controller is busy, so a writer running from flash can stall trying to fetch
 * its own next instruction. Running from RAM removes the question entirely.
 *
 * These are the ONLY functions in the bootloader that can modify flash, and
 * every one of them re-checks its address range. Bounds checking lives here,
 * at the point of no return, rather than only in the protocol layer — a caller
 * bug must not be able to erase the bootloader.
 */

#define FLASH_OK            0
#define FLASH_ERR_RANGE     1
#define FLASH_ERR_ALIGN     2
#define FLASH_ERR_VERIFY    3

/* Region the caller is permitted to modify. */
typedef enum {
    FLASH_REGION_APP  = 0,   /* CHGAME_APP_START .. CHGAME_FLASH_SIZE (incl. metadata) */
    FLASH_REGION_BOOT = 1,   /* 0 .. CHGAME_APP_START — developer command only */
} flash_region_t;

void ramfunc_init(void);   /* copy .ramfunc from flash to SRAM; call once at boot */

int  flash_erase_page(uint32_t addr, flash_region_t region);
int  flash_write_page(uint32_t addr, const uint8_t *data, flash_region_t region);
int  flash_erase_range(uint32_t addr, uint32_t len, flash_region_t region);

/* ---- developer self-update -------------------------------------------------
 * Rewrites the BOOTLOADER's own region from an image already staged in the
 * application region, then resets. Never returns.
 *
 * This exists to make bootloader iteration cost one USB command instead of a
 * physical BOOT-button-plus-power-cycle, which during bring-up is the single
 * biggest drag on the development loop.
 *
 * It is unreachable through any normal upload command: the host must first send
 * DEV_UNLOCK with the correct key, and the whole facility compiles out when
 * CHGAME_ALLOW_SELFUPDATE is 0. The gate is there to prevent ACCIDENTS, not
 * attacks - anyone who can talk to the port can read the key out of the binary.
 * Treat it as a safety interlock, not a security boundary.
 *
 * There is an unavoidable window, roughly the erase+program time of the whole
 * boot region, in which a power loss leaves the device with no usable
 * bootloader. That is recoverable exactly one way: the BOOT button and the
 * factory ISP (docs/recovery.md). It is why that path must never be
 * compromised.
 */
void flash_selfupdate(uint32_t src, uint32_t len) __attribute__((noreturn));

#endif
