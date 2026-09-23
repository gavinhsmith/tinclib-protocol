# Changelog

Every wire-visible change bumps `TINC_PROTO_MAJOR` or `TINC_PROTO_MINOR` in
`protocol.h`, and is tagged `vMAJOR.MINOR.0`.

## v0.3.0

The number of Wi-Fi slots is now defined by the firmware. Not compatible
with 0.2.

- The `HELLO` reply appends `wifi_slots u8` (1..254). Slots are numbered
  `0 .. wifi_slots-1`, and a slot outside that range gets `ERR_BAD_ARG`.
- `WIFI_LIST` (0x40) is replaced by `WIFI_GET` (0x40). The request is
  `slot u8`, and the reply is `ssid_len u8, ssid[], wflags u8` for that one
  slot. A full list could outgrow a 64-byte `max_payload`; one slot per frame
  always fits.
- `TINC_WIFI_SLOTS` is replaced by `TINC_WIFI_SLOTS_MAX` (254).

## v0.2.0

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

## v0.1.0

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
