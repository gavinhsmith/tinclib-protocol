/* Reference check: CRC vector + every golden frame through the parser. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../protocol.h"
#include "../crc16.h"
#include "../tinc_frame.h"
#include "../test_vectors/vectors.h"

static uint8_t buf[TINC_FRAME_BUF(TINC_PAYLOAD_LIMIT)];
static uint8_t enc[TINC_FRAME_BUF(TINC_PAYLOAD_LIMIT)];

/* Feed all bytes; return the last non-NONE result (or NONE). */
static uint8_t feed_all(tinc_parser *p, const uint8_t *d, uint16_t n)
{
    uint8_t r = TINC_PARSE_NONE, last = TINC_PARSE_NONE;
    while (n--) {
        r = tinc_parser_feed(p, *d++);
        if (r != TINC_PARSE_NONE)
            last = r;
    }
    return last;
}

int main(void)
{
    tinc_parser p;
    int i;

    assert(tinc_crc16((const uint8_t *)"123456789", 9, TINC_CRC16_INIT) == 0x29B1);

    tinc_parser_init(&p, buf, sizeof buf);
    for (i = 0; i < TINC_VALID_COUNT; i++) {
        const tinc_vector *v = &tinc_valid_vectors[i];
        uint8_t noise[] = {0x00, 0x13, 0xFF};

        /* leading noise must be skipped */
        assert(feed_all(&p, noise, sizeof noise) == TINC_PARSE_NONE);
        if (feed_all(&p, v->data, v->len) != TINC_PARSE_FRAME) {
            printf("FAIL valid %s\n", v->name);
            return 1;
        }
        assert(tinc_frame_encode(enc, p.flags, p.type, p.seq, p.payload, p.len) == v->len);
        assert(memcmp(enc, v->data, v->len) == 0);
    }

    for (i = 0; i < TINC_INVALID_COUNT; i++) {
        const tinc_vector *v = &tinc_invalid_vectors[i];
        const tinc_vector *good = &tinc_valid_vectors[0];

        if (feed_all(&p, v->data, v->len) == TINC_PARSE_FRAME) {
            printf("FAIL invalid %s\n", v->name);
            return 1;
        }
        tinc_parser_reset(&p); /* the platform's inter-byte gap */
        assert(feed_all(&p, good->data, good->len) == TINC_PARSE_FRAME);
    }

    /* a parser sized for the minimum payload drops a bigger frame, then resyncs */
    {
        static uint8_t small[TINC_FRAME_BUF(TINC_PAYLOAD_MIN)];
        const tinc_vector *big = &tinc_valid_vectors[TINC_VALID_COUNT - 1]; /* max_payload */
        tinc_parser_init(&p, small, sizeof small);
        assert(feed_all(&p, big->data, TINC_HDR_LEN) == TINC_PARSE_DROPPED);
        assert(feed_all(&p, tinc_valid_vectors[0].data, tinc_valid_vectors[0].len) == TINC_PARSE_FRAME);
    }

    printf("ok: crc, %d valid, %d invalid\n", TINC_VALID_COUNT, TINC_INVALID_COUNT);
    return 0;
}
