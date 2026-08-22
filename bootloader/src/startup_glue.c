/* Symbols the WCH startup and newlib expect but that nothing in an 8 KB,
 * C-only bootloader actually needs.
 *
 * ch32x035_misc.c supplies weak stubs for every IRQ handler except NMI,
 * HardFault and SysTick (see the commented-out block at its end); the first two
 * live in fault.c, and SysTick is here because the bootloader uses SysTick as a
 * free-running counter with its interrupt disabled, so this can never run.
 *
 * _init/_fini exist only to satisfy __libc_init_array/__libc_fini_array, which
 * startup_ch32x035.S calls unconditionally. There are no static constructors. */

void SysTick_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SysTick_Handler(void) { }

void _init(void) { }
void _fini(void) { }
