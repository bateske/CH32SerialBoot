/*
 * M1/M2 relocated-application test.
 *
 * Bare metal, linked at CHGAME_APP_START (0x2000) with the same script the
 * Arduino CHGame board will use. It proves two things the real application will
 * have to do:
 *
 *   1. run correctly from the relocated address (M1), and
 *   2. request the bootloader by setting the retained magic and resetting (M2).
 *
 * The A button stands in for the 1200-baud USB touch that the Arduino core will
 * use later. Testing the boot-request path with a button first keeps it
 * independent of USB, so an M2 failure is unambiguous about which half broke.
 *
 * Blink signature: BLINK_PULSES fast pulses then a pause. Deliberately unlike
 * every bootloader pattern in bootloader/src/led.h.
 *
 * BLINK_PULSES is a build-time knob so the test suite can flash a visibly
 * DIFFERENT image and confirm from across the room that an upload really did
 * replace the firmware, rather than trusting a status byte.
 *
 * Timing is a plain busy loop, NOT SysTick — coupling the proof that a relocated
 * image runs to the timer configuration is how the first attempt produced an
 * ambiguous "solid on" instead of an answer.
 */
#include "chgame_map.h"
#include "bootreq.h"
#include "spin.h"
#include "ch32x035.h"

#define LED_PIN     GPIO_Pin_9    /* PB9,  active high */
#define BTN_A_PIN   GPIO_Pin_1    /* PB1,  active low (switch to GND, internal pull-up) */

#ifndef BLINK_PULSES
#define BLINK_PULSES 3
#endif

static void request_bootloader_if_A_pressed(void)
{
    if (GPIOB->INDR & BTN_A_PIN)
        return;                            /* not pressed */

    /* Crude debounce: it must still be down after a short settle. */
    spin_ms(30);
    if (GPIOB->INDR & BTN_A_PIN)
        return;

    GPIOB->BCR = LED_PIN;
    bootreq_set();
    NVIC_SystemReset();
    for (;;) { }
}

/* Busy-wait in small slices so the button stays responsive. */
static void wait_ms(uint32_t ms)
{
    while (ms >= 10u) { spin_ms(10); request_bootloader_if_A_pressed(); ms -= 10u; }
    if (ms) spin_ms(ms);
}

int main(void)
{
    GPIO_InitTypeDef gpio = {0};

    SystemCoreClockUpdate();
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    gpio.GPIO_Pin   = LED_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    gpio.GPIO_Pin  = BTN_A_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gpio);

    for (;;) {
        for (int i = 0; i < BLINK_PULSES; i++) {
            GPIOB->BSHR = LED_PIN;  wait_ms(80);
            GPIOB->BCR  = LED_PIN;  wait_ms(120);
        }
        wait_ms(1000);
    }
}
