#pragma once

#include <Arduino.h>

namespace wch { namespace usbcdc {

class USBSerialCH32 : public Stream {
public:
  USBSerialCH32();

  bool begin(uint16_t rxTxFifoSize = 0);
  void end();

  int available() override;
  int read() override;
  int peek() override;
  void flush() override;

  size_t write(uint8_t c) override;
  using Print::write;

  // Control/status
  bool dtr() const;
  bool rts() const;
  uint32_t baud() const;
  bool setBaud(uint32_t baud);

  // Wait for host connection with optional timeout (ms)
  bool waitForPC(uint32_t timeoutMs = 0);

  // Internal pump (ISR-safe lightweight)
  void _irqHandler();

private:
  bool _initialized;
  uint16_t _fifoSize;
};

extern USBSerialCH32 USBSerial;

}} // namespace wch::usbcdc

/* CHGAME: hoist the instance to global scope.
 *
 * Upstream keeps USBSerial inside wch::usbcdc and expects the sketch to add a
 * using-declaration. On this board Serial IS this object (see WSerial.h), so it
 * has to be visible with no cooperation from the sketch - an empty sketch must
 * still link. */
using wch::usbcdc::USBSerialCH32;
using wch::usbcdc::USBSerial;