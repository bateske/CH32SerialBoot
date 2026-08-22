#define ARDUINO_MAIN

#include "Arduino.h"
#include "debug.h"

#if defined(USE_CHGAME_USB_CDC)
#include "CHGameUSBSerial.h"
#endif


/*
 * \brief Main entry point of Arduino application
 */
int main( void )
{
    pre_init( );

#if defined(USE_CHGAME_USB_CDC)
    /* Bring USB CDC up BEFORE setup().
     *
     * This is what keeps an arbitrary sketch uploadable. The upload handshake
     * is detected in the USB interrupt, so the port has to exist and be
     * enumerated even for a sketch that never mentions Serial, never calls
     * Serial.begin(), or blocks forever in setup(). Requiring the sketch to
     * opt in would mean any sketch that forgot could only be recovered with
     * the BOOT button. */
    USBSerial.begin();
#endif

#if defined(USE_TINYUSB)
    if (TinyUSB_Device_Init) {
        TinyUSB_Device_Init(0);
    }
#endif
    setup( );
  
    do {
        loop( );
#if defined(USE_TINYUSB)
        if (TinyUSB_Device_Task) {
            TinyUSB_Device_Task();
        }
        if (TinyUSB_Device_FlushCDC) {
            TinyUSB_Device_FlushCDC();
        }
#endif
    } while (1);

    return 0;
}
