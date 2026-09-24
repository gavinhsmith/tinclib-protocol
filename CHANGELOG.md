# Changelog

Every wire-visible change bumps `TINC_PROTO_MAJOR` or `TINC_PROTO_MINOR` in
`protocol.h`, and is tagged `vMAJOR.MINOR`.

## v0.5

Request bodies, more methods and response headers. Not compatible with 0.4.

- `REQ_BEGIN` accepts methods POST (2), PUT (3), DELETE (4), PATCH (5) and
  HEAD (6), as well as GET.
  - `content_len` is the exact request body length. It must be 0 for GET
    and HEAD. `TINC_LEN_UNKNOWN` (a chunked upload) is `ERR_BAD_ARG`, and is
    reserved.
  - The ESP generates `Host` and `Content-Length`. An app header named
    `Host`, `Content-Length`, `Transfer-Encoding` or `Expect` is
    `ERR_BAD_ARG`.
  - Only GET and HEAD follow redirects. For other methods the 3xx is the
    response.
  - For HEAD, `REQ_STATUS.content_len` is the response's Content-Length and
    `BODY_READ` returns EOF at offset 0.
- `BODY_WRITE` (0x20) uploads the body: `offset u32, wait_ms u8, data` →
  `next_offset u32, flags u8`.
  - The ESP takes as much as fits in its send buffer, possibly nothing. The
    CE continues from `next_offset`.
  - A wrong offset is `ERR_BAD_OFFSET` with detail `expected u32`.
  - It is valid from `CONNECTING` on, so the write loop is also the connect
    poll. The request moves to `WAIT_HEADERS` when the body is complete.
  - Flag `RESPONDED` (0x01): the server answered before the upload finished.
- `HDR_GET` (0x13) reads a response header: `index u8, offset u16,
  name_len u8, name` → `flags u8, total_len u16, data`.
  - Flags: `FOUND` (0x01), and `TRUNC` (0x02) when the ESP's header store
    overflowed. Location is always kept in full.
  - It pages through long values with `offset`. Valid in `BODY`/`DONE`.
- After a reset, the CE never resends a request other than GET/HEAD.
- `INFO` (0x03) returns the firmware version and board name as
  display-only strings: `fw_len u8, fw, board_len u8, board`, each at most
  24 bytes. It is not a compatibility check; `HELLO` still is.

## v0.4

HTTPS. Not compatible with 0.3.

- `REQ_BEGIN` accepts `https://`. Certificates are always verified against
  CA roots built into the firmware, and a reflash updates them.
  `ERR_UNSUPPORTED_SCHEME` now means "neither http nor https".
- The `TLS` request state is now used. It waits for a valid clock (SNTP),
  then does the handshake, and the per-phase timeout covers both steps.
- New request errors: `ERR_TLS` (0x27, handshake), `ERR_CERT` (0x28, chain,
  hostname or validity), `ERR_TIME` (0x29, no clock before the TLS phase
  timed out), `ERR_REDIRECT_DOWNGRADE` (0x2A).
- TLS failures carry a protocol-defined reason `err_detail u8`
  (`TINC_TLSR_*`), both in the `BODY_READ` error reply's detail and
  appended to the `REQ_STATUS` reply after `ctype`. It is 0 for other errors.
- The ESP must answer every frame within `TINC_REPLY_TIMEOUT_MS` in every
  request phase. The TLS handshake has to run incrementally, not block.
- Redirects: http→https is followed. https→http is not, so the app's
  headers are never resent in clear. A redirect to a different host drops
  all of the app's headers, so auth headers only reach the host the app
  named.
- `STATUS.flags` gets `TINC_STATUSF_TIME_VALID` (0x02).
- Reserved for later: request flag `INSECURE` (0x02) and
  `ERR_INSECURE_DISABLED` (0x0B). Deferred until the admin access-control
  question is settled, along with CA bundle update over the wire.

## v0.3

The number of Wi-Fi slots is now defined by the firmware. Not compatible
with 0.2.

- The `HELLO` reply appends `wifi_slots u8` (1..254). Slots are numbered
  `0 .. wifi_slots-1`, and a slot outside that range gets `ERR_BAD_ARG`.
- `WIFI_LIST` (0x40) is replaced by `WIFI_GET` (0x40). The request is
  `slot u8`, and the reply is `ssid_len u8, ssid[], wflags u8` for that one
  slot. A full list could outgrow a 64-byte `max_payload`; one slot per frame
  always fits.
- `TINC_WIFI_SLOTS` is replaced by `TINC_WIFI_SLOTS_MAX` (254).

## v0.2

Wi-Fi additions. Not compatible with 0.1: HELLO rejects a 0.1 peer with
`ERR_VERSION`.

- **Hidden networks:** `WIFI_SET` gets a trailing `wflags` byte
  (`TINC_WF_HIDDEN` = 0x01), and the ESP tries a hidden slot directly
  instead of waiting to see it in a scan. `WIFI_LIST` appends one `wflags`
  byte per slot.
- **Wi-Fi lock:** the ESP can lock its connection profiles. The lock is set
  in firmware (a build flag or a physical switch), never over the wire.
  - `STATUS` gets a trailing `flags` byte (`TINC_STATUSF_WIFI_LOCKED` = 0x01).
  - While locked, `WIFI_SET` and `WIFI_FORGET` return the new
    `ERR_LOCKED` (0x0A). `WIFI_LIST` still works.

## v0.1

First draft. Pre-1.0, so any minor bump may break the wire format
(HELLO requires an exact version match). It supports plain-HTTP GET and
the minimum Wi-Fi setup.

- Frame format, CRC-16/CCITT-FALSE, FLAGS `RESP`/`EVENT`/`ERR`.
- Payloads are append-only. Receivers ignore trailing bytes, and from 1.0
  minor versions may only append fields.
- Messages: `HELLO`, `STATUS`, `REQ_BEGIN` (GET only, `http://` only),
  `REQ_STATUS`, `REQ_ABORT`, `BODY_READ`, `WIFI_LIST`, `WIFI_SET`,
  `WIFI_FORGET`.
- Request flag `TRANSCODE`.
- Reserved for later versions: `HDR_GET` (0x13), `BODY_WRITE` (0x20),
  `BOOT` event (0x80), the `TLS` request state, HTTPS, and baud switching.
- Reference frame encoder/parser (`tinc_frame.c`), golden vectors, and a
  fuzz target.
