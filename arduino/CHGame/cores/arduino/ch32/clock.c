/**
 *******************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * All rights reserved.
 *
 * This software component is licensed by WCH under BSD 3-Clause license,
 * the "License"; You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                        opensource.org/licenses/BSD-3-Clause
 *
 *******************************************************************************
 */
#include "backup.h"
#include "clock.h"
#include "core_riscv_ch32yyxx.h"
#include "ch32yyxx_rcc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TICK_FREQ_1KHz    1L
// #define TICK_FREQ_100Hz   10L
// #define TICK_FREQ_10Hz    100L 


__IO uint64_t msTick=0;
WEAK uint64_t GetTick(void)
{
  return msTick;
}


void osSystickHandler() __attribute__((weak, alias("noOsSystickHandler")));
void noOsSystickHandler()
{

}

/**
  * @brief  Function called wto read the current millisecond
  * @param  None
  * @retval None
  */
uint32_t getCurrentMillis(void)
{
  return GetTick();
}



#if defined(CH32V20x) || defined(CH32V30x) || defined(CH32V30x_C) || defined(CH32V00x) || defined(CH32X035) || defined(CH32L10x) || defined(CH32VM00X)

/*
 * systick_init() runs SysTick as an UP counter with auto-reload: CNT climbs
 * from 0 to CMP at HCLK, then reloads to 0 and sets SR.CNTIF, and the SysTick
 * interrupt advances msTick.  The elapsed part of the current millisecond is
 * therefore CNT / (CMP + 1).  The old code used (CMP + 1 - CNT), the formula
 * for a down-counting Cortex-M SysTick, which made micros() run backwards
 * inside every millisecond and jump forward by ~2 ms at each tick.
 *
 * SR.CNTIF stays set from the reload until SysTick_Handler clears it.  If it
 * is set, the counter has already wrapped but msTick has not caught up yet
 * (interrupts disabled, or we were called from an ISR that outranks SysTick),
 * so that millisecond is added here.  The reads are retried until msTick and
 * the flag are stable around the CNT sample, so a tick landing mid-read can
 * neither be missed nor counted twice.  A sample of exactly CMP with the flag
 * set is the match cycle itself, before the reload, and is not bumped.
 */
uint32_t getCurrentMicros(void)
{
  const uint32_t tms = (uint32_t)SysTick->CMP + 1;   /* SysTick ticks per ms */
  uint32_t m, u, sr0, sr1;

  do {
    m   = (uint32_t)GetTick();
    sr0 = SysTick->SR & 1u;
    u   = (uint32_t)SysTick->CNT;
    sr1 = SysTick->SR & 1u;
  } while (m != (uint32_t)GetTick() || sr0 != sr1);

  if (sr1 && u < tms - 1) {
    m++;                                  /* wrapped, tick not counted yet */
  }
  return m * 1000u + (u * 1000u) / tms;
}


/*********************************************************************
 * @fn      SysTick_Handler
 *
 * @brief   This function handles systick interrupt.
 *
 * @return  none
 */
void SysTick_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SysTick_Handler(void)
{
  /*
   * Clear the flag and advance the tick as one unit.  Interrupt nesting is
   * enabled, so a higher-priority ISR could otherwise land between the two
   * and see the wrap both in SR.CNTIF and in msTick, and getCurrentMicros()
   * would count that millisecond twice.
   */
  uint32_t mstatus;
  __asm volatile ("csrrci %0, mstatus, 0x8" : "=r" (mstatus) : : "memory");
  SysTick->SR = 0;
  msTick += TICK_FREQ_1KHz;
  __asm volatile ("csrw mstatus, %0" : : "r" (mstatus) : "memory");
  osSystickHandler();
}

#endif





// for 10x serils (Qingke V3A) 
#if defined (CH32V10x)

#define SYSTICK_CNTL    (0xE000F004)   
#define SYSTICK_CNTH    (0xE000F008)
#define SYSTICK_CMPL    (0xE000F00C)
#define SYSTICK_CMPH    (0xE000F010)

/* V3A SysTick counts up from 0 and is reset by SysTick_Handler, so the
 * elapsed part of the millisecond is CNT / (CMP + 1), as above. */
uint32_t getCurrentMicros(void)
{
  
  uint64_t m0 = GetTick();
  uint64_t u0 = *((__IO uint32_t *)SYSTICK_CNTH);  
           u0 = (u0 << 32) + *((__IO uint32_t *)SYSTICK_CNTL);
  
  uint64_t m1 = GetTick();
  uint64_t u1 = *((__IO uint32_t *)SYSTICK_CNTH); //may be a interruption
           u1 = (u1 << 32) + *((__IO uint32_t *)SYSTICK_CNTL);

  uint64_t tms = *((__IO uint32_t *)SYSTICK_CMPH);
           tms = (tms << 32) + *((__IO uint32_t *)SYSTICK_CMPL) + 1;     

  if (m1 != m0) {
    return (m1 * 1000 + (u1 * 1000) / tms);
  } else {
    return (m0 * 1000 + (u0 * 1000) / tms);
  }
}



/*********************************************************************
 * @fn      SysTick_Handler
 *
 * @brief   This function handles systick interrupt.
 *
 * @return  none
 */
void SysTick_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SysTick_Handler(void)
{
  SysTick->CTLR=0;
  msTick+=TICK_FREQ_1KHz;
  SysTick->CNTL0=0;SysTick->CNTL1=0;SysTick->CNTL2=0;SysTick->CNTL3=0;
  SysTick->CNTH0=0;SysTick->CNTH1=0;SysTick->CNTH2=0;SysTick->CNTH3=0;
  SysTick->CTLR=0x1;
  osSystickHandler();
}

#endif




#ifdef __cplusplus
}
#endif


