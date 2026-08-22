#include "jump.h"
#include "chgame_map.h"
#include "proto.h"
#include "ch32x035.h"

/*
 * The application's own startup (startup_ch32x035.S handle_reset) re-establishes
 * gp, sp and mtvec, so this only has to leave the machine in a state that looks
 * like a fresh reset: interrupts off, nothing pending, peripherals we touched
 * put back.
 */
void jump_to_app(void)
{
    /* Detach from USB first, while interrupts are still live and the host can
       still observe the change. Leaving the pull-up asserted strands the host
       with a device that never answers again. */
    proto_shutdown();

    __disable_irq();

    /* Mask and clear every interrupt source, both NVIC words. A latched USB or
     * SysTick interrupt firing after the jump but before the app installs its
     * own mtvec would vector into the bootloader's freed handlers. */
    NVIC->IRER[0] = 0xFFFFFFFFu;
    NVIC->IRER[1] = 0xFFFFFFFFu;
    NVIC->IPRR[0] = 0xFFFFFFFFu;
    NVIC->IPRR[1] = 0xFFFFFFFFu;

    SysTick->CTLR = 0;
    SysTick->SR   = 0;

    /* Return the peripherals the bootloader enabled to reset state, so the
     * application starts from the same conditions as a cold boot. */
    RCC_APB2PeriphResetCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB2PeriphResetCmd(RCC_APB2Periph_GPIOB, DISABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, DISABLE);

    __asm volatile (
        "fence.i\n"
        "jr %0\n"
        :
        : "r" (CHGAME_APP_START)
    );

    __builtin_unreachable();
}
