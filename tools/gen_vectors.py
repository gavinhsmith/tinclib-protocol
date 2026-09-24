#!/usr/bin/env python3
"""Generate golden frames in test_vectors/.

CRC comes from binascii.crc_hqx (CRC-16/CCITT with caller-chosen init),
an implementation independent of crc16.c, so the C tests cross-check it.

Outputs:
  test_vectors/valid.hex    frames every parser must accept
  test_vectors/invalid.hex  frames every parser must reject
  test_vectors/vectors.h    the same frames as C arrays (for CE/ESP tests
                            that have no filesystem)
"""
import binascii
import struct
from pathlib import Path

SOF = 0xA5
RESP, EVENT, ERR = 0x01, 0x02, 0x04
PAYLOAD_LIMIT = 1024

HELLO, STATUS, REQ_BEGIN, REQ_STATUS, REQ_ABORT, BODY_READ = 0x01, 0x02, 0x10, 0x11, 0x14, 0x21
HDR_GET, BODY_WRITE = 0x13, 0x20
WIFI_GET, WIFI_SET, WIFI_FORGET = 0x40, 0x41, 0x42
E_UNSUPPORTED, E_NO_HELLO, E_VERSION, E_BAD_OFFSET, E_BAD_ARG, E_UNSUPPORTED_SCHEME = 0x01, 0x02, 0x03, 0x07, 0x08, 0x09
E_LOCKED = 0x0A
E_DNS, E_TLS, E_CERT, E_TIME = 0x21, 0x27, 0x28, 0x29
TLSR_VERSION, TLSR_EXPIRED, TLSR_HOSTNAME = 0x01, 0x10, 0x12
RS_ERROR = 7
M_GET, M_POST, M_PUT, M_DELETE, M_PATCH, M_HEAD = 1, 2, 3, 4, 5, 6
HGETF_FOUND, HGETF_TRUNC = 0x01, 0x02
WRITEF_RESPONDED = 0x01
WF_HIDDEN = 0x01
STATUSF_WIFI_LOCKED, STATUSF_TIME_VALID = 0x01, 0x02


def crc(data):
    return binascii.crc_hqx(data, 0xFFFF)


assert crc(b"123456789") == 0x29B1


def frame(flags, typ, seq, payload=b""):
    body = struct.pack("<BBBH", flags, typ, seq, len(payload)) + payload
    return bytes([SOF]) + body + struct.pack("<H", crc(body))


def lp8(b):
    return bytes([len(b)]) + b


url = b"http://example.com/api?q=1"
url_https = b"https://example.com/api?q=1"
hdrs = b"Accept: application/json\r\n"
ctype = b"application/json"
post_hdrs = b"Content-Type: application/json\r\n"
post_body = b'{"name":"calc","n":84}'

VALID = [
    ("hello_req", frame(0, HELLO, 1, struct.pack("<BBHH", 0, 5, 0, 256))),
    ("hello_resp", frame(RESP, HELLO, 1, struct.pack("<BBHHIB", 0, 5, 0, 1024, 28000, 5))),
    ("status_req", frame(0, STATUS, 2)),
    ("status_resp", frame(RESP, STATUS, 2,
                          struct.pack("<BBb4sIBB", 2, 0, -61, bytes([192, 168, 1, 42]), 27500, 0,
                                      STATUSF_TIME_VALID))),
    ("status_resp_locked", frame(RESP, STATUS, 2,
                                 struct.pack("<BBb4sIBB", 2, 0, -61, bytes([192, 168, 1, 42]), 27500, 0,
                                             STATUSF_WIFI_LOCKED))),
    ("req_begin_req", frame(0, REQ_BEGIN, 3,
                            struct.pack("<BBBIHH", 1, 0x01, 0, 0, len(url), len(hdrs)) + url + hdrs)),
    ("req_begin_req_https", frame(0, REQ_BEGIN, 3,
                                  struct.pack("<BBBIHH", 1, 0, 0, 0, len(url_https), 0) + url_https)),
    ("req_begin_req_post", frame(0, REQ_BEGIN, 3,
                                 struct.pack("<BBBIHH", M_POST, 0, 0, len(post_body), len(url_https),
                                             len(post_hdrs)) + url_https + post_hdrs)),
    ("req_begin_req_delete", frame(0, REQ_BEGIN, 3,
                                   struct.pack("<BBBIHH", M_DELETE, 0, 0, 0, len(url_https), 0) + url_https)),
    ("req_begin_req_head", frame(0, REQ_BEGIN, 3,
                                 struct.pack("<BBBIHH", M_HEAD, 0, 0, 0, len(url_https), 0) + url_https)),
    ("req_begin_resp", frame(RESP, REQ_BEGIN, 3)),
    ("err_bad_arg_hdr", frame(RESP | ERR, REQ_BEGIN, 3, bytes([E_BAD_ARG]))),
    ("body_write_req", frame(0, BODY_WRITE, 21, struct.pack("<IB", 0, 100) + post_body)),
    ("body_write_resp_partial", frame(RESP, BODY_WRITE, 21, struct.pack("<IB", 8, 0))),
    ("body_write_req_rest", frame(0, BODY_WRITE, 22, struct.pack("<IB", 8, 100) + post_body[8:])),
    ("body_write_resp_done", frame(RESP, BODY_WRITE, 22, struct.pack("<IB", len(post_body), 0))),
    ("body_write_resp_none", frame(RESP, BODY_WRITE, 23, struct.pack("<IB", 0, 0))),
    ("body_write_resp_responded", frame(RESP, BODY_WRITE, 24, struct.pack("<IB", 8, WRITEF_RESPONDED))),
    ("err_write_bad_offset", frame(RESP | ERR, BODY_WRITE, 25, bytes([E_BAD_OFFSET]) + struct.pack("<I", 8))),
    ("err_write_past_len", frame(RESP | ERR, BODY_WRITE, 26, bytes([E_BAD_ARG]))),
    ("hdr_get_req", frame(0, HDR_GET, 27, struct.pack("<BH", 0, 0) + lp8(b"Location"))),
    ("hdr_get_resp", frame(RESP, HDR_GET, 27,
                           struct.pack("<BH", HGETF_FOUND, len(b"/items/42")) + b"/items/42")),
    ("hdr_get_req_paged", frame(0, HDR_GET, 28, struct.pack("<BH", 1, 4) + lp8(b"link"))),
    ("hdr_get_resp_paged", frame(RESP, HDR_GET, 28, struct.pack("<BH", HGETF_FOUND, 200) + b"/page2>")),
    ("hdr_get_resp_missing", frame(RESP, HDR_GET, 29, struct.pack("<BH", HGETF_TRUNC, 0))),
    ("req_status_req", frame(0, REQ_STATUS, 4)),
    ("req_status_resp", frame(RESP, REQ_STATUS, 4,
                              struct.pack("<BBHI", 5, 0, 200, 0xFFFFFFFF) + lp8(ctype) + bytes([0]))),
    ("req_status_resp_cert", frame(RESP, REQ_STATUS, 4,
                                   struct.pack("<BBHI", RS_ERROR, E_CERT, 0, 0xFFFFFFFF) + lp8(b"")
                                   + bytes([TLSR_HOSTNAME]))),
    ("body_read_req", frame(0, BODY_READ, 5, struct.pack("<IHB", 0, 128, 50))),
    ("body_read_resp", frame(RESP, BODY_READ, 5, struct.pack("<IB", 0, 0) + b'{"ok":true,')),
    ("body_read_resp_eof", frame(RESP, BODY_READ, 6, struct.pack("<IB", 11, 0x01) + b'"n":1}')),
    ("body_read_resp_empty", frame(RESP, BODY_READ, 7, struct.pack("<IB", 17, 0))),
    ("req_abort_req", frame(0, REQ_ABORT, 8)),
    ("req_abort_resp", frame(RESP, REQ_ABORT, 8)),
    ("wifi_get_req", frame(0, WIFI_GET, 9, bytes([4]))),
    ("wifi_get_resp", frame(RESP, WIFI_GET, 9, lp8(b"Phone") + bytes([WF_HIDDEN]))),
    ("wifi_get_resp_empty", frame(RESP, WIFI_GET, 9, lp8(b"") + bytes([0]))),
    ("err_bad_slot", frame(RESP | ERR, WIFI_GET, 9, bytes([E_BAD_ARG]))),
    ("wifi_set_req", frame(0, WIFI_SET, 10, bytes([1]) + lp8(b"Phone") + lp8(b"hunter22") + bytes([WF_HIDDEN]))),
    ("wifi_set_resp", frame(RESP, WIFI_SET, 10)),
    ("wifi_forget_req", frame(0, WIFI_FORGET, 11, bytes([1]))),
    ("wifi_forget_resp", frame(RESP, WIFI_FORGET, 11)),
    ("err_no_hello", frame(RESP | ERR, STATUS, 12, bytes([E_NO_HELLO]))),
    ("err_version", frame(RESP | ERR, HELLO, 13, bytes([E_VERSION, 0, 6]))),
    ("err_unsupported", frame(RESP | ERR, 0x12, 14, bytes([E_UNSUPPORTED]))),
    ("err_bad_offset", frame(RESP | ERR, BODY_READ, 15, bytes([E_BAD_OFFSET]))),
    ("err_scheme", frame(RESP | ERR, REQ_BEGIN, 16, bytes([E_UNSUPPORTED_SCHEME]))),
    ("err_locked", frame(RESP | ERR, WIFI_SET, 16, bytes([E_LOCKED]))),
    ("err_request_dns", frame(RESP | ERR, BODY_READ, 17, bytes([E_DNS, 0]))),
    ("err_request_cert", frame(RESP | ERR, BODY_READ, 17, bytes([E_CERT, TLSR_EXPIRED]))),
    ("err_request_tls", frame(RESP | ERR, BODY_READ, 17, bytes([E_TLS, TLSR_VERSION]))),
    ("err_request_time", frame(RESP | ERR, BODY_READ, 17, bytes([E_TIME, 0]))),
    # forward-compat: reserved flag bit set -> must be accepted, bit ignored
    ("fwd_reserved_flag", frame(RESP | 0x80, REQ_ABORT, 18)),
    # forward-compat: trailing bytes after known fields -> must be accepted
    ("fwd_trailing_bytes", frame(RESP, STATUS, 19,
                                 struct.pack("<BBb4sIBB", 2, 0, -61, bytes(4), 1, 0, 0) + b"\x01\x02")),
    ("max_payload", frame(RESP, BODY_READ, 20,
                          struct.pack("<IB", 0, 0) + bytes(i & 0xFF for i in range(PAYLOAD_LIMIT - 5)))),
]

good = frame(0, STATUS, 1)
INVALID = [
    # parser must report a drop
    ("bad_crc", good[:-1] + bytes([good[-1] ^ 0xFF])),
    ("len_over_limit", bytes([SOF, 0, BODY_READ, 1]) + struct.pack("<H", PAYLOAD_LIMIT + 1)),
    # parser must never report a frame (feed, then reset)
    ("truncated", good[:-1]),
]


def write_hex(path, vectors):
    with open(path, "w", newline="\n") as f:
        for name, data in vectors:
            f.write(f"# {name}\n{data.hex()}\n")


def c_array(name, data):
    rows = [", ".join(f"0x{b:02X}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    return f"static const uint8_t tv_{name}[] = {{\n    " + ",\n    ".join(rows) + "\n};\n"


def write_header(path):
    out = ["/* Generated by tools/gen_vectors.py. Do not edit. */",
           "#ifndef TINC_TEST_VECTORS_H", "#define TINC_TEST_VECTORS_H", "",
           "#include <stdint.h>", "",
           "typedef struct { const char *name; const uint8_t *data; uint16_t len; } tinc_vector;", ""]
    for name, data in VALID + INVALID:
        out.append(c_array(name, data))
    for label, vecs in (("valid", VALID), ("invalid", INVALID)):
        out.append(f"static const tinc_vector tinc_{label}_vectors[] = {{")
        out += [f'    {{"{n}", tv_{n}, sizeof tv_{n}}},' for n, _ in vecs]
        out += ["};", f"#define TINC_{label.upper()}_COUNT {len(vecs)}", ""]
    out.append("#endif")
    Path(path).write_text("\n".join(out) + "\n", newline="\n")


if __name__ == "__main__":
    d = Path(__file__).resolve().parent.parent / "test_vectors"
    d.mkdir(exist_ok=True)
    write_hex(d / "valid.hex", VALID)
    write_hex(d / "invalid.hex", INVALID)
    write_header(d / "vectors.h")
    print(f"{len(VALID)} valid, {len(INVALID)} invalid -> {d}")
