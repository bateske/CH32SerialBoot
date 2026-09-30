/* CHGAME: compiled only when the board's USB menu keeps USB. core.a is linked
   --whole-archive and USBFS_IRQHandler sits in the vector table, so this file
   would otherwise always be linked. */
#if defined(USE_CHGAME_USB_CDC) || defined(USE_CHGAME_USB_BOOTONLY)

#include "wch_usbcdc_internal.h"

typedef struct {
  uint32_t baudrate;
  uint8_t  stopbits;
  uint8_t  parity;
  uint8_t  databits;
} CDC_LINE_CODING_TYPE;

CDC_LINE_CODING_TYPE CDC_lineCoding = { .baudrate = 115200, .stopbits = 0, .parity = 0, .databits = 8 };

volatile uint8_t CDC_controlLineState = 0;
volatile uint8_t CDC_readByteCount = 0;
volatile uint8_t CDC_readPointer   = 0;
volatile uint8_t CDC_writePointer  = 0;
volatile uint8_t CDC_writeBusyFlag = 0;

#define SET_LINE_CODING           0x20
#define GET_LINE_CODING           0x21
#define SET_CONTROL_LINE_STATE    0x22

/* CHGAME: the Arduino-style upload handshake.
 *
 * The host opens the port at 1200 baud and then drops DTR (typically by simply
 * closing it). That combination is reserved as "please reboot into the
 * bootloader" and never occurs in normal use -- nothing talks to a USB CDC
 * device at 1200 baud, and the rate is ignored by the peripheral anyway.
 *
 * Detection lives HERE, in the control-request path, rather than in sketch
 * code, because it has to work for a sketch that never touches Serial and for
 * one stuck in a blocking loop. That is the difference between "uploads work"
 * and "uploads work as long as the sketch cooperates", and the latter is not
 * good enough: an empty sketch must stay uploadable.
 *
 * The flag is only ARMED here. Acting on it is deferred to the end of the USB
 * interrupt so the control transfer's status stage can complete first,
 * otherwise the host sees the request fail as the device vanishes. */
#define CDC_UPLOAD_TOUCH_BAUD     1200u
volatile uint8_t CDC_bootRequest = 0;

void CDC_init(void) { USB_init(); }

uint8_t CDC_available(void) { return CDC_readByteCount; }
uint8_t CDC_ready(void) { return !CDC_writeBusyFlag; }

void CDC_flush(void) {
  if(!CDC_writeBusyFlag && CDC_writePointer > 0) {
    USBFSD->UEP2_TX_LEN = CDC_writePointer;
    USBFSD->UEP2_CTRL_H = (USBFSD->UEP2_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_ACK;
    CDC_writeBusyFlag = 1;
    CDC_writePointer  = 0;
  }
}

void CDC_write(char c) {
  while(CDC_writeBusyFlag);
  wch_usbcdc_EP2_buffer[64 + CDC_writePointer++] = (unsigned char)c;
  /* CHGAME: upstream flushed here on EVERY byte, which pins throughput at
     roughly one byte per USB frame (~1 KB/s) and would make a 55 KB firmware
     upload take about a minute. Flush only on a full packet; callers that need
     a partial packet on the wire call CDC_flush() explicitly. */
  if(CDC_writePointer >= WCH_USBCDC_EP2_SIZE) CDC_flush();
}

/* CHGAME: non-blocking variants. The bootloader's protocol loop must never be
   wedged by a host that stopped reading, so it uses these and never the
   spin-waiting CDC_read()/CDC_write() above. */
uint8_t CDC_write_nb(char c) {
  if(CDC_writeBusyFlag) return 0;
  wch_usbcdc_EP2_buffer[64 + CDC_writePointer++] = (unsigned char)c;
  if(CDC_writePointer >= WCH_USBCDC_EP2_SIZE) CDC_flush();
  return 1;
}

int16_t CDC_read_nb(void) {
  int16_t data;
  if(!CDC_readByteCount) return -1;
  data = (int16_t)wch_usbcdc_EP2_buffer[CDC_readPointer++];
  if(--CDC_readByteCount == 0)
    USBFSD->UEP2_CTRL_H = (USBFSD->UEP2_CTRL_H & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_ACK;
  return data;
}

/* True once the host has selected a configuration, i.e. enumeration finished. */
uint8_t CDC_enumerated(void) { return USB_ENUM_OK; }

/* Host-asserted DTR, needed for the 1200-baud upload handshake. */
uint8_t CDC_dtr(void) { return (CDC_controlLineState & 0x01u) != 0u; }

uint32_t CDC_baud(void) { return CDC_lineCoding.baudrate; }

char CDC_read(void) {
  char data;
  while(!CDC_readByteCount);
  data = (char)wch_usbcdc_EP2_buffer[CDC_readPointer++];
  if(--CDC_readByteCount == 0)
    USBFSD->UEP2_CTRL_H = (USBFSD->UEP2_CTRL_H & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_ACK;
  return data;
}

void CDC_EP_init(void) {
  USBFSD->UEP1_DMA    = (uint32_t)wch_usbcdc_EP1_buffer;
  USBFSD->UEP2_DMA    = (uint32_t)wch_usbcdc_EP2_buffer;
  USBFSD->UEP4_1_MOD  = USBFS_UEP1_TX_EN;
  USBFSD->UEP2_3_MOD  = USBFS_UEP2_RX_EN | USBFS_UEP2_TX_EN;
  USBFSD->UEP1_CTRL_H = USBFS_UEP_AUTO_TOG | USBFS_UEP_T_RES_NAK;
  USBFSD->UEP2_CTRL_H = USBFS_UEP_AUTO_TOG | USBFS_UEP_R_RES_ACK | USBFS_UEP_T_RES_NAK;
  USBFSD->UEP1_TX_LEN = 0;
  USBFSD->UEP2_TX_LEN = 0;
  CDC_readByteCount   = 0;
  CDC_writeBusyFlag   = 0;
}

uint8_t CDC_control(void) {
  switch(USB_SetupReq) {
    case GET_LINE_CODING:
      USB_pDescr = (const uint8_t*)&CDC_lineCoding;
      USB_EP0_copyDescr(sizeof(CDC_lineCoding));
      if(USB_SetupLen > sizeof(CDC_lineCoding)) USB_SetupLen = sizeof(CDC_lineCoding);
      return (uint8_t)USB_SetupLen;
    case SET_CONTROL_LINE_STATE:
      CDC_controlLineState = wch_usbcdc_EP0_buffer[2];
      /* CHGAME: 1200 baud + DTR deasserted == upload request. */
      if (CDC_lineCoding.baudrate == CDC_UPLOAD_TOUCH_BAUD &&
          (CDC_controlLineState & 0x01u) == 0u) {
        CDC_bootRequest = 1;
      }
      return 0;
    case SET_LINE_CODING:
      return 0;
    default:
      return 0xff;
  }
}

void CDC_EP0_OUT(void) {
  uint8_t i, len;
  if(USB_SetupReq == SET_LINE_CODING) {
    len = USBFSD->RX_LEN;
    for(i=0; i<((sizeof(CDC_lineCoding)<=len)?sizeof(CDC_lineCoding):len); i++)
      ((uint8_t*)&CDC_lineCoding)[i] = wch_usbcdc_EP0_buffer[i];
    USB_SetupLen = 0;
  }
  USBFSD->UEP0_CTRL_H = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK | USBFS_UEP_R_RES_ACK;
}

void CDC_EP2_IN(void) {
  CDC_writeBusyFlag = 0;
  USBFSD->UEP2_CTRL_H = (USBFSD->UEP2_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_NAK;
}

void CDC_EP2_OUT(void) {
  if((USBFSD->INT_FG & USBFS_U_TOG_OK) && USBFSD->RX_LEN) {
    USBFSD->UEP2_CTRL_H = (USBFSD->UEP2_CTRL_H & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_NAK;
    CDC_readByteCount   = USBFSD->RX_LEN;
    CDC_readPointer     = 0;
  }
}

#endif /* USE_CHGAME_USB_CDC || USE_CHGAME_USB_BOOTONLY */
