// GhostMesh payload file transfer — the `/put` protocol (stop-and-wait).
//
// This is the firmware receiver for the web configurator's file uploader. It is split out of
// CommandModule.cpp (these are CommandModule member methods, defined here) only to keep that file
// readable — the transfer state lives as file-scope statics below, so no heavy FS includes leak
// into the header.
//
// WHY chunked text and not YMODEM: every serial path on a Meshtastic node is the PROTO StreamAPI,
// not a raw TTY — the USB port is the same protobuf console the web client does want_config over.
// YMODEM's raw framing would be eaten by the protobuf parser. So a file rides the ONLY channel we
// control: TEXT_MESSAGE_APP packets, base64-chunked, reassembled here and verified with a CRC32.
//
// WHY stop-and-wait: a rapid BURST of self-addressed packets overruns Meshtastic's serial ingest —
// the first gets through and the rest are dropped before they ever reach a module. So the node ACKs
// every data chunk (immediately, delivered phone-only via sendToPhone so it costs no LoRa airtime),
// and the client sends the next chunk only after the ACK. That paces the sender to the node's real
// capacity and makes each chunk individually confirmed — no burst, no silent loss.
//
// Wire protocol (each line is one addressed text message; @id = this node's last-4 hex):
//   /put @id begin <fid> <nchunks> <totalbytes> <crc32hex> <name>   -> PUT <fid> ready <n>
//   /put @id d <fid> <index> <base64>                               -> PUT <fid> a <highest-contiguous>
//   /put @id end <fid>            -> PUT <fid> ok <bytes> | need <nextIndex> | crcfail | sizefail
// A stalled transfer (no traffic for PUT_TIMEOUT_MS) is aborted from runOnce -> PUT <fid> timeout.
// Chunks are appended in order (stop-and-wait guarantees ordering), so no per-chunk seek/bitmap.
//
// Build-time API to sanity-check against tag v2.7.15.567b8ea (fix in one line if it shifted):
// FSCommon.h exposes `FSCom` (LittleFS on ESP32) + the FILE_O_WRITE / FILE_O_READ open-mode macros;
// File supports write()/read()/close(); FSCom.totalBytes()/usedBytes() for the free check.

#include "CommandModule.h"
#include "FSCommon.h"      // FSCom (LittleFS on ESP32), FILE_O_WRITE / FILE_O_READ
#include "NodeDB.h"        // nodeDB (targetsMe, self node num for off-air acks)
#include "configuration.h" // LOG_*
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// ── Tunables ─────────────────────────────────────────────────────────────────────────
#define PUT_MAX_CHUNKS    8192    // sanity cap on nchunks
#define PUT_MAX_BYTES     (512u * 1024u) // hard ceiling regardless of free space
#define PUT_TIMEOUT_MS    15000   // abort a transfer that goes quiet this long
#define PUT_DIR           "/ghostmesh"
#define PUT_FREE_MARGIN   (32u * 1024u)  // leave this much LittleFS headroom for Meshtastic itself
#define PUT_ACK_NONE      (-2)    // sentinel: no ack pending

// ── Transfer state (one active transfer; a new `begin` supersedes any prior) ───────────
static bool     s_active = false;
static uint32_t s_fid = 0;        // client-chosen id, echoed in every reply so it can correlate
static uint32_t s_nchunks = 0;
static uint32_t s_total = 0;
static uint32_t s_crc = 0;        // expected CRC32 of the whole file
static uint32_t s_nextIdx = 0;    // next in-order chunk we expect (== count received so far)
static uint32_t s_lastRxAt = 0;
static long     s_ackIdx = PUT_ACK_NONE; // runOnce sends "a <s_ackIdx>" then clears (fast, off queue)
static char     s_name[40];
static char     s_path[56];
static File     s_file;

// ── CRC32 (zlib/PNG polynomial, matches the browser's implementation) ──────────────────
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return ~crc;
}

// ── base64 decode (tolerates '=' padding and whitespace) → count of bytes, or -1 on bad char ──
static int b64val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
static int b64decode(const char *in, uint8_t *out, int outmax)
{
    int o = 0, acc = 0, nbits = 0;
    for (const char *p = in; *p; p++) {
        char c = *p;
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t')
            continue;
        int v = b64val(c);
        if (v < 0)
            return -1;
        acc = (acc << 6) | v;
        nbits += 6;
        if (nbits >= 8) {
            nbits -= 8;
            if (o < outmax)
                out[o++] = (uint8_t)((acc >> nbits) & 0xFF);
        }
    }
    return o;
}

// Sanitize an operator-supplied filename to a flat, printable basename (no path traversal).
static void sanitizeName(const char *in, char *out, size_t outsz)
{
    size_t o = 0;
    for (const char *p = in; *p && o < outsz - 1; p++) {
        char c = *p;
        if (c == '/' || c == '\\')
            continue; // flat namespace — no directories, no ../
        if (c < 0x20 || c > 0x7E)
            c = '_';
        out[o++] = c;
    }
    if (o == 0)
        out[o++] = 'f'; // never empty
    out[o] = '\0';
}

static void abortTransfer()
{
    if (s_active && s_file)
        s_file.close();
    s_active = false;
    s_ackIdx = PUT_ACK_NONE;
}

// ── /put begin: open the file, reset progress, validate size ───────────────────────────
// Tokens after "begin": <fid> <nchunks> <totalbytes> <crc32hex> <name>
void CommandModule::putBegin(char *save)
{
    char *fidS = strtok_r(nullptr, " ", &save);
    char *nS = strtok_r(nullptr, " ", &save);
    char *totS = strtok_r(nullptr, " ", &save);
    char *crcS = strtok_r(nullptr, " ", &save);
    char *nameS = strtok_r(nullptr, " ", &save);
    if (!fidS || !nS || !totS || !crcS || !nameS) {
        sendTextToPhone("PUT begin: bad args");
        return;
    }

    uint32_t fid = (uint32_t)strtoul(fidS, nullptr, 16);
    uint32_t nchunks = (uint32_t)strtoul(nS, nullptr, 10);
    uint32_t total = (uint32_t)strtoul(totS, nullptr, 10);
    uint32_t crc = (uint32_t)strtoul(crcS, nullptr, 16);

    char reply[64];
    if (nchunks == 0 || nchunks > PUT_MAX_CHUNKS || total == 0 || total > PUT_MAX_BYTES) {
        snprintf(reply, sizeof(reply), "PUT %x toobig max=%uKB", (unsigned)fid, (unsigned)(PUT_MAX_BYTES / 1024));
        sendTextToPhone(reply);
        return;
    }
    uint32_t freeb = (uint32_t)(FSCom.totalBytes() - FSCom.usedBytes());
    if (total + PUT_FREE_MARGIN > freeb) {
        snprintf(reply, sizeof(reply), "PUT %x nospace free=%uKB", (unsigned)fid, (unsigned)(freeb / 1024));
        sendTextToPhone(reply);
        return;
    }

    abortTransfer(); // supersede any prior transfer

    sanitizeName(nameS, s_name, sizeof(s_name));
    FSCom.mkdir(PUT_DIR); // no-op if it exists
    snprintf(s_path, sizeof(s_path), "%s/%s", PUT_DIR, s_name);
    s_file = FSCom.open(s_path, FILE_O_WRITE); // truncate; we append in order
    if (!s_file) {
        snprintf(reply, sizeof(reply), "PUT %x open-fail", (unsigned)fid);
        sendTextToPhone(reply);
        return;
    }

    s_active = true;
    s_fid = fid;
    s_nchunks = nchunks;
    s_total = total;
    s_crc = crc;
    s_nextIdx = 0;
    s_ackIdx = PUT_ACK_NONE;
    s_lastRxAt = millis();

    LOG_INFO("PUT begin fid=%x '%s' %u chunks / %u bytes", (unsigned)fid, s_name, (unsigned)nchunks,
             (unsigned)total);
    snprintf(reply, sizeof(reply), "PUT %x ready %u", (unsigned)fid, (unsigned)nchunks);
    sendTextToPhone(reply);
}

// ── /put d: one in-order data chunk → append + ACK. Tokens: <fid> <index> <base64> ─────
// Stop-and-wait: the ack (sent immediately from runOnce, off-air) is the client's cue to send the
// next chunk. We ack the highest CONTIGUOUS index we hold, so a lost ack or a resent chunk just
// re-acks and the client advances. A gap (idx > expected) re-acks the last good one → client resends.
void CommandModule::putData(char *save)
{
    char *fidS = strtok_r(nullptr, " ", &save);
    char *idxS = strtok_r(nullptr, " ", &save);
    char *b64 = strtok_r(nullptr, " ", &save);
    if (!fidS || !idxS || !b64)
        return;
    if (!s_active || (uint32_t)strtoul(fidS, nullptr, 16) != s_fid)
        return;

    uint32_t idx = (uint32_t)strtoul(idxS, nullptr, 10);
    if (idx == s_nextIdx) {
        uint8_t bytes[256];
        int n = b64decode(b64, bytes, sizeof(bytes));
        if (n <= 0)
            return; // corrupt chunk — don't ack; client's ack-wait times out and resends
        s_file.write(bytes, n);
        s_nextIdx++;
    }
    // else: duplicate (idx < nextIdx) or gap (idx > nextIdx) — no write, just re-ack below.

    s_lastRxAt = millis();
    s_ackIdx = (long)s_nextIdx - 1; // highest contiguous index we hold (-1 before any chunk)
}

// ── /put end: verify size + CRC32 over the reassembled file. Tokens: <fid> ─────────────
void CommandModule::putEnd(char *save)
{
    char *fidS = strtok_r(nullptr, " ", &save);
    if (!fidS)
        return;
    uint32_t fid = (uint32_t)strtoul(fidS, nullptr, 16);
    char reply[64];

    if (!s_active || fid != s_fid) {
        snprintf(reply, sizeof(reply), "PUT %x noxfer", (unsigned)fid);
        sendTextToPhone(reply);
        return;
    }
    if (s_nextIdx < s_nchunks) {
        // Missing the tail — tell the client where to resume (stop-and-wait shouldn't reach here).
        snprintf(reply, sizeof(reply), "PUT %x need %u", (unsigned)fid, (unsigned)s_nextIdx);
        sendTextToPhone(reply);
        return;
    }

    s_file.flush();
    s_file.close();

    File rf = FSCom.open(s_path, FILE_O_READ);
    if (!rf) {
        s_active = false;
        snprintf(reply, sizeof(reply), "PUT %x reopen-fail", (unsigned)fid);
        sendTextToPhone(reply);
        return;
    }
    uint32_t sz = rf.size();
    if (sz != s_total) {
        rf.close();
        s_active = false;
        snprintf(reply, sizeof(reply), "PUT %x sizefail got=%u", (unsigned)fid, (unsigned)sz);
        sendTextToPhone(reply);
        return;
    }
    uint32_t crc = 0;
    uint8_t rbuf[256];
    int r;
    while ((r = rf.read(rbuf, sizeof(rbuf))) > 0)
        crc = crc32_update(crc, rbuf, r);
    rf.close();
    s_active = false;

    if (crc != s_crc) {
        snprintf(reply, sizeof(reply), "PUT %x crcfail", (unsigned)fid);
        sendTextToPhone(reply);
        return;
    }

    LOG_INFO("PUT ok fid=%x '%s' %u bytes crc=%x", (unsigned)fid, s_name, (unsigned)sz, (unsigned)crc);
    snprintf(reply, sizeof(reply), "PUT %x ok %u", (unsigned)fid, (unsigned)sz);
    sendTextToPhone(reply);
}

// ── Dispatcher: called from handleReceived for any '/put ...' line addressed to us ─────
void CommandModule::handlePut(char *text)
{
    char *save = nullptr;
    strtok_r(text, " ", &save);               // "/put"
    char *tgt = strtok_r(nullptr, " ", &save); // "@f69c"
    if (!targetsMe(tgt))
        return; // someone else's transfer — silent
    char *sub = strtok_r(nullptr, " ", &save); // begin | d | end
    if (!sub)
        return;

    if (strcasecmp(sub, "d") == 0)
        putData(save);
    else if (strcasecmp(sub, "begin") == 0)
        putBegin(save);
    else if (strcasecmp(sub, "end") == 0)
        putEnd(save);
}

// ── Called every runOnce tick: emit a pending chunk ACK immediately (not via the 800 ms reply
// queue — flow control must be fast). Delivered phone-only (sendToPhone) so it reaches the connected
// web/serial client with NO LoRa transmit — a mesh-transmitted ack costs ~1 s of airtime each and
// throttled the whole transfer to a crawl.
void CommandModule::servicePutAck()
{
    if (s_ackIdx == PUT_ACK_NONE)
        return;
    char r[24];
    snprintf(r, sizeof(r), "PUT %x a %ld", (unsigned)s_fid, s_ackIdx);
    s_ackIdx = PUT_ACK_NONE;
    sendTextToPhone(r); // straight to the connected client, no LoRa TX — keeps acks ~instant
}

// ── Called from runOnce: abort a transfer that has gone quiet mid-stream ────────────────
void CommandModule::servicePutTimeout(uint32_t now)
{
    if (!s_active)
        return;
    if (now - s_lastRxAt < PUT_TIMEOUT_MS)
        return;
    char reply[32];
    snprintf(reply, sizeof(reply), "PUT %x timeout", (unsigned)s_fid);
    abortTransfer();
    sendTextToPhone(reply);
}
