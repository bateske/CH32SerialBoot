#ifndef CHGAME_CRC16_H
#define CHGAME_CRC16_H
#include <stdint.h>
#include <stddef.h>

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final xor.
 * Used for protocol frame integrity. The image itself is protected separately
 * by CRC-32 (crc32.h) — the two serve different purposes and must not be
 * conflated: frames are small and transient, images are large and persisted. */
uint16_t crc16_update(uint16_t crc, const void *data, size_t len);
uint16_t crc16_buf(const void *data, size_t len);

#endif
