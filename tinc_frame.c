#include <string.h>
#include "tinc_frame.h"
#include "crc16.h"

uint16_t tinc_frame_encode(uint8_t *out, uint8_t flags, uint8_t type,
                           uint8_t seq, const uint8_t *payload, uint16_t len)
{
    uint16_t crc;

    out[0] = TINC_SOF;
    out[1] = flags;
    out[2] = type;
    out[3] = seq;
    tinc_put_u16(out + 4, len);
    if (len && payload != out + TINC_HDR_LEN)
        memmove(out + TINC_HDR_LEN, payload, len);
    crc = tinc_crc16(out + 1, (uint16_t)(TINC_HDR_LEN - 1 + len), TINC_CRC16_INIT);
    tinc_put_u16(out + TINC_HDR_LEN + len, crc);
    return (uint16_t)(TINC_OVERHEAD + len);
}

void tinc_parser_init(tinc_parser *p, uint8_t *buf, uint16_t cap)
{
    p->buf = buf;
    p->cap = cap;
    p->pos = 0;
}

void tinc_parser_reset(tinc_parser *p)
{
    p->pos = 0;
}

/* ponytail: on a drop we don't rescan the discarded bytes for a SOF, so a
 * frame that starts inside a corrupt one is lost too; retries cover it.
 * Add a rescan if the link turns out to be noisy. */
uint8_t tinc_parser_feed(tinc_parser *p, uint8_t byte)
{
    uint16_t len;

    if (p->pos == 0 && byte != TINC_SOF)
        return TINC_PARSE_NONE;

    p->buf[p->pos++] = byte;
    if (p->pos < TINC_HDR_LEN)
        return TINC_PARSE_NONE;

    len = tinc_get_u16(p->buf + 4);
    if ((uint32_t)len + TINC_OVERHEAD > p->cap) {
        p->pos = 0;
        return TINC_PARSE_DROPPED;
    }
    if (p->pos < TINC_OVERHEAD + len)
        return TINC_PARSE_NONE;

    p->pos = 0;
    if (tinc_crc16(p->buf + 1, (uint16_t)(TINC_HDR_LEN - 1 + len), TINC_CRC16_INIT)
        != tinc_get_u16(p->buf + TINC_HDR_LEN + len))
        return TINC_PARSE_DROPPED;

    p->flags = p->buf[1];
    p->type = p->buf[2];
    p->seq = p->buf[3];
    p->len = len;
    p->payload = p->buf + TINC_HDR_LEN;
    return TINC_PARSE_FRAME;
}
