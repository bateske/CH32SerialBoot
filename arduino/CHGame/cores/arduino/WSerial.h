#ifndef WIRING_SERIAL_H
#define WIRING_SERIAL_H

#include "variant.h"
#include "HardwareSerial.h"

#if defined(USE_TINYUSB)
#include "Adafruit_USBD_CDC.h"
#define Serial SerialTinyUSB
#endif

/* CHGAME: Serial is the native USB CDC port, matching a Leonardo rather than a
 * board whose Serial is a UART. Any physical UART is exposed as Serial1.
 *
 * Defined before the UART block below so the "#if !defined(Serial)" guards
 * there leave it alone. */
#if defined(USE_CHGAME_USB_CDC)
#include "CHGameUSBSerial.h"
#define Serial USBSerial
#elif defined(USE_CHGAME_USB_BOOTONLY)
/* Tools -> USB -> "Upload only" has no Serial, and a sketch that uses it must
 * fail to build rather than lose its output. Defining Serial is also what stops
 * the UART block below from quietly mapping it to Serial1 (Peripherals -> Full)
 * -- the same name talking to a different port is worse than an error. The
 * type is never defined, so any use names it in the compiler error. */
struct Serial_is_not_available_with_Tools_USB_Upload_only;
#define Serial (Serial_is_not_available_with_Tools_USB_Upload_only())
#define USBSerial Serial   /* the upstream name some older sketches use */
#endif

#if defined(UART_MODULE_ENABLED) && !defined(UART_MODULE_ONLY)

  #if !defined(HWSERIAL_NONE) && defined(SERIAL_UART_INSTANCE)

    #if SERIAL_UART_INSTANCE == 1
      #define ENABLE_HWSERIAL1
      #if !defined(Serial)
        #define Serial Serial1
        // #define serialEvent serialEvent1  //reserved
      #endif  
    #elif SERIAL_UART_INSTANCE == 2
      #define ENABLE_HWSERIAL2
      #if !defined(Serial)
        #define Serial Serial2
        // #define serialEvent serialEvent2
      #endif
    #elif SERIAL_UART_INSTANCE == 3
      #define ENABLE_HWSERIAL3
      #if !defined(Serial)
        #define Serial Serial3
        // #define serialEvent serialEvent3
      #endif
    #elif SERIAL_UART_INSTANCE == 4
      #define ENABLE_HWSERIAL4
      #if !defined(Serial)
        #define Serial Serial4
        // #define serialEvent serialEvent4
      #endif
    #elif SERIAL_UART_INSTANCE == 5
      #define ENABLE_HWSERIAL5
      #if !defined(Serial)
        #define Serial Serial5
        // #define serialEvent serialEvent5
      #endif
    #elif SERIAL_UART_INSTANCE == 6
      #define ENABLE_HWSERIAL6
      #if !defined(Serial)
        #define Serial Serial6
        // #define serialEvent serialEvent6
      #endif
    #elif SERIAL_UART_INSTANCE == 7
      #define ENABLE_HWSERIAL7
      #if !defined(Serial)
        #define Serial Serial7
        // #define serialEvent serialEvent7
      #endif
    #elif SERIAL_UART_INSTANCE == 8
      #define ENABLE_HWSERIAL8
      #if !defined(Serial)
        #define Serial Serial8
        // #define serialEvent serialEvent8
      #endif
    #else
      #if !defined(Serial)
        #warning "No generic 'Serial' defined!"
      #endif

    #endif /* SERIAL_UART_INSTANCE == x */

  #endif /* !HWSERIAL_NONE && SERIAL_UART_INSTANCE */


  #if defined(ENABLE_HWSERIAL1)
    #if defined(USART1_BASE)
      #define HAVE_HWSERIAL1
    #endif
  #endif
  #if defined(ENABLE_HWSERIAL2)
    #if defined(USART2_BASE)
      #define HAVE_HWSERIAL2
    #endif
  #endif
  #if defined(ENABLE_HWSERIAL3)
    #if defined(USART3_BASE)
      #define HAVE_HWSERIAL3
    #endif
  #endif
  #if defined(ENABLE_HWSERIAL4)
    #if defined(USART4_BASE) || defined(UART4_BASE)
      #define HAVE_HWSERIAL4
    #endif
  #endif
  #if defined(ENABLE_HWSERIAL5)
    #if defined(UART5_BASE)
      #define HAVE_HWSERIAL5
    #endif
  #endif
  #if defined(ENABLE_HWSERIAL6)
    #if defined(UART6_BASE)
      #define HAVE_HWSERIAL6
    #endif
  #endif
  #if defined(ENABLE_HWSERIAL7)
    #if defined(UART7_BASE)
      #define HAVE_HWSERIAL7
    #endif
  #endif
  #if defined(ENABLE_HWSERIAL8)
    #if defined(UART8_BASE)
      #define HAVE_HWSERIAL8
    #endif
  #endif

#endif

#endif /* WIRING_SERIAL_H */
