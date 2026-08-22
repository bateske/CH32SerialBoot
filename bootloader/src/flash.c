#include "flash.h"
#include "ch32x035.h"

#define RAMFUNC __attribute__((section(".ramfunc"), noinline))

/* Flash controller bits, duplicated from the SPL's private header so that this
 * file does not depend on the SPL implementation at runtime — nothing here may
 * call into code that still lives in flash. */
#define CR_STRT      ((uint32_t)0x00000040)
#define CR_FLOCK     ((uint32_t)0x00008000)
#define CR_PAGE_PG   ((uint32_t)0x00010000)
#define CR_PAGE_ER   ((uint32_t)0x00020000)
#define CR_BUF_LOAD  ((uint32_t)0x00040000)
#define CR_BUF_RST   ((uint32_t)0x00080000)
#define SR_BSY       ((uint32_t)0x00000001)
#define FKEY1        ((uint32_t)0x45670123)
#define FKEY2        ((uint32_t)0xCDEF89AB)

/* The flash array is addressed TWICE in this part's memory map:
 *
 *   0x00000000  execution alias  - what the CPU fetches and what every address
 *                                  in chgame_map.h refers to
 *   0x08000000  FLASH_BASE       - what the FLASH CONTROLLER expects in its ADDR
 *                                  register and for page-buffer load stores
 *
 * Reads work through either. Programming does NOT: give the controller a
 * 0x0000xxxx address and the erase silently does nothing (BSY never asserts, so
 * the busy-wait falls straight through and the driver cheerfully reports
 * success), while a page-buffer store to the execution alias hangs.
 *
 * Every address this project handles is in the 0x0000xxxx space; the
 * translation is applied here, at the controller boundary, so no caller has to
 * remember it. */
#define PROG(addr)   ((addr) + FLASH_BASE)

extern uint32_t _ramfunc_lma, _ramfunc_vma, _ramfunc_vma_end;

void ramfunc_init(void)
{
    uint32_t *src = &_ramfunc_lma;
    uint32_t *dst = &_ramfunc_vma;
    while (dst < &_ramfunc_vma_end)
        *dst++ = *src++;
    __asm volatile ("fence.i");
}

/* ---- interrupt masking -------------------------------------------------------
 * Putting the flash routines in SRAM is only half the problem. The INTERRUPT
 * VECTOR TABLE still lives in flash, so an interrupt taken while the flash
 * controller is busy makes the core fetch a vector from flash mid-operation -
 * the very stall .ramfunc exists to avoid. On this board the USB peripheral
 * interrupts constantly, so this is not a rare race: it reproduces every time.
 *
 * Interrupts are therefore masked around each individual page operation, and
 * released between pages so USB gets serviced during a long erase.
 *
 * CSR 0x800 is the WCH global interrupt register; bits 3 and 7 mirror
 * mstatus.MIE / MPIE. Same register the SPL's __disable_irq() writes.
 */
RAMFUNC static uint32_t irq_off(void)
{
    uint32_t old;
    __asm volatile ("csrr %0, 0x800" : "=r" (old));
    __asm volatile ("csrw 0x800, %0" : : "r" (old & ~0x88u));
    return old;
}

RAMFUNC static void irq_restore(uint32_t old)
{
    __asm volatile ("csrw 0x800, %0" : : "r" (old));
}

/* ---- bounds ---------------------------------------------------------------
 * Deliberately duplicated at the lowest level. The protocol layer checks too,
 * but this is the check that actually protects the bootloader.
 */
RAMFUNC static int range_ok(uint32_t addr, uint32_t len, flash_region_t region)
{
    uint32_t lo = (region == FLASH_REGION_BOOT) ? 0u : CHGAME_APP_START;
    uint32_t hi = (region == FLASH_REGION_BOOT) ? CHGAME_APP_START : CHGAME_FLASH_SIZE;

    if (len == 0u)                 return 0;
    if (addr < lo)                 return 0;
    if (addr > hi)                 return 0;   /* catches addr beyond the region */
    if (len > hi - addr)           return 0;   /* no overflow: hi >= addr here    */
    return 1;
}

/* ---- RAM-resident primitives ---------------------------------------------- */

RAMFUNC static void fl_unlock(void)
{
    FLASH->KEYR     = FKEY1;  FLASH->KEYR     = FKEY2;
    FLASH->MODEKEYR = FKEY1;  FLASH->MODEKEYR = FKEY2;
}

RAMFUNC static void fl_lock(void)
{
    FLASH->CTLR |= CR_FLOCK;
}

RAMFUNC static void fl_erase(uint32_t addr)
{
    FLASH->CTLR |= CR_PAGE_ER;
    FLASH->ADDR  = PROG(addr);
    FLASH->CTLR |= CR_STRT;
    while (FLASH->STATR & SR_BSY) { }
    FLASH->CTLR &= ~CR_PAGE_ER;
}

RAMFUNC static void fl_program(uint32_t addr, const uint32_t *words)
{
    /* Reset the page buffer, load 64 words, then commit the page. */
    FLASH->CTLR |= CR_PAGE_PG;
    FLASH->CTLR |= CR_BUF_RST;
    while (FLASH->STATR & SR_BSY) { }
    FLASH->CTLR &= ~CR_PAGE_PG;

    for (uint32_t i = 0; i < CHGAME_PAGE_SIZE / 4u; i++) {
        FLASH->CTLR |= CR_PAGE_PG;
        *(volatile uint32_t *)(PROG(addr) + i * 4u) = words[i];
        FLASH->CTLR |= CR_BUF_LOAD;
        while (FLASH->STATR & SR_BSY) { }
        FLASH->CTLR &= ~CR_PAGE_PG;
    }

    FLASH->CTLR |= CR_PAGE_PG;
    FLASH->ADDR  = PROG(addr);
    FLASH->CTLR |= CR_STRT;
    while (FLASH->STATR & SR_BSY) { }
    FLASH->CTLR &= ~CR_PAGE_PG;
}

/* ---- public API ------------------------------------------------------------ */

RAMFUNC int flash_erase_page(uint32_t addr, flash_region_t region)
{
    uint32_t irq;

    if (addr % CHGAME_PAGE_SIZE)                          return FLASH_ERR_ALIGN;
    if (!range_ok(addr, CHGAME_PAGE_SIZE, region))        return FLASH_ERR_RANGE;

    irq = irq_off();
    fl_unlock();
    fl_erase(addr);
    fl_lock();
    irq_restore(irq);
    return FLASH_OK;
}

RAMFUNC int flash_write_page(uint32_t addr, const uint8_t *data, flash_region_t region)
{
    uint32_t irq;
    /* The page buffer is loaded a word at a time, so the source must be word
       aligned. The protocol layer always stages through an aligned page buffer. */
    uint32_t buf[CHGAME_PAGE_SIZE / 4u];
    const uint8_t *s = data;

    if (addr % CHGAME_PAGE_SIZE)                          return FLASH_ERR_ALIGN;
    if (!range_ok(addr, CHGAME_PAGE_SIZE, region))        return FLASH_ERR_RANGE;

    for (uint32_t i = 0; i < CHGAME_PAGE_SIZE / 4u; i++) {
        buf[i] = (uint32_t)s[0] | ((uint32_t)s[1] << 8)
               | ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24);
        s += 4;
    }

    irq = irq_off();
    fl_unlock();
    fl_erase(addr);
    fl_program(addr, buf);
    fl_lock();
    irq_restore(irq);

    /* Read back immediately. A silent programming failure that only shows up at
       the final image CRC wastes an entire upload; catching it per page tells
       the host exactly which page failed. */
    for (uint32_t i = 0; i < CHGAME_PAGE_SIZE / 4u; i++)
        if (*(volatile uint32_t *)(addr + i * 4u) != buf[i])
            return FLASH_ERR_VERIFY;

    return FLASH_OK;
}

int flash_erase_range(uint32_t addr, uint32_t len, flash_region_t region)
{
    if (addr % CHGAME_PAGE_SIZE)      return FLASH_ERR_ALIGN;
    if (!range_ok(addr, len, region)) return FLASH_ERR_RANGE;

    for (uint32_t a = addr; a < addr + len; a += CHGAME_PAGE_SIZE) {
        int rc = flash_erase_page(a, region);
        if (rc != FLASH_OK) return rc;
    }
    return FLASH_OK;
}

/* ---- developer self-update --------------------------------------------------
 * Entirely RAM-resident and calls nothing that is not. From the first erase
 * until the reset there is no valid code in the boot region, so a single call
 * into flash here would be fatal.
 */
RAMFUNC void flash_selfupdate(uint32_t src, uint32_t len)
{
    uint32_t buf[CHGAME_PAGE_SIZE / 4u];

    /* Interrupts off and staying off. A USB interrupt vectoring through a
       vector table that is mid-erase would jump into erased flash. */
    __asm volatile ("csrw 0x800, %0" : : "r" (0x6000));

    fl_unlock();

    /* Erase the whole reservation, not just the part the new image occupies, so
       no fragment of the previous bootloader survives past its replacement. */
    for (uint32_t a = 0; a < CHGAME_BOOT_SIZE; a += CHGAME_PAGE_SIZE)
        fl_erase(a);

    for (uint32_t off = 0; off < len; off += CHGAME_PAGE_SIZE) {
        for (uint32_t i = 0; i < CHGAME_PAGE_SIZE / 4u; i++) {
            uint32_t sa = src + off + i * 4u;
            buf[i] = (off + i * 4u < len) ? *(volatile uint32_t *)sa : 0xFFFFFFFFu;
        }
        fl_program(off, buf);
    }

    fl_lock();

    /* Straight into reset: the code that called us no longer exists. */
    NVIC->CFGR = NVIC_KEY3 | (1u << 7);
    for (;;) { }
}
