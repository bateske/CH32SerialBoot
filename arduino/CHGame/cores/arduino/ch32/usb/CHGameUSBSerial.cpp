#include "CHGameUSBSerial.h"
#include "wch_usbcdc_config.h"

// Pull in WCH device headers from the installed core
extern "C" {
#include <ch32x035.h>
}

extern "C" {
  void USB_init(void);
  void USB_EP0_copyDescr(uint8_t len);
  void CDC_init(void);
  void CDC_flush(void);
  char CDC_read(void);
  void CDC_write(char c);
  uint8_t CDC_available(void);
  uint8_t CDC_ready(void);
  uint8_t CDC_write_nb(char c);
  uint8_t CDC_enumerated(void);
  extern volatile uint8_t CDC_controlLineState;
  typedef struct {
    uint32_t baudrate; uint8_t stopbits; uint8_t parity; uint8_t databits;
  } CDC_LINE_CODING_TYPE;
  extern CDC_LINE_CODING_TYPE CDC_lineCoding;
}

/* How long write() will wait for the host to drain the IN endpoint before it
 * gives up and drops the byte. Bounded on purpose: see write() below. Override
 * with -DCHGAME_USB_TX_TIMEOUT_MS=n if a sketch would rather stall than lose
 * output. */
#ifndef CHGAME_USB_TX_TIMEOUT_MS
#define CHGAME_USB_TX_TIMEOUT_MS 25u
#endif

using namespace wch::usbcdc;

USBSerialCH32::USBSerialCH32() : _initialized(false), _fifoSize(WCH_USBCDC_DEFAULT_FIFO_SIZE) {}

/* Bring the USB device up and return immediately.
 *
 * This used to detach, delay(100), attach, then delay(1000) "for enumeration".
 * None of that was doing what it claimed. Enumeration is handled entirely in
 * USBFS_IRQHandler -- USB_EP0_SETUP/IN/OUT are only ever reached from the ISR,
 * and USB_ENUM_OK is set there on SET_CONFIGURATION -- so the main loop
 * contributes nothing to it. Blocking here and running setup() are equally good
 * places for the CPU to be while the host enumerates; the difference is that
 * blocking cost every sketch 1.1 s before it could touch the display.
 *
 * The same goes for the upload handshake: the 1200-baud touch is detected in
 * the control path and acted on at the end of the interrupt, deliberately so
 * that uploads never depend on the sketch reaching loop(). Returning early here
 * cannot make a board harder to recover.
 *
 * The old detach + delay(100) was for the host to notice a disconnect before we
 * re-attach. It is redundant: on a cold boot the bootloader never brought USB
 * up at all, and after an upload proto_shutdown() already drops the pull-up and
 * spins 30 ms before jumping here. USB_init() also resets the core itself. */
bool USBSerialCH32::begin(uint16_t rxTxFifoSize) {
  if (rxTxFifoSize) _fifoSize = rxTxFifoSize;
  if (_initialized) return true;

  CDC_init();

  _initialized = true;
  return true;
}

void USBSerialCH32::end() {
  _initialized = false;
}

int USBSerialCH32::available() {
  return CDC_available();
}

int USBSerialCH32::read() {
  if (!CDC_available()) return -1;
  return (int)(uint8_t)CDC_read();
}

int USBSerialCH32::peek() {
  // Not supported in low-level; emulate via read/unread if needed. Return -1.
  return -1;
}

void USBSerialCH32::flush() {
  CDC_flush();
}

/* Write one byte, and never wedge the sketch doing it.
 *
 * The underlying CDC_write() spins on `while(CDC_writeBusyFlag)`, and that flag
 * is only cleared by CDC_EP2_IN -- which runs when the host polls the IN
 * endpoint. So on a board running from a battery or a wall wart, with no host
 * at all, the old path hung forever on the 65th byte of the first print in
 * setup(). That was true before this change too; it was merely hidden behind
 * the 1 s pad, which made it look like a rare bug instead of a guaranteed one.
 *
 * Now that setup() starts while enumeration is still in flight, this has to be
 * correct rather than lucky, so:
 *   - no host yet (or ever)  -> drop the byte, keep running
 *   - host present but stalled -> wait, but only up to a bound, then drop
 *
 * Dropping and reporting success is the right Arduino contract here: Print will
 * otherwise retry forever, and a game that stutters because someone closed the
 * serial monitor is a worse failure than a lost debug line. Sketches that would
 * rather block until the port is real should say so with waitForPC(). */
size_t USBSerialCH32::write(uint8_t c) {
  if (!CDC_enumerated()) return 1;

  uint32_t start = millis();
  while (!CDC_write_nb((char)c)) {
    if (!CDC_enumerated()) return 1;                       /* host went away */
    if (millis() - start >= CHGAME_USB_TX_TIMEOUT_MS) return 1;
  }

  // Immediate flush to send without batching
  CDC_flush();
  return 1;
}

bool USBSerialCH32::dtr() const { return (CDC_controlLineState & 0x01) != 0; }
bool USBSerialCH32::rts() const { return (CDC_controlLineState & 0x02) != 0; }
bool USBSerialCH32::enumerated() const { return CDC_enumerated() != 0; }
uint32_t USBSerialCH32::baud() const { return CDC_lineCoding.baudrate; }
bool USBSerialCH32::setBaud(uint32_t baud) {
  CDC_lineCoding.baudrate = baud;
  return true;
}

bool USBSerialCH32::waitForPC(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (!dtr()) {
    if (timeoutMs && (millis() - start >= timeoutMs)) return false;
    delay(1);
  }
  return true;
}

void USBSerialCH32::_irqHandler() {
  // ISR is handled in C code
}

wch::usbcdc::USBSerialCH32 wch::usbcdc::USBSerial;
