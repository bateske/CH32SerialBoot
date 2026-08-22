#ifndef CHGAME_SYS_H
#define CHGAME_SYS_H
#include <stdint.h>

/* Free-running SysTick used as a millisecond timebase. HCLK/8 is the SysTick
 * clock on this part (matching WCH's own Delay_Init), so at 48 MHz the counter
 * ticks at 6 MHz and the low 32 bits wrap every ~716 s. All elapsed-time
 * comparisons use unsigned subtraction, so the wrap is harmless. */
#define SYS_TICKS_PER_MS   (F_CPU / 8u / 1000u)

void     sys_init(void);
uint32_t sys_ticks(void);
uint32_t sys_millis(void);
void     sys_delay_ms(uint32_t ms);
uint32_t sys_counts_down(void);   /* measured at init; reported by STATUS */

/* Elapsed milliseconds since a tick stamp, wrap-safe. */
static inline uint32_t sys_elapsed_ms(uint32_t since)
{
    return (sys_ticks() - since) / SYS_TICKS_PER_MS;
}

#endif
