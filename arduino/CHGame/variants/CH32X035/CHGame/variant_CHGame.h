/*
 * CHGame board variant  -  CH32X035G8U6, QFN28.
 *
 * Derived from the stock CH32X035G8U variant. Pin roles come from the board
 * netlist cross-checked against the package pin table and this core's own
 * PeripheralPins.c; see docs/hardware-pinmap.md for the derivation.
 *
 * TWO STOCK DEFAULTS ARE ACTIVELY DANGEROUS ON THIS BOARD and are overridden
 * below:
 *
 *   PIN_WIRE_SDA / PIN_WIRE_SCL defaulted to PC17 / PC16 - those are the USB
 *   D+ / D- lines here. Using Wire with the stock mapping would drive the USB
 *   pins and kill enumeration.
 *
 *   PIN_SERIAL_TX / PIN_SERIAL_RX defaulted to PB10 / PB11 - those are the
 *   BUZZER and the microSD chip select here. Using Serial1 with the stock
 *   mapping would sound the buzzer with UART traffic and fight the SD card.
 *
 * Serial is USB CDC (see WSerial.h). Serial1 is USART2 on the H1 header.
 */
#pragma once

/* ENABLE Peripherals */
#define                         ADC_MODULE_ENABLED
#define                         UART_MODULE_ENABLED
#define                         SPI_MODULE_ENABLED
#define                         I2C_MODULE_ENABLED
#define                         TIM_MODULE_ENABLED

/* CH32VX035G8 Pins */
#define PA0                     PIN_A0
#define PA1                     PIN_A1
#define PA2                     PIN_A2
#define PA3                     PIN_A3
#define PA4                     PIN_A4
#define PA5                     PIN_A5
#define PA6                     PIN_A6
#define PA7                     PIN_A7
#define PB0                     PIN_A8
#define PB1                     PIN_A9
#define PB3                     10
#define PB4                     11
#define PB6                     12
#define PB7                     13
#define PB8                     14
#define PB9                     15
#define PB10                    16
#define PB11                    17
#define PB12                    18
#define PC0                     PIN_A10
#define PC3                     PIN_A13
#define PC14                    21
#define PC15                    22
#define PC16                    23
#define PC17                    24
#define PC18                    25
#define PC19                    26

// Alternate pins number
#define PA0_ALT1                (PA0  | ALT1)
#define PA1_ALT1                (PA1  | ALT1)
#define PA2_ALT1                (PA2  | ALT1)
#define PA3_ALT1                (PA3  | ALT1)
#define PA4_ALT1                (PA4  | ALT1)
#define PA5_ALT1                (PA5  | ALT1)
#define PA6_ALT1                (PA6  | ALT1)
#define PA7_ALT1                (PA7  | ALT1)
#define PB0_ALT1                (PB0  | ALT1)
#define PB1_ALT1                (PB1  | ALT1)
#define PC0_ALT1                (PC0  | ALT1)
#define PC3_ALT1                (PC3  | ALT1)



#define NUM_DIGITAL_PINS        27
#define NUM_ANALOG_INPUTS       14       
#define ADC_RESOLUTION          12


// On-board LED pin number

/* ---- CHGame board pin roles -------------------------------------------------
 * QFN28 pin numbers in comments, from the netlist.
 */

/* Status LED - PB9 -> R9 -> LED2 -> GND, so ACTIVE HIGH. */
#define LED_BUILTIN             PB9     /* pin 20 */
#define PIN_LED                 PB9

/* Piezo buzzer. */
#define PIN_BUZZER              PB10    /* pin 21 */

/* Game buttons. All are switch-to-GND, so they need INPUT_PULLUP and read
 * LOW when pressed. */
#define PIN_BTN_UP              PB4     /* pin 15 */
#define PIN_BTN_DOWN            PC14    /* pin 28 */
#define PIN_BTN_LEFT            PB3     /* pin 14 */
#define PIN_BTN_RIGHT           PC15    /* pin  1 */
#define PIN_BTN_A               PB1     /* pin 16 */
#define PIN_BTN_B               PB6     /* pin 17 */
#define PIN_BTN_SELECT          PB7     /* pin 18 */
#define PIN_BTN_START           PB8     /* pin 19 */

/* ST7735 128x128 display and microSD share hardware SPI1. */
#define PIN_LCD_CS              PA4     /* pin  9  (also SPI1 NSS) */
#define PIN_LCD_DC              PB0     /* pin 13 */
#define PIN_LCD_RST             PB12    /* pin 23 */
#define PIN_SD_CS               PB11    /* pin 22 */

/* Expansion header H1. */
#define PIN_GPIO1               PC0     /* pin  3, H1.8 */
#define PIN_GPIO2               PC3     /* pin  4, H1.7 */
#define PIN_GPIO3               PA0     /* pin  5, H1.6 */
#define PIN_GPIO4               PA1     /* pin  6, H1.5 */

#ifndef LED_BUILTIN
  #define LED_BUILTIN           PB9
#endif



// On-board user button
#ifndef USER_BTN
  #define USER_BTN              PNUM_NOT_DEFINED
#endif

// SPI definitions
#ifndef PIN_SPI_SS
  #define PIN_SPI_SS            PA4
#endif
#ifndef PIN_SPI_MOSI
  #define PIN_SPI_MOSI          PA7
#endif
#ifndef PIN_SPI_MISO
  #define PIN_SPI_MISO          PA6
#endif
#ifndef PIN_SPI_SCK
  #define PIN_SPI_SCK           PA5
#endif

// I2C definitions
/* CHGAME: stock default was PC17 - that is USB D+ on this board. */
#ifndef PIN_WIRE_SDA
  #define PIN_WIRE_SDA          PC18
#endif
/* CHGAME: stock default was PC16 - that is USB D- on this board. */
#ifndef PIN_WIRE_SCL
  #define PIN_WIRE_SCL          PC19
#endif

// Timer Definitions
#ifndef TIMER_TONE
  #define TIMER_TONE            TIM3
#endif
#ifndef TIMER_SERVO
  #define TIMER_SERVO           TIM2
#endif


// UART Definitions
#ifndef SERIAL_UART_INSTANCE
  #define SERIAL_UART_INSTANCE  1
#endif
// Default pin used for generic 'Serial' instance
// Mandatory for Firmata
/* CHGAME: stock default was PB11 - that is the microSD chip select here.
 * Serial1 is USART2 on the H1 expansion header instead. */
#ifndef PIN_SERIAL_RX
  #define PIN_SERIAL_RX         PA3
#endif
/* CHGAME: stock default was PB10 - that is the buzzer here. */
#ifndef PIN_SERIAL_TX
  #define PIN_SERIAL_TX         PA2
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#ifdef __cplusplus
  // These serial port names are intended to allow libraries and architecture-neutral
  // sketches to automatically default to the correct port name for a particular type
  // of use.  For example, a GPS module would normally connect to SERIAL_PORT_HARDWARE_OPEN,
  // the first hardware serial port whose RX/TX pins are not dedicated to another use.
  //
  // SERIAL_PORT_MONITOR        Port which normally prints to the Arduino Serial Monitor
  //
  // SERIAL_PORT_USBVIRTUAL     Port which is USB virtual serial
  //
  // SERIAL_PORT_LINUXBRIDGE    Port which connects to a Linux system via Bridge library
  //
  // SERIAL_PORT_HARDWARE       Hardware serial port, physical RX & TX pins.
  //
  // SERIAL_PORT_HARDWARE_OPEN  Hardware serial ports which are open for use.  Their RX & TX
  //                            pins are NOT connected to anything by default.
  #ifndef SERIAL_PORT_MONITOR
    #define SERIAL_PORT_MONITOR   Serial
  #endif
  #ifndef SERIAL_PORT_HARDWARE
    #define SERIAL_PORT_HARDWARE  Serial
  #endif
#endif


