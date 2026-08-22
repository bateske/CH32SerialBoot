/*
 * CHGame bootloader entry.
 *
 * Boot decision (see docs/boot-flow.md):
 *
 *   reset
 *    |-- host requested the bootloader?  -> clear the request, stay here forever
 *    |-- application metadata valid and CRC matches?  -> jump to the application
 *    '-- otherwise                                     -> stay here forever
 *
 * There is deliberately no timeout in either "stay here" branch: a device that
 * cannot be re-flashed without a power cycle is a support problem, and a device
 * that gives up waiting and jumps into a half-written image is a brick.
 */
#include "chgame_map.h"
#include "bootreq.h"
#include "appmeta.h"
#include "jump.h"
#include "led.h"
#include "sys.h"
#include "spin.h"
#include "proto.h"
#include "flash.h"
#include "ch32x035.h"

int main(void)
{
    int requested, app_state;

    SystemCoreClockUpdate();
    ramfunc_init();   /* flash routines must be resident in SRAM before first use */
    sys_init();
    led_init();

    /* Read and immediately clear the request, so a bootloader that is reset
       again for any other reason does not stay latched in update mode. */
    requested = bootreq_pending();
    if (requested)
        bootreq_clear();

    app_state = appmeta_check();

#if CHGAME_DIAG
    /* Bench diagnostic: say out loud how many times we have booted without a
       power cycle, and what we think of the application image, BEFORE acting on
       either. Compiled out of production builds. */
    led_report(bootcount_bump(), app_state);
#endif

    if (!requested && app_state == APP_VALID) {
        led_set(LED_OFF);
#if CHGAME_DIAG
        /* "Bootloader ran, validated the image, handing over" - the marker that
           separates a bootloader fault from an application fault when the LED
           is the only instrument available. Invaluable during bring-up, but it
           is 100 ms on EVERY boot and it makes startup look like a double
           blink, so it belongs with the other diagnostics rather than on the
           normal path. */
        led_signature_handoff();
#endif
        jump_to_app();
    }

    led_set(requested ? LED_REQUESTED : LED_WAIT_UPDATE);

    /* Bring up USB CDC only on the path that actually stays here. The handover
       path must not leave a configured USB device behind for the application to
       trip over. */
    proto_init();

    for (;;) {
        proto_task();
        led_task();
    }
}
