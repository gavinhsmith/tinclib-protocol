#ifndef TINC_CRC16_H
#define TINC_CRC16_H

#include <stdint.h>

#define TINC_CRC16_INIT 0xFFFFu

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout.
 * Pass TINC_CRC16_INIT to start; pass the previous result to continue.
 * "123456789" -> 0x29B1. */
uint16_t tinc_crc16(const uint8_t *p, uint16_t n, uint16_t crc);

#endif /* TINC_CRC16_H */
