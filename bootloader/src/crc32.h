#ifndef CHGAME_CRC32_H
#define CHGAME_CRC32_H
#include <stdint.h>
#include <stddef.h>

/* CRC-32/ISO-HDLC (a.k.a. zlib crc32): reflected poly 0xEDB88320,
 * init 0xFFFFFFFF, final xor 0xFFFFFFFF.
 * Nibble-at-a-time: 64 bytes of table instead of 1 KB, which matters inside
 * an 8 KB bootloader. Python's zlib.crc32 and JS implementations must agree. */
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);
static inline uint32_t crc32_init(void)               { return 0xFFFFFFFFu; }
static inline uint32_t crc32_final(uint32_t crc)      { return crc ^ 0xFFFFFFFFu; }
uint32_t crc32_buf(const void *data, size_t len);
#endif
