#ifndef CHGAME_PROTO_H
#define CHGAME_PROTO_H
#include <stdint.h>

/*
 * CHGame bootloader wire protocol. One definition, three implementations:
 * this C bootloader, the Python development uploader, and the Web Serial page.
 * docs/protocol.md is the normative description; test/protocol/vectors.json
 * keeps the three honest.
 *
 * Frame:
 *   'C' 'G' | ver u8 | cmd u8 | len u16 LE | payload[len] | crc16 u16 LE
 *            \______________ CRC-16/CCITT-FALSE ________/
 *
 * Responses echo the command with bit 7 set; payload[0] is a status code.
 * A frame that fails CRC is answered with ERR_FRAME and the parser resynchronises
 * by scanning for the next 'C','G'.
 */

#define PROTO_SOF0            0x43u   /* 'C' */
#define PROTO_SOF1            0x47u   /* 'G' */
#define PROTO_VERSION         0x01u
/* Frame overhead is 8 bytes: 'C' 'G' | ver | cmd | len16 | crc16.
 *
 * 512 lets a WRITE carry a whole 256-byte flash page plus its 4-byte offset,
 * so the device commits exactly one page per frame.
 *
 * Frames therefore span several 64-byte USB packets, which is ordinary USB and
 * works. Recorded because it cost a day: this was briefly capped to 56 on the
 * belief that the vendored CDC driver could not receive multi-packet
 * transfers. It could. The real fault was the SysTick direction bug in sys.c -
 * a bogus elapsed time made the inter-frame timeout reset the parser between
 * every pair of packets, producing a symptom that imitated a broken USB stack
 * almost perfectly (a hard cliff at exactly the 64-byte packet size). See
 * sys.c for the calibration that now prevents it.
 */
#define PROTO_MAX_PAYLOAD     512u
#define PROTO_RESPONSE_BIT    0x80u

/* If a frame stops arriving part-way through, abandon it and go back to hunting
 * for a start marker. Without this, a truncated frame silently consumes the
 * first bytes of the NEXT frame (its length field absorbs the 'C'), so a host
 * that died mid-frame poisons the first command of the next session.
 * A full 520-byte frame crosses USB CDC in single-digit milliseconds, so this is
 * enormously generous and can never fire on a healthy transfer. */
#define PROTO_RX_TIMEOUT_MS   250u

/* Commands */
#define CMD_HELLO             0x01u
#define CMD_BEGIN             0x02u
#define CMD_WRITE             0x03u
#define CMD_END               0x04u
#define CMD_RUN               0x05u
#define CMD_STATUS            0x06u
#define CMD_ABORT             0x07u
#define CMD_READ              0x08u
#define CMD_DEV_UNLOCK        0x40u
#define CMD_DEV_WRITE_BOOT    0x41u

/* Status codes */
#define ST_OK                 0x00u
#define ST_ERR_BADCMD         0x01u
#define ST_ERR_STATE          0x02u
#define ST_ERR_RANGE          0x03u
#define ST_ERR_SIZE           0x04u
#define ST_ERR_CRC            0x05u
#define ST_ERR_FLASH          0x06u
#define ST_ERR_LOCKED         0x07u
#define ST_ERR_FRAME          0x08u
#define ST_ERR_NOTIMPL        0x09u

/* Device mode reported by HELLO */
#define MODE_BOOTLOADER       0x01u
#define MODE_APPLICATION      0x02u

#define BOOT_VERSION          0x0001u

/* ---- developer self-update gate ---------------------------------------------
 * CHGAME_ALLOW_SELFUPDATE compiles the whole facility in or out. It is 1 for
 * bench builds and MUST be 0 for anything that ships.
 *
 * CHGAME_DEV_KEY is an interlock against accidents - a stray DEV_WRITE_BOOT
 * from a confused host - not a security control. The key is plainly visible in
 * the binary to anyone who cares to look, and that is fine: the threat being
 * managed is "an upload tool bug bricks the bootloader", not "an attacker with
 * physical USB access". Do not describe it as protection.
 */
#ifndef CHGAME_ALLOW_SELFUPDATE
#define CHGAME_ALLOW_SELFUPDATE 1
#endif
#define CHGAME_DEV_KEY        0x43484744u   /* "CHGD" */

void proto_init(void);
void proto_task(void);   /* non-blocking; call from the main loop */

/* Detach from USB and put the peripheral back to sleep. MUST be called before
 * handing control to the application: otherwise the D+ pull-up stays asserted,
 * the host keeps a device it will never get another response from, and the
 * application's own USB init is not preceded by a disconnect the host can see.
 * Safe to call when USB was never brought up. */
void proto_shutdown(void);

#endif
