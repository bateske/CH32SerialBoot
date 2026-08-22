#ifndef CHGAME_LED_H
#define CHGAME_LED_H
#include <stdint.h>

/* Status LED on PB9 (netlist: PB9 -> R9 -> LED2 -> GND, so active HIGH).
 *
 * Without SWD this LED is the bootloader's only debug channel before USB
 * enumerates, so the patterns are deliberately distinguishable at a glance:
 *
 *   LED_OFF            dark          bootloader is about to jump to the app
 *   LED_WAIT_UPDATE    2 Hz even     bootloader idle, no valid application
 *   LED_REQUESTED      double-pulse  bootloader entered by host request
 *   LED_BUSY           10 Hz         erasing / writing flash
 *   LED_FAULT          solid on      unrecoverable state
 */
typedef enum {
    LED_OFF = 0,
    LED_WAIT_UPDATE,
    LED_REQUESTED,
    LED_BUSY,
    LED_FAULT,
} led_pattern_t;

void led_init(void);
void led_set(led_pattern_t p);
void led_task(void);          /* non-blocking; call from the main loop */
void led_panic(void);         /* solid on, never returns */
void led_signature_handoff(void); /* one long pulse, busy-loop timed */

/* Diagnostic report, blinked before the boot decision is acted on.
 *
 *   [ 1.2 s solid ]  frame marker, unmistakable
 *   [ n short   ]    warm-reset boot count (capped at 9)
 *   [ gap       ]
 *   [ m short   ]    application state: 1 VALID, 2 NO_META, 3 BAD_LENGTH, 4 BAD_CRC
 *   [ long gap  ]
 *
 * A boot count that climbs 1, 2, 3 ... on its own is a reset loop. All timing
 * is busy-loop driven, so the report is trustworthy even if the timebase is not. */
void led_report(uint32_t boot_count, int app_state);

#endif
