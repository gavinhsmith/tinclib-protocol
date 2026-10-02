# tinclib-protocol

[![Tag](https://img.shields.io/github/v/tag/gavinhsmith/tinclib-protocol)](https://github.com/gavinhsmith/tinclib-protocol/tags)
[![License: Apache 2.0](https://img.shields.io/github/license/gavinhsmith/tinclib-protocol)](LICENSE)

**The wire protocol that gives the TI-84 Plus CE Wi-Fi, HTTP and HTTPS.** A
small, framed, retry-safe serial protocol between the calculator and an
ESP8266 board, written as one plain C99 header plus a reference frame encoder
and parser. Every other tinclib repo builds these files as they are, from a
pinned tag.

- **One file is the spec**: [`protocol.h`](protocol.h) holds the frame format,
  message types, error codes, request states, timing, and a layout comment for
  every payload.
- **Portable C99**: no platform code, no `#ifdef`s, no packed structs (byte
  offset macros with little-endian get/put helpers instead), so it builds
  for the eZ80, Xtensa and a PC alike.
- **Retry-safe**: the calculator reuses a sequence number on retry and the
  board replays its cached reply, so no command runs twice.
- **Pull-based HTTP**: the calculator starts a request, then polls its
  status and reads the body and headers in pieces as it has room for them.
- **Golden vectors**: generated valid and invalid frames, as hex files and as
  a C header, so every implementation tests against the same bytes.
- **Tested and fuzzed**: an assert-based test of the CRC, encoder and parser,
  and a libFuzzer target for the parser.

## How it fits together

tinclib-protocol is one part of four:

| Repo | What |
|---|---|
| [tinclib](https://github.com/gavinhsmith/tinclib) | The C library your calculator program links against |
| [tinclib-firmware](https://github.com/gavinhsmith/tinclib-firmware) | Firmware for the ESP8266 board, which does the networking |
| [tinclib-config](https://github.com/gavinhsmith/tinclib-config) | TINCLIBC, the calculator app for Wi-Fi setup |
| **tinclib-protocol** (this one) | The wire protocol between calculator and board |

```
TI-84 Plus CE ──USB serial, this protocol──> ESP8266 board ──Wi-Fi──> the internet
```

## The protocol at a glance

```
[SOF 0xA5][FLAGS:1][TYPE:1][SEQ:1][LEN:2][PAYLOAD:LEN][CRC16:2]
```

- Little-endian. CRC-16/CCITT-FALSE over everything after the SOF.
- A frame with a bad CRC, an oversize length or a missing tail is dropped
  silently. The sender's timeout and retry recover it.
- Replies echo `TYPE` and `SEQ` with the `RESP` flag set. Errors also set
  `ERR` and carry an error code.
- 115200 baud by default. Each side advertises its receive limit (64 to 1024
  bytes of payload) in `HELLO`.

| Type | Messages |
|---|---|
| Session | `HELLO`, `STATUS`, `INFO` |
| HTTP(S) request | `REQ_BEGIN`, `REQ_STATUS`, `HDR_GET`, `REQ_ABORT`, `BODY_WRITE`, `BODY_READ` |
| Wi-Fi profiles | `WIFI_GET`, `WIFI_SET`, `WIFI_FORGET` |

HTTPS is always certificate-checked, against CA roots built into the
firmware. TLS failures report a reason (expired, wrong hostname, untrusted
root, no common version, and so on), so the calculator can say what went
wrong. See [`protocol.h`](protocol.h) for every layout and rule, and
[CHANGELOG.md](CHANGELOG.md) for what changed in each version.

## Versions

The current version is **0.6**, tagged `v0.6`. Every wire-visible change bumps
the version and gets a `vMAJOR.MINOR` tag. Before 1.0 any minor version may
break the wire format, so `HELLO` requires an exact match.

Deferred for now: insecure (unverified) TLS, CA bundle updates over the wire,
chunked uploads, Wi-Fi scan, a boot event and baud switching.

## Using it

Add it as a submodule pinned to a tag, and build `crc16.c` and `tinc_frame.c`
in place. Don't copy the files.

```sh
git submodule add https://github.com/gavinhsmith/tinclib-protocol external/tinclib-protocol
git -C external/tinclib-protocol checkout v0.6
```

| File | What |
|---|---|
| `protocol.h` | The spec: constants, enums, payload offsets and LE helpers |
| `crc16.c`, `crc16.h` | `tinc_crc16()`, CRC-16/CCITT-FALSE with a nibble table |
| `tinc_frame.c`, `tinc_frame.h` | `tinc_frame_encode()` and a byte-at-a-time parser with no timers or platform calls |
| `test_vectors/` | Generated golden frames: `valid.hex`, `invalid.hex` and `vectors.h` |
| `tools/gen_vectors.py` | Generates `test_vectors/` (edit this, never its output) |

## Test

```sh
make test     # CRC, encoder and parser against the golden vectors
make fuzz     # libFuzzer on the parser for 60 s (needs clang)
make vectors  # regenerate test_vectors/
```

## License

[Apache 2.0](LICENSE)
