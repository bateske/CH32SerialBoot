#include "bootreq.h"

volatile chgame_bootreq_t chgame_bootreq __attribute__((section(".boot_magic"), used));

int bootreq_pending(void)
{
    return chgame_bootreq.magic   == CHGAME_BOOT_MAGIC &&
           chgame_bootreq.inverse == (uint32_t)~CHGAME_BOOT_MAGIC;
}

void bootreq_set(void)
{
    chgame_bootreq.magic   = CHGAME_BOOT_MAGIC;
    chgame_bootreq.inverse = (uint32_t)~CHGAME_BOOT_MAGIC;
}

void bootreq_clear(void)
{
    chgame_bootreq.magic   = 0;
    chgame_bootreq.inverse = 0;
}

uint32_t bootcount_bump(void)
{
    uint32_t n = chgame_bootreq.boot_count;

    if (n != (uint32_t)~chgame_bootreq.boot_count_inv || n == 0 || n > 0xFFFF)
        n = 0;   /* no trustworthy previous value: cold boot */

    n++;
    chgame_bootreq.boot_count     = n;
    chgame_bootreq.boot_count_inv = ~n;
    return n;
}
