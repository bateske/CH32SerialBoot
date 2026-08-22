/*
 * Application side of the CHGame boot-request handshake.
 *
 * The retained block lives in .boot_magic, which the application linker script
 * pins to CHGAME_MAGIC_ADDR and marks NOLOAD -- exactly as the bootloader's
 * script does -- so startup neither loads nor zeroes it and the value survives
 * NVIC_SystemReset(). Verified on hardware: SRAM does survive a warm reset on
 * this part, but NOT a power cycle, which is what we want. A marker that
 * outlived power loss could strand a device in the bootloader.
 */
#include "chgame_map.h"
#include "chgame_boot.h"
#include "ch32yyxx.h"

static volatile chgame_bootreq_t chgame_bootreq
    __attribute__((section(".boot_magic"), used));

void chgame_enter_bootloader(void)
{
    chgame_bootreq.magic   = CHGAME_BOOT_MAGIC;
    chgame_bootreq.inverse = (uint32_t)~CHGAME_BOOT_MAGIC;

    /* Leave the USB peripheral detached so the host sees a clean disconnect
       rather than a device that stops answering. */
    __disable_irq();
    NVIC_SystemReset();

    for (;;) { }
}
