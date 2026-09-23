/* libFuzzer target for the frame parser. Consumers can swap in their own
 * parser behind the same three calls.
 *   clang -fsanitize=fuzzer,address tests/fuzz_frame.c crc16.c tinc_frame.c */
#include <stddef.h>
#include <stdint.h>
#include "../tinc_frame.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static uint8_t buf[TINC_FRAME_BUF(TINC_PAYLOAD_MIN)];
    tinc_parser p;
    size_t i;

    tinc_parser_init(&p, buf, sizeof buf);
    for (i = 0; i < size; i++) {
        if (tinc_parser_feed(&p, data[i]) == TINC_PARSE_FRAME) {
            /* touch the whole frame view so ASan sees any overrun */
            volatile uint8_t sink = p.flags ^ p.type ^ p.seq;
            uint16_t j;
            for (j = 0; j < p.len; j++)
                sink ^= p.payload[j];
            (void)sink;
        }
    }
    return 0;
}
