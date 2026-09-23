# AGENTS.md — tinclib-protocol

## What this repo is

This repo is the **single source of truth** for the TINCLIB wire protocol: the
byte-level frame format, message types, payload layouts, state machines, and
error codes used between a TI-84 Plus CE calculator and an ESP8266-based
board over USB serial.

It is consumed by three other repos as a submodule / pinned dependency:
- `tinclib` — the CE-side C library (calculator, eZ80, CE C toolchain)
- `tinclib-firmware` — the ESP8266 firmware (PlatformIO / Arduino core)
- `tinclib-config` — TINCLIBC.8xp, the config app (also uses `tinclib`)
- A PC-side test suite also consumes this repo directly.

**Nothing hardware-specific or platform-specific belongs here.** No Arduino
calls, no CE toolchain calls, no `#ifdef` per platform. If a change requires
platform code to compile, it belongs in the consuming repo, not here.

## Current status

- **Version 0.3.0 is a draft.** `v0.1.0` and `v0.2.0` are tagged, and `v0.3.0` gets tagged
  when the user asks. The spec will keep changing on the way to 1.0.
- **0.3 scope:** plain-HTTP GET, plus the minimum Wi-Fi setup
  (`WIFI_GET` / `WIFI_SET` / `WIFI_FORGET`) with a firmware-defined number
  of slots, hidden networks, and an
  ESP-side lock on the Wi-Fi profiles.
- **Deferred:**
  - HTTPS / TLS / the `INSECURE` flag / CA bundle update
  - POST (`BODY_WRITE`)
  - `HDR_GET`
  - Wi-Fi scan
  - `BOOT` event
  - baud switching
- In this file, "v1" means the first design generation, which is what 1.0.0
  will freeze. It is not a version number. Concrete versions are written
  as `0.1`, `1.0.0` and so on.

## Contents

- `protocol.h` — plain C99 header, and **the spec**. It holds:
  - constants, and enums for message types, error codes, request states and
    flags.
  - payload layouts as **byte-offset macros** with LE get/put helpers.
    There are deliberately no packed structs: packing isn't C99, and
    unaligned member access traps on Xtensa.
  - a layout comment next to every message.
- `crc16.c` / `crc16.h` — `tinc_crc16(p, n, crc)`, a nibble-table
  CRC-16/CCITT-FALSE. Pass `TINC_CRC16_INIT` to start, or the previous result
  to continue.
- `tinc_frame.c` / `tinc_frame.h` — reference frame encoder
  (`tinc_frame_encode`) and a byte-at-a-time parser (`tinc_parser_init` /
  `_feed` / `_reset`).
  - It has no timers and makes no platform calls. The caller resets the
    parser after `TINC_INTERBYTE_RESET_MS` of silence.
  - The parser buffer is `TINC_FRAME_BUF(max_payload)` bytes.
  - It does not rescan dropped bytes for a SOF. This is a deliberate
    `ponytail:` shortcut: retries cover it.
- `tools/gen_vectors.py` — generates everything in `test_vectors/`.
  - **Edit the script, never its outputs.**
  - It uses Python's `binascii.crc_hqx` as an independent CRC, so it
    cross-checks `crc16.c`.
- `test_vectors/` — generated, committed golden frames:
  - `valid.hex` / `invalid.hex`, one `# name` line plus one hex line per
    frame.
  - `vectors.h`, the same frames as C arrays, for CE/ESP tests that have no
    filesystem.
  - `.gitignore` ignores `*.hex` globally, with an exception for
    `test_vectors/*.hex`. Keep that exception.
- `tests/test_frame.c` — assert-based check:
  - the CRC vector.
  - every valid frame parses (after leading noise) and re-encodes
    byte-for-byte.
  - every invalid frame is rejected, and the parser resyncs.
  - an oversize frame is dropped by a small-buffer parser.
- `tests/fuzz_frame.c` — libFuzzer target for the parser.
- `Makefile` — `make test`, `make vectors`, `make fuzz` (needs clang),
  `make clean`. Output goes to `build/`, which is ignored.
- `CHANGELOG.md` — protocol version history.

## Build & test

```sh
python tools/gen_vectors.py      # after any change to the script
make test CC=gcc                 # PC: CRC + golden frames through the parser
make fuzz                        # needs clang/libFuzzer (not installed on the Windows dev box)
```

eZ80 width check. The shared sources must compile cleanly for the CE:
```sh
/c/tools/CEdev/bin/ez80-clang.exe -target ez80-none-elf -nostdinc \
  -isystem /c/tools/CEdev/include -std=c99 -Wall -Wextra -pedantic -Werror \
  -Oz -S -o build/x.src crc16.c   # repeat for tinc_frame.c
```

Windows dev box quirk: put `/c/msys64/ucrt64/bin` first on `PATH`.
Otherwise Git's `mingw64` DLLs shadow the ucrt64 gcc's, and gcc exits 1
without printing anything.

## Versioning rules (important — do not skip)

- Tag releases as `vMAJOR.MINOR.0` (e.g. `v1.2.0`). The tag must match
  `TINC_PROTO_MAJOR`/`TINC_PROTO_MINOR` in `protocol.h` exactly. Every
  wire-visible change bumps one of them, and is recorded in `CHANGELOG.md`.
- Every consuming repo pins this repo to a specific tag. **Don't tag unless
  the user asks.**
- **Pre-1.0 (`0.x`): the spec is still moving.**
  - Any MINOR bump may break the wire format.
  - So while MAJOR is 0, `HELLO` treats a MINOR mismatch as `ERR_VERSION`
    too.
  - The golden vectors carry the current version, so regenerate them on
    every bump.
- **From `1.0.0`**, which freezes the frame format and the existing layouts:
  - **MAJOR** bumps on any breaking change: new required field, changed
    field meaning, removed message type, changed frame layout.
  - **MINOR** bumps on additive, backward-compatible change: new optional
    message type, new flag bit that older peers can safely ignore, new error
    code, new trailing field.
  - A major mismatch in `HELLO` is `ERR_VERSION`. A minor mismatch is not an
    error: the higher-versioned side just shouldn't rely on fields or
    behavior the other side doesn't have.
- **Payloads are append-only.** Receivers must ignore trailing bytes past
  the fields they know about. From 1.0, a MINOR version may *append* fields
  to any payload, but inserting, reordering or resizing a field is a MAJOR
  change.
- Never silently change a message's payload layout without a version bump.
  An unversioned change stays invisible until it causes a field-offset bug
  at runtime.

## Frame format (do not change without a MAJOR bump)

```
[SOF 0xA5][FLAGS:1][TYPE:1][SEQ:1][LEN:2][PAYLOAD:0..LEN][CRC16:2]
```
- All multi-byte fields little-endian (matches eZ80 and ESP8266/ESP32).
- CRC covers FLAGS through end of PAYLOAD (SOF excluded). CRC-16/CCITT-FALSE,
  poly `0x1021`, init `0xFFFF`, no reflection. Test vector: ASCII `123456789`
  → `0x29B1`. Any change to the CRC parameters is a MAJOR change.
- FLAGS bits: `0x01 RESP`, `0x02 EVENT`, `0x04 ERR`. Reserved bits MUST be
  sent as 0 and MUST be ignored (not rejected) by receivers.
- **Bad CRC, an oversize LEN or a truncated frame → dropped silently, with
  no reply**, because the SEQ can't be trusted. The sender's retry recovers.
- A reply echoes TYPE and SEQ with `RESP` set. An error reply sets
  `RESP|ERR`, and its payload is `[err:u8][detail…]`. For example,
  `ERR_VERSION`'s detail is the ESP's major and minor.
- **Payload size:** each side advertises its *receive* limit as
  `max_payload` in `HELLO`. It must be ≥ `TINC_PAYLOAD_MIN` (64) and ≤
  `TINC_PAYLOAD_LIMIT` (1024). Before `HELLO` completes, both sides assume
  64.
- Recommended timing lives in `protocol.h` (`TINC_REPLY_TIMEOUT_MS`,
  `TINC_RETRY_MAX`, `TINC_WATCHDOG_MS`, …). The default baud is 115200.

## Core protocol rules

These rules exist for reasons discussed at length during design; do not
"simplify" them without understanding why they're there.

1. **Stop-and-wait, one request in flight.** The CE never has more than one
   frame outstanding. v1 explicitly does not support concurrent requests.
2. **SEQ + reply caching prevents duplicate side effects.** A lost reply must
   not cause the ESP to re-execute a request (critical for `REQ_BEGIN` and
   `BODY_WRITE` on a POST — a double-submit is a real-world bug, not a
   theoretical one).
   - The CE bumps SEQ for each new frame and reuses it on a retry.
   - The ESP caches its last reply and replays it verbatim for a repeated
     SEQ. It never re-runs the command.
   - **Exception:** `HELLO` is always executed, never replayed. It is
     idempotent and clears the cache.
3. **Offsets, not ACK counters, drive body streaming.** `BODY_READ`/
   `BODY_WRITE` use absolute byte offsets.
   - Re-requesting the last offset re-delivers the last chunk (safe retry).
   - Requesting `last_offset+last_len` advances and frees the old chunk.
   - Any other offset is `ERR_BAD_OFFSET`. There is no seeking backward
     beyond the last chunk: this is a streaming protocol, not a
     random-access one.
   - **An empty chunk is never cached.** A retry and an advance would use
     the same offset, so both are treated as a fresh read.
4. **The ESP never pushes data unsolicited.** Every body byte crosses the
   wire because the CE asked for it. The CE's RAM is the scarce resource;
   the protocol is pull-based specifically because of that. The ESP clamps
   `max_len` to fit the CE's `max_payload`.
5. **`BODY_READ` supports short long-polling** via `wait_ms` (clamped to
   100ms) to cut empty round trips without blocking the CE. It must return
   early if any other frame needs to be processed (e.g. an abort) — a held
   read must never block an abort.
6. **Unknown TYPE → `ERR_UNSUPPORTED`, never silence.** This is how new
   message types can be added across MINOR versions without a hang on the
   older side.
7. **Boot state / reset detection.** The ESP boots un-handshaken, and every
   message except `HELLO` gets `ERR_NO_HELLO` until `HELLO` succeeds. There
   is no session ID scheme: `ERR_NO_HELLO` on a previously-handshaken link
   is itself the reset signal.
   - The CE side must treat it as "ESP reset, re-handshake automatically".
   - It surfaces `TINC_ERR_ESP_RESET` to the app for any request that was
     in flight. That is a tinclib API code, not a wire code.
   - A POST must never be silently resent after a reset.
8. **Per-phase timeouts, not one total timeout**, for request state
   (connect / TLS / wait-headers / inter-byte-gap on body). `REQ_BEGIN`'s
   `timeout_s` applies per phase, and 0 means the 10s default. A
   slow-but-alive transfer should not be killed by a timeout meant to catch
   a hung one.
9. **CE-silence watchdog on the ESP.** If the ESP hears nothing for ~30s
   with a request active, it frees that request's resources. This is a
   safety net for a CE that crashed or was unplugged mid-request.

## Message types (0.3 — see protocol.h for layouts)

| Type | Name | Notes |
|---|---|---|
| `0x01` | `HELLO` | major, minor, caps (0), max_payload; the reply adds free_heap and wifi_slots (the firmware's slot count, 1..254). Always executed, never replayed |
| `0x02` | `STATUS` | wifi_state, slot, rssi, ip, free_heap, req_state, flags (`0x01 WIFI_LOCKED`). time_valid and insecure_enabled get appended when HTTPS lands |
| `0x10` | `REQ_BEGIN` | method, flags, timeout_s, content_len, url_len, hdr_len, url, headers. **0.3: GET and `http://` only.** content_len must be 0 (reserved for POST). Flag `0x01 TRANSCODE` |
| `0x11` | `REQ_STATUS` | state, err, http_status, content_len, ctype — folds in what would've been REQ_INFO |
| `0x13` | `HDR_GET` | **reserved, not implemented** — needed for reading `Location` on 3xx |
| `0x14` | `REQ_ABORT` | works in any state, including idle; frees resources immediately |
| `0x20` | `BODY_WRITE` | **reserved, planned (POST)**. Offset-based, credit/window flow control from the ESP |
| `0x21` | `BODY_READ` | offset, max_len, wait_ms → offset, flags (`0x01 EOF`), data |
| `0x40` | `WIFI_GET` | slot → ssid, wflags for that one slot. One slot per frame, so it always fits in the 64-byte minimum payload. Never passwords. Works while locked. `ERR_BAD_ARG` if slot ≥ wifi_slots |
| `0x41` | `WIFI_SET` | slot, ssid, password (write-only), wflags (`0x01 HIDDEN`). Saves the slot and triggers a reconnect. `ERR_LOCKED` while locked |
| `0x42` | `WIFI_FORGET` | slot. `ERR_LOCKED` while locked |
| `0x43+` | admin | scan, insecure-mode toggle and CA bundle update will come later. **Admin commands are not access-controlled on the wire**; see Open Questions |
| `0x80` | `BOOT` event | **reserved, not in 0.3.** Optional hint, not authoritative; the CE must still poll |

The ESP auto-connects to the first reachable Wi-Fi slot, trying 0 → wifi_slots−1. A
hidden slot never appears in a scan, so the ESP tries it directly.

**Wi-Fi lock.** It is set on the ESP only, by a firmware build flag or a
physical switch. Nothing on the wire can set or clear it: that would be a
software-only gate, which Open Questions rules out without discussion.
- `STATUS` reports it.
- `WIFI_SET` / `WIFI_FORGET` return `ERR_LOCKED`.
- `WIFI_GET` still works.

**Request state machine.** Values are fixed and all defined now, so a
newer ESP never sends a state an older CE doesn't know:
`IDLE 0`, `CONNECTING 1`, `TLS 2`, `SENDING 3`, `WAIT_HEADERS 4`, `BODY 5`,
`DONE 6`, `ERROR 7`.
- `TLS` is never sent in 0.3.
- `DONE` means EOF has been delivered to the CE.
- `ERROR` can happen at any point, and carries an error code.
- `REQ_BEGIN` on top of an active request returns `ERR_BUSY`. On top of a
  `DONE`/`ERROR` request it implicitly releases the old one.
- `DONE` stays readable for ~2s, so a retried final `BODY_READ` still works.
- `BODY_READ` is valid only in `BODY`/`DONE`. In `ERROR` it gets an error
  reply carrying the request's err; in any other state, `ERR_BAD_STATE`.
- GET follows up to 5 redirects.

## Design constraints baked into this protocol

- **Scope is HTTP/HTTPS in v1; 0.3 is plain-HTTP GET only.**
  - `https://` returns `ERR_UNSUPPORTED_SCHEME`. So does a GET redirected
    to https, which is common: many sites redirect http→https.
  - No raw TCP sockets are exposed to apps.
  - No Bluetooth. That's v2, and the ESP8266 doesn't have it anyway.
    BLE-only or BT-Classic capability depends on the chip, and must go
    through `HELLO` caps bits when it arrives.
- **The ESP owns all HTTP semantics**: DNS, TCP, TLS, redirects (GET/HEAD
  only — POST/PUT/DELETE return the 3xx directly, since following a 307/308
  would require buffering the whole request body in ESP RAM), chunked
  transfer decoding, gzip (ESP requests uncompressed content). The CE only
  ever sees plain decoded bytes.
- **The firmware decides how many Wi-Fi profiles there are**, and reports
  the count in `HELLO`. Never assume a fixed number on the CE side. There is
  no API-key/credential storage on the ESP. Passwords are write-only on the
  wire — no command reads one back, and `WIFI_GET` returns the SSID and flags
  only. API keys/auth headers are the app's
  responsibility, sent per-request in the headers field; the firmware must
  never persist or log request headers.
- **Insecure TLS mode is double-gated** (from when HTTPS lands). A request's
  `INSECURE` flag only works if TINCLIBC's global "allow insecure" setting
  is also on (`ERR_INSECURE_DISABLED` otherwise). No per-request-only bypass
  exists.
- **ASCII transcoding is in 0.3**, controlled by the `TRANSCODE` request
  flag.
  - It converts curly quotes, em-dashes, accented chars and the like to
    calculator-safe ASCII.
  - It applies to `text/*`, JSON and XML only; other content types pass
    through untouched.
  - While transcoding is active, `content_len` is reported as unknown
    (`0xFFFFFFFF`), since the decoded length isn't known up front.

## Open questions / known gaps (flag, don't silently resolve)

- **`HDR_GET` (0x13) is reserved but unimplemented.** Needed the moment any
  app needs to read `Location` after a POST redirect. If you're asked to
  implement redirect-following behavior for non-GET methods, implementing
  this message is the correct fix — do not add a workaround that fakes it
  through another message type.
- **Admin commands (0x40+) have no wire-level access control.** Any program
  that opens the serial link could send them. A physical-button confirmation
  scheme (require a press on the ESP within ~10s window for sensitive admin
  ops) was discussed but not decided. Do not implement an alternative
  software-only gate (like a PIN over serial) without raising this
  explicitly — it wasn't the direction favored in design discussion. The
  Wi-Fi lock (added in 0.2) is consistent with this: it is set physically or at build
  time and only reported on the wire. It covers Wi-Fi profiles only, not
  admin commands in general.
- **Baud negotiation is deferred.** 0.3 runs at a fixed 115200, and `HELLO`
  has no baud field yet; it will be appended. Don't wire up a runtime
  baud-switch without confirming the CE-side driver situation (see tinclib's
  AGENTS.md — the `srldrvce` chip-support situation directly affects this).

## Testing expectations

- Every message type needs a golden frame, generated by
  `tools/gen_vectors.py`. It must include:
  - a request and a reply for each type.
  - error replies.
  - forward-compat frames (a reserved flag bit set, trailing payload bytes)
    that must be accepted.
  - bad frames (bad CRC, LEN over the limit, truncated) that must be
    rejected.
- The CRC test vector (`123456789` → `0x29B1`) must pass identically when
  compiled for CE (eZ80), ESP8266 (Xtensa), and the PC test tools (native).
  `uint16_t`/`uint8_t` explicit typing throughout — the eZ80's native `int`
  is 24-bit, so anything relying on plain `int` width is a latent bug when
  shared with 32-bit builds.
- `tests/fuzz_frame.c` feeds random/truncated bytes to the parser. Consumers
  can run it against their own parser implementation by swapping in their
  own parser behind the same three calls, not just this repo's
  `tinc_frame.c`.
