#ifndef TINC_FRAME_H
#define TINC_FRAME_H

#include <stdint.h>
#include "protocol.h"

/* Buffer size needed to hold a frame with up to max_payload bytes. */
#define TINC_FRAME_BUF(max_payload) ((max_payload) + TINC_OVERHEAD)

/* Encode a frame into out (must hold TINC_FRAME_BUF(len)). payload may be
 * NULL when len is 0, and may alias out + TINC_HDR_LEN (build in place).
 * Returns total frame length. */
uint16_t tinc_frame_encode(uint8_t *out, uint8_t flags, uint8_t type,
                           uint8_t seq, const uint8_t *payload, uint16_t len);

/* Byte-at-a-time parser. No timers: the caller calls tinc_parser_reset()
 * after TINC_INTERBYTE_RESET_MS of line silence. */
typedef struct {
    uint8_t *buf;
    uint16_t cap;
    uint16_t pos;
    /* valid after TINC_PARSE_FRAME, until the next feed */
    uint8_t flags, type, seq;
    uint16_t len;
    const uint8_t *payload;
} tinc_parser;

enum {
    TINC_PARSE_NONE    = 0, /* need more bytes */
    TINC_PARSE_FRAME   = 1, /* complete, CRC-valid frame available */
    TINC_PARSE_DROPPED = 2  /* bad CRC or LEN over capacity; resyncing */
};

/* buf/cap: storage for one whole frame, TINC_FRAME_BUF(max_payload). */
void tinc_parser_init(tinc_parser *p, uint8_t *buf, uint16_t cap);
void tinc_parser_reset(tinc_parser *p);
uint8_t tinc_parser_feed(tinc_parser *p, uint8_t byte);

#endif /* TINC_FRAME_H */
