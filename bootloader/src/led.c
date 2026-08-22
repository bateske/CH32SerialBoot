#include "led.h"
#include "sys.h"
#include "spin.h"
#include "ch32x035.h"

#define LED_PORT   GPIOB
#define LED_PIN    GPIO_Pin_9

static led_pattern_t g_pattern = LED_OFF;
static uint32_t      g_phase_start;
static uint8_t       g_phase;

static void led_write(int on)
{
    if (on) LED_PORT->BSHR = LED_PIN;
    else    LED_PORT->BCR  = LED_PIN;
}

void led_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    gpio.GPIO_Pin   = LED_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_PORT, &gpio);
    led_write(0);
    g_phase_start = sys_ticks();
    g_phase = 0;
}

void led_set(led_pattern_t p)
{
    if (p == g_pattern) return;
    g_pattern     = p;
    g_phase       = 0;
    g_phase_start = sys_ticks();
    led_write(p == LED_FAULT);
}

/* Each pattern is a table of {on?, duration_ms} phases, cycled forever. */
typedef struct { uint8_t on; uint16_t ms; } phase_t;

static const phase_t ph_wait[]      = { {1, 250}, {0, 250} };
static const phase_t ph_requested[] = { {1,  80}, {0, 120}, {1, 80}, {0, 720} };
static const phase_t ph_busy[]      = { {1,  50}, {0,  50} };

void led_task(void)
{
    const phase_t *tab;
    uint8_t n;

    switch (g_pattern) {
        case LED_WAIT_UPDATE: tab = ph_wait;      n = 2; break;
        case LED_REQUESTED:   tab = ph_requested; n = 4; break;
        case LED_BUSY:        tab = ph_busy;      n = 2; break;
        default: return;   /* OFF and FAULT are static */
    }

    if (sys_elapsed_ms(g_phase_start) >= tab[g_phase].ms) {
        g_phase       = (uint8_t)((g_phase + 1u) % n);
        g_phase_start = sys_ticks();
        led_write(tab[g_phase].on);
    }
}

void led_panic(void)
{
    led_set(LED_FAULT);
    led_write(1);
    for (;;) { }
}

/* Brief "bootloader ran and is handing over" blip. Kept short deliberately: it
 * is on the critical path of EVERY boot, and a diagnostic that costs the user
 * most of a second on every power-on is not worth what it tells them. */
void led_signature_handoff(void)
{
    led_write(1); spin_ms(40);
    led_write(0); spin_ms(60);
}

static void led_blink_n(uint32_t n)
{
    if (n > 9) n = 9;                 /* keep it countable by eye */
    while (n--) {
        led_write(1); spin_ms(80);
        led_write(0); spin_ms(180);
    }
}

void led_report(uint32_t boot_count, int app_state)
{
    led_write(1); spin_ms(400);       /* frame marker */
    led_write(0); spin_ms(300);

    led_blink_n(boot_count);
    spin_ms(300);

    led_blink_n((uint32_t)app_state + 1u);   /* +1 so VALID (0) is one pulse, not zero */
    spin_ms(600);
}
