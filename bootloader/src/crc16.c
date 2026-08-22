#include "crc16.h"

uint16_t crc16_update(uint16_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    while (len--) {
        crc ^= (uint16_t)(*p++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

uint16_t crc16_buf(const void *data, size_t len)
{
    return crc16_update(0xFFFFu, data, len);
}
