#ifndef CHGAME_BOOTREQ_H
#define CHGAME_BOOTREQ_H
#include "chgame_map.h"

/* The retained boot-request block. Lives in .boot_magic, which both the
 * bootloader and the application linker scripts pin to CHGAME_MAGIC_ADDR and
 * mark NOLOAD, so startup neither loads nor zeroes it and it survives
 * NVIC_SystemReset().
 *
 * NOTE: SRAM retention across a system reset is an assumption that gets
 * verified on real hardware as the first test of Milestone 2. If it does not
 * hold, the fallback is a dedicated flash request page. */
extern volatile chgame_bootreq_t chgame_bootreq;

int  bootreq_pending(void);   /* magic AND its inverse both intact */
void bootreq_set(void);
void bootreq_clear(void);

/* Increments the warm-reset boot counter and returns the new value. Returns 1
 * when the previous value did not survive (a cold boot, or SRAM that does not
 * retain), so a climbing value is unambiguous evidence of a reset loop. */
uint32_t bootcount_bump(void);

#endif
