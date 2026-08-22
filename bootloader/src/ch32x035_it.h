/* CHGame replacement for the WCH ch32x035_it.h.
 *
 * The stock header pulls in WCH's debug.h, which drags printf and a UART
 * console into the image. The bootloader has an 8 KB budget and no console, so
 * this deliberately empty header shadows it via the -I search order. Interrupt
 * handlers live in fault.c and in the USB driver. */
#ifndef __CH32X035_IT_H
#define __CH32X035_IT_H
#endif
