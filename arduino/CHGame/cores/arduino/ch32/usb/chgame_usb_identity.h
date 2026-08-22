/*
 * CHGame USB identity — SHARED by the bootloader and the Arduino core.
 *
 * vendor/usbcdc/wch_usbcdc_config.h is replaced by a one-line forwarder to this
 * file. It CANNOT be shadowed via the -I order: the vendored headers include it
 * with quotes, and a quoted include always searches the including file's own
 * directory first, so the vendor copy would always win. Both images MUST
 * compile against this same header.
 *
 * WHY IDENTICAL IN BOTH MODES:
 * Windows keys a CDC COM port assignment on VID + PID + serial number +
 * interface number. Presenting the same descriptors in application mode and
 * bootloader mode means the device keeps ONE COM port across the
 * app -> bootloader -> app transition. That removes the transient-port problem
 * that makes Leonardo-style uploads fragile, and it means Web Serial keeps its
 * permission grant across the switch.
 *
 * The cost is that the two modes are indistinguishable from the descriptors
 * alone, so mode is discovered by protocol probe (HELLO) instead. That is a
 * deliberate trade: a reliable port is worth more than a self-describing one.
 *
 * If Windows ever does split them, the fallback is Leonardo-style separate PIDs;
 * the uploader already has to handle a port transition either way.
 */
#pragma once

/* CH32X035 Electronic Signature (ESIG) — 96-bit device unique ID. */
#define CH32X035_ESIG_UNIID1    0x1FFFF7E8
#define CH32X035_ESIG_UNIID2    0x1FFFF7EC
#define CH32X035_ESIG_UNIID3    0x1FFFF7F0

/* Strings. Keep these stable: changing them after devices ship can cause
 * Windows to re-enumerate and reassign COM ports on existing units. */
#define WCH_USBCDC_MANUF_STR            "CHGame"
#define WCH_USBCDC_PROD_STR             "CHGame"
#define WCH_USBCDC_INTERF_STR           "CHGame Serial"
#define WCH_USBCDC_SERIAL_PREFIX        "CG"

#define STR_LEN(s) (sizeof(s) - 1)

#define WCH_USBCDC_MANUF_LEN            STR_LEN(WCH_USBCDC_MANUF_STR)
#define WCH_USBCDC_PROD_LEN             STR_LEN(WCH_USBCDC_PROD_STR)
#define WCH_USBCDC_INTERF_LEN           STR_LEN(WCH_USBCDC_INTERF_STR)
#define WCH_USBCDC_SERIAL_PREFIX_LEN    STR_LEN(WCH_USBCDC_SERIAL_PREFIX)
#define WCH_USBCDC_UID_HEX_CHARS        24
#define WCH_USBCDC_SERIAL_LEN           (WCH_USBCDC_SERIAL_PREFIX_LEN + WCH_USBCDC_UID_HEX_CHARS)

/*
 * DEVELOPMENT IDENTIFIERS ONLY.
 *
 * 0x16C0:0x27DD is the shared V-USB / Van Ooijen CDC-ACM pair. It is fine on the
 * bench and it is what this hardware already enumerated with, but it MUST NOT
 * ship: it is shared with unrelated devices, so port auto-discovery cannot rely
 * on it and collisions are possible on a user's machine.
 *
 * Before shipping, obtain a real allocation - pid.codes issues free PIDs under
 * VID 0x1209 for open-source projects - and change these two lines only.
 */
#define WCH_USBCDC_VENDOR_ID        0x16C0
#define WCH_USBCDC_PRODUCT_ID       0x27DD
#define WCH_USBCDC_DEVICE_VERSION   0x0100
#define WCH_USBCDC_LANGUAGE         0x0409
#define WCH_USBCDC_MAX_POWER_mA     500

#ifndef WCH_USBCDC_DEFAULT_FIFO_SIZE
#define WCH_USBCDC_DEFAULT_FIFO_SIZE 128
#endif
