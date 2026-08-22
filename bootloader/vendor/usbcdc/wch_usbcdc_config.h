/* CHGAME: contents replaced.
 *
 * Upstream shipped Jobit's identity strings and VID/PID here. CHGame needs one
 * identity shared by the bootloader and the application, so the real definitions
 * live in shared/chgame_usb_identity.h and this file just forwards to them.
 *
 * This has to be a replacement rather than an -I shadow: wch_usbcdc_internal.h
 * includes "wch_usbcdc_config.h" with quotes, and a quoted include searches the
 * including file's own directory first regardless of -I order. Shadowing looked
 * like it worked and silently did nothing - the device kept enumerating with the
 * old serial number prefix, which is how this was caught.
 */
#pragma once
#include "chgame_usb_identity.h"
