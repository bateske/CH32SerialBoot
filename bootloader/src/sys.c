#include "sys.h"
#include "ch32x035.h"

/* Which way SysTick counts is MEASURED at startup rather than assumed.
 *
 * WCH's own Delay_Ms sets CTLR bit 4 and then counts toward CMP, which implies
 * DOWN-counting, but the bit is not documented in the datasheet and getting it
 * wrong is silently catastrophic: on a down-counter, (now - then) underflows to
 * a huge unsigned value, so every elapsed-time test reads as "ages ago". That
 * turned the protocol's inter-frame timeout into a parser reset that fired
 * between every pair of USB packets, which looked exactly like a broken USB
 * driver.
 *
 * Measuring costs a few hundred cycles once. If the counter turns out not to be
 * running at all, direction stays "up" and sys_ticks() is constant, which makes
 * every elapsed test read as zero - the safe failure, since no timeout fires. */
static uint8_t g_down;

void sys_init(void)
{
    SysTick->SR   = 0;
    SysTick->CNT  = 0;
    SysTick->CMP  = 0xFFFFFFFFFFFFFFFFull;
    /* Bit 4 must be set for the counter to advance: WCH's own Delay_Us/Delay_Ms
       in debug.c set it before every enable. Omitting it leaves CNT parked at 0,
       which reads as a hang rather than a slow clock. Bit 5 = INIT (clear the
       counter), bit 0 = STE (enable). */
    SysTick->CTLR = (1u << 4) | (1u << 5) | (1u << 0);

    {
        uint32_t a, b;

        /* Bit 5 (INIT) zeroes CNT as the counter starts. Sampling from zero
           means a down-counter immediately wraps to 0xFFFFFFFF, and the
           direction test then measures across the wrap and concludes "up" -
           which is exactly what happened the first time this was written.
           Move well away from either wrap boundary before measuring. */
        SysTick->CNT = 0x80000000ull;

        a = (uint32_t)SysTick->CNT;
        for (volatile int i = 0; i < 500; i++) { }
        b = (uint32_t)SysTick->CNT;
        g_down = (b < a) ? 1u : 0u;
    }
}

uint32_t sys_ticks(void)
{
    uint32_t v = (uint32_t)SysTick->CNT;
    /* Negating a down-counter yields a monotonically increasing value, so every
       caller can use plain unsigned subtraction regardless of direction. */
    return g_down ? (uint32_t)(0u - v) : v;
}

uint32_t sys_counts_down(void)
{
    return g_down;
}

uint32_t sys_millis(void)
{
    return sys_ticks() / SYS_TICKS_PER_MS;
}

void sys_delay_ms(uint32_t ms)
{
    uint32_t start = sys_ticks();
    while ((sys_ticks() - start) < ms * SYS_TICKS_PER_MS) { }
}
