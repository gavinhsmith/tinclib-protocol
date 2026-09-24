/*
 * TINCLIB wire protocol — single source of truth.
 * Shared verbatim by tinclib (CE), tinclib-firmware (ESP8266) and PC tests.
 * Plain C99, no platform code. See AGENTS.md for the rules behind this file.
 *
 * Frame:  [SOF 0xA5][FLAGS:1][TYPE:1][SEQ:1][LEN:2][PAYLOAD:LEN][CRC16:2]
 *   - all multi-byte fields little-endian
 *   - CRC-16/CCITT-FALSE over FLAGS..end of PAYLOAD (SOF excluded)
 *   - bad CRC / oversize LEN / truncated frame: dropped silently, no reply
 *
 * Forward compatibility:
 *   - reserved FLAGS bits are sent as 0 and ignored on receipt
 *   - payloads are append-only: receivers MUST ignore trailing bytes past
 *     the fields they know; from 1.0, MINOR versions may only append fields
 *   - unknown TYPE -> error reply TINC_ERR_UNSUPPORTED
 *
 * Replies echo TYPE and SEQ with FLAGS RESP set. Error replies set
 * RESP|ERR and carry [err:u8][detail...].
 *
 * SEQ: the CE bumps SEQ for each new frame and reuses it on retry. The ESP
 * caches its last reply and replays it verbatim for a repeated SEQ, never
 * re-running the command. HELLO is the exception: always executed (it is
 * idempotent) and it clears the reply cache.
 */
#ifndef TINC_PROTOCOL_H
#define TINC_PROTOCOL_H

#include <stdint.h>

/* Pre-1.0: any MINOR bump may break the wire format, so while MAJOR is 0
 * HELLO requires an exact MAJOR.MINOR match (else ERR_VERSION). */
#define TINC_PROTO_MAJOR 0
#define TINC_PROTO_MINOR 4

/* ---- Framing ---------------------------------------------------------- */

#define TINC_SOF        0xA5u
#define TINC_HDR_LEN    6u      /* SOF FLAGS TYPE SEQ LEN(2) */
#define TINC_CRC_LEN    2u
#define TINC_OVERHEAD   (TINC_HDR_LEN + TINC_CRC_LEN)

#define TINC_FLAG_RESP  0x01u
#define TINC_FLAG_EVENT 0x02u   /* no events defined in 0.4 */
#define TINC_FLAG_ERR   0x04u

/* Each side advertises its RECEIVE limit in HELLO. Before HELLO completes
 * both sides assume TINC_PAYLOAD_MIN. */
#define TINC_PAYLOAD_MIN   64u
#define TINC_PAYLOAD_LIMIT 1024u

/* Recommended timing (CE driver / ESP firmware). */
#define TINC_BAUD_DEFAULT        115200ul
#define TINC_REPLY_TIMEOUT_MS    200u   /* plus wait_ms for BODY_READ */
#define TINC_RETRY_MAX           3u
#define TINC_INTERBYTE_RESET_MS  50u    /* call tinc_parser_reset after this gap */
#define TINC_WATCHDOG_MS         30000ul /* ESP frees active request after CE silence */
#define TINC_DONE_LINGER_MS      2000u  /* DONE request stays readable */
#define TINC_WAIT_MS_MAX         100u
#define TINC_TIMEOUT_S_DEFAULT   10u    /* per-phase, when timeout_s == 0 */
#define TINC_REDIRECT_MAX        5u
/* The ESP must answer every frame within TINC_REPLY_TIMEOUT_MS in every
 * request phase. DNS, connect and the TLS handshake run incrementally and
 * never block the link (so REQ_ABORT always gets through). */

/* ---- Message types ---------------------------------------------------- */

enum {
    TINC_T_HELLO       = 0x01,
    TINC_T_STATUS      = 0x02,
    TINC_T_REQ_BEGIN   = 0x10,
    TINC_T_REQ_STATUS  = 0x11,
    /* 0x13 HDR_GET    reserved, not in 0.4 */
    TINC_T_REQ_ABORT   = 0x14,
    /* 0x20 BODY_WRITE reserved (POST, planned) */
    TINC_T_BODY_READ   = 0x21,
    TINC_T_WIFI_GET    = 0x40,
    TINC_T_WIFI_SET    = 0x41,
    TINC_T_WIFI_FORGET = 0x42
    /* 0x80 BOOT event reserved */
};

/* ---- Error codes (u8) ------------------------------------------------- */

enum {
    TINC_OK                     = 0x00,
    /* link / command */
    TINC_ERR_UNSUPPORTED        = 0x01, /* unknown TYPE */
    TINC_ERR_NO_HELLO           = 0x02, /* ESP un-handshaken: reset signal */
    TINC_ERR_VERSION            = 0x03, /* detail: esp_major u8, esp_minor u8 */
    TINC_ERR_BAD_LEN            = 0x04, /* payload too short / lengths don't add up */
    TINC_ERR_BUSY               = 0x05,
    TINC_ERR_BAD_STATE          = 0x06,
    TINC_ERR_BAD_OFFSET         = 0x07,
    TINC_ERR_BAD_ARG            = 0x08,
    TINC_ERR_UNSUPPORTED_SCHEME = 0x09, /* not http:// or https:// */
    TINC_ERR_LOCKED             = 0x0A, /* Wi-Fi profiles locked on the ESP */
    /* 0x0B INSECURE_DISABLED reserved (INSECURE flag, not in 0.4) */
    /* request (REQ_STATUS.err, or BODY_READ error reply in ERROR state).
     * Detail: err_detail u8, a TINC_TLSR_* reason for ERR_TLS / ERR_CERT,
     * 0 for every other error. */
    TINC_ERR_WIFI_DOWN          = 0x20,
    TINC_ERR_DNS                = 0x21,
    TINC_ERR_CONNECT            = 0x22,
    TINC_ERR_TIMEOUT            = 0x23,
    TINC_ERR_HTTP_PROTO         = 0x24,
    TINC_ERR_TOO_MANY_REDIRECTS = 0x25,
    TINC_ERR_NO_MEM             = 0x26,
    TINC_ERR_TLS                = 0x27, /* handshake failed (not a cert problem) */
    TINC_ERR_CERT               = 0x28, /* chain, hostname or validity check failed */
    TINC_ERR_TIME               = 0x29, /* no valid clock within the TLS phase timeout */
    TINC_ERR_REDIRECT_DOWNGRADE = 0x2A  /* https -> http redirect, not followed */
};

/* TLS failure reasons (err_detail u8). Protocol-defined, not library codes:
 * the firmware maps its TLS library's errors onto these. */
enum {
    TINC_TLSR_OTHER         = 0x00, /* unknown / unmapped */
    /* with ERR_TLS */
    TINC_TLSR_VERSION       = 0x01, /* no common TLS version (ESP8266 tops out at 1.2) */
    TINC_TLSR_CIPHER        = 0x02, /* no common cipher suite */
    TINC_TLSR_ALERT         = 0x03, /* server sent a fatal alert */
    TINC_TLSR_PROTO         = 0x04, /* malformed / unexpected handshake message */
    /* with ERR_CERT */
    TINC_TLSR_EXPIRED       = 0x10,
    TINC_TLSR_NOT_YET_VALID = 0x11,
    TINC_TLSR_HOSTNAME      = 0x12, /* cert doesn't match the host */
    TINC_TLSR_UNTRUSTED     = 0x13, /* chain doesn't reach a built-in CA root */
    TINC_TLSR_BAD_CHAIN     = 0x14  /* bad signature, key usage, malformed cert */
};

/* ---- Request state (u8) ----------------------------------------------- */

enum {
    TINC_RS_IDLE         = 0,
    TINC_RS_CONNECTING   = 1,
    TINC_RS_TLS          = 2,   /* https: waiting for a valid clock, then handshake */
    TINC_RS_SENDING      = 3,
    TINC_RS_WAIT_HEADERS = 4,
    TINC_RS_BODY         = 5,
    TINC_RS_DONE         = 6,   /* EOF has been delivered to the CE */
    TINC_RS_ERROR        = 7
};

/* ---- Wi-Fi state (u8) ------------------------------------------------- */

enum {
    TINC_WIFI_NO_CREDS   = 0,
    TINC_WIFI_CONNECTING = 1,
    TINC_WIFI_CONNECTED  = 2,
    TINC_WIFI_FAILED     = 3
};

/* Slot count is defined by the firmware and reported in HELLO (wifi_slots).
 * Slots are numbered 0 .. wifi_slots-1. */
#define TINC_WIFI_SLOTS_MAX 254u /* 0xFF is TINC_SLOT_NONE */
#define TINC_SSID_MAX       32u
#define TINC_PASS_MAX       64u
#define TINC_SLOT_NONE      0xFFu

/* Per-slot Wi-Fi flags (WIFI_SET, WIFI_GET) */
#define TINC_WF_HIDDEN      0x01u /* SSID not broadcast: ESP connects without seeing it in a scan */

/* ---- Payload layouts (byte offsets) ------------------------------------
 * Offsets rather than packed structs: packed is not C99, and unaligned
 * member access is a trap on Xtensa. Read/write with the LE helpers below.
 */

/* HELLO req & resp (same leading layout)
 *   major u8, minor u8, caps u16 (0 in 0.4), max_payload u16
 *   resp only: free_heap u32, wifi_slots u8 (1 .. TINC_WIFI_SLOTS_MAX) */
#define TINC_HELLO_MAJOR        0
#define TINC_HELLO_MINOR        1
#define TINC_HELLO_CAPS         2
#define TINC_HELLO_MAX_PAYLOAD  4
#define TINC_HELLO_REQ_LEN      6
#define TINC_HELLO_FREE_HEAP    6
#define TINC_HELLO_WIFI_SLOTS   10
#define TINC_HELLO_RESP_LEN     11

/* STATUS req: empty
 * resp: wifi_state u8, slot u8, rssi i8, ip[4], free_heap u32, req_state u8,
 *   flags u8 */
#define TINC_STATUS_WIFI_STATE  0
#define TINC_STATUS_SLOT        1
#define TINC_STATUS_RSSI        2
#define TINC_STATUS_IP          3
#define TINC_STATUS_FREE_HEAP   7
#define TINC_STATUS_REQ_STATE   11
#define TINC_STATUS_FLAGS       12
#define TINC_STATUS_RESP_LEN    13

/* Wi-Fi lock: set on the ESP only (firmware build flag or physical
 * switch), never over the wire. While set, WIFI_SET and WIFI_FORGET return
 * ERR_LOCKED; WIFI_GET still works. */
#define TINC_STATUSF_WIFI_LOCKED 0x01u
/* The ESP clock is set (SNTP), so certificates can be checked. */
#define TINC_STATUSF_TIME_VALID  0x02u

/* REQ_BEGIN req: method u8, flags u8, timeout_s u8, content_len u32,
 *   url_len u16, hdr_len u16, url[url_len], hdrs[hdr_len]
 *   - method: GET only in 0.4 (else ERR_BAD_ARG)
 *   - content_len: must be 0 in 0.4 (field reserved for POST)
 *   - timeout_s: per phase (connect / TLS / wait-headers / body gap), 0 = default
 *   - hdrs: "Name: value\r\n" pairs; ESP never logs or stores them
 *   - http:// or https:// (anything else -> ERR_UNSUPPORTED_SCHEME)
 *   - https is always verified against the CA roots built into the
 *     firmware. The TLS phase first waits for a valid clock (ERR_TIME if
 *     the phase times out first), then handshakes (ERR_TLS / ERR_CERT).
 *   - redirects: http -> https is followed; https -> http is not
 *     (ERR_REDIRECT_DOWNGRADE), so the app's headers never go out in clear
 *   - a redirect to a different host drops all of the app's hdrs, so auth
 *     headers only ever reach the host the app named
 * resp: empty. Sync errors: BUSY, BAD_ARG, BAD_LEN, UNSUPPORTED_SCHEME,
 *   WIFI_DOWN. On top of DONE/ERROR the old request is released. */
#define TINC_BEGIN_METHOD       0
#define TINC_BEGIN_FLAGS        1
#define TINC_BEGIN_TIMEOUT_S    2
#define TINC_BEGIN_CONTENT_LEN  3
#define TINC_BEGIN_URL_LEN      7
#define TINC_BEGIN_HDR_LEN      9
#define TINC_BEGIN_URL          11  /* fixed part length */

#define TINC_METHOD_GET         1u

#define TINC_REQF_TRANSCODE     0x01u /* ASCII-transcode text/JSON/XML */
/* 0x02 INSECURE reserved (skip cert checks), not in 0.4 */

#define TINC_LEN_UNKNOWN        0xFFFFFFFFul

/* REQ_STATUS req: empty
 * resp: state u8, err u8, http_status u16, content_len u32,
 *   ctype_len u8, ctype[ctype_len <= 63], err_detail u8
 *   - content_len = TINC_LEN_UNKNOWN when unknown or transcoding
 *   - err_detail: TINC_TLSR_* for ERR_TLS / ERR_CERT, else 0. It sits at
 *     TINC_RSTAT_CTYPE + ctype_len */
#define TINC_RSTAT_STATE        0
#define TINC_RSTAT_ERR          1
#define TINC_RSTAT_HTTP_STATUS  2
#define TINC_RSTAT_CONTENT_LEN  4
#define TINC_RSTAT_CTYPE_LEN    8
#define TINC_RSTAT_CTYPE        9
#define TINC_CTYPE_MAX          63u

/* REQ_ABORT req/resp: empty. Always succeeds, even when idle. */

/* BODY_READ req: offset u32, max_len u16, wait_ms u8
 *   - valid in BODY/DONE; ERROR -> error reply carrying the request's err;
 *     other states -> ERR_BAD_STATE
 *   - offset == last_offset: re-deliver the identical chunk
 *   - offset == last_offset + last_len: next chunk, old one freed
 *   - anything else: ERR_BAD_OFFSET. First read is offset 0.
 *   - an empty chunk is never cached (retry == advance, both are fresh reads)
 *   - wait_ms clamped to TINC_WAIT_MS_MAX; ends early if any frame arrives
 *   - ESP clamps max_len to peer max_payload - TINC_READ_DATA
 * resp: offset u32, flags u8, data[LEN - 5] */
#define TINC_READ_OFFSET        0
#define TINC_READ_MAX_LEN       4
#define TINC_READ_WAIT_MS       6
#define TINC_READ_REQ_LEN       7
#define TINC_READ_FLAGS         4   /* resp */
#define TINC_READ_DATA          5   /* resp */

#define TINC_READF_EOF          0x01u

/* WIFI_GET req: slot u8
 * resp: ssid_len u8, ssid[ssid_len], wflags u8 (TINC_WF_*); ssid_len 0 = empty
 *   One slot per frame, so it always fits in TINC_PAYLOAD_MIN whatever the
 *   slot count. Slot >= wifi_slots -> ERR_BAD_ARG.
 * Passwords are write-only: no command ever returns one. */
#define TINC_WGET_SLOT          0   /* req */
#define TINC_WGET_SSID_LEN      0   /* resp */
#define TINC_WGET_SSID          1   /* resp; wflags follows ssid */

/* WIFI_SET req: slot u8, ssid_len u8, ssid[], pass_len u8, pass[], wflags u8
 *   - wflags: TINC_WF_* (HIDDEN)
 * resp: empty. Saves the slot and triggers reconnect. ESP auto-connects to
 * the first reachable slot, 0 -> wifi_slots-1; a hidden slot is tried directly since it
 * never shows up in a scan. Errors: ERR_LOCKED, ERR_BAD_ARG (slot out of range). */
#define TINC_WSET_SLOT          0
#define TINC_WSET_SSID_LEN      1
#define TINC_WSET_SSID          2

/* WIFI_FORGET req: slot u8. resp: empty. Errors: ERR_LOCKED, ERR_BAD_ARG. */
#define TINC_WFORGET_SLOT       0

/* NOTE: admin commands (0x40+) have no wire-level access control in 0.4.
 * Open question — see AGENTS.md. */

/* ---- Little-endian helpers -------------------------------------------- */

static inline uint16_t tinc_get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t tinc_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void tinc_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline void tinc_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

#endif /* TINC_PROTOCOL_H */
