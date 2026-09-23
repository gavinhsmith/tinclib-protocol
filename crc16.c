#include "crc16.h"

/* Nibble table: 32 bytes, small enough for the CE. */
static const uint16_t tinc_crc16_tab[16] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF
};

uint16_t tinc_crc16(const uint8_t *p, uint16_t n, uint16_t crc)
{
    while (n--) {
        uint8_t b = *p++;
        crc = (uint16_t)((crc << 4) ^ tinc_crc16_tab[((crc >> 12) ^ (b >> 4)) & 0x0F]);
        crc = (uint16_t)((crc << 4) ^ tinc_crc16_tab[((crc >> 12) ^ b) & 0x0F]);
    }
    return crc;
}
