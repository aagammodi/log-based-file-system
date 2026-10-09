#include "logfs.h"

/*
 * Standard reflected CRC-32.
 * Polynomial: 0xEDB88320
 */
uint32_t logfs_crc32(const void *data, size_t len)
{
    const unsigned char *p = data;
    uint32_t crc = 0xFFFFFFFFU;

    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];

        for (unsigned int bit = 0; bit < 8; bit++) {
            if (crc & 1U)
                crc = (crc >> 1) ^ 0xEDB88320U;
            else
                crc >>= 1;
        }
    }

    return ~crc;
}
