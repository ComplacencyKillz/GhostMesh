// GhostMesh payload file transfer — the `/put` (upload) and `/get` (download) protocols, plus `/ls`.
//
// This is the firmware side of the web configurator's file uploader AND its mirror-image
// downloader — how a payload gets onto a node (/put) and then off it again, onto whatever's asking
// (/get): the web configurator (verify an upload), or the FAP wired to this backpack, pulling a
// payload down to the Flipper's own SD card so it can hand off to Bad USB (see /run in
// CommandModule.cpp and payloads/README.md). Split out of CommandModule.cpp (these are CommandModule
// member methods, defined here) only to keep that file readable — the transfer state lives as
// file-scope statics below, so no heavy FS includes leak into the header.
//
// WHY chunked text and not YMODEM: every serial path on a Meshtastic node is the PROTO StreamAPI,
// not a raw TTY — the USB port is the same protobuf console the web client does want_config over.
// YMODEM's raw framing would be eaten by the protobuf parser. So a file rides the ONLY channel we
// control: TEXT_MESSAGE_APP packets, base64-chunked, verified with a CRC32. (Meshtastic does ship
// its own XModem-over-protobuf transfer — src/xmodem.cpp — but that rides a dedicated protobuf
// field between PhoneAPI and the LOCAL client only; it can't be addressed to a remote node over the
// mesh, which is the whole point here, so it doesn't fit and isn't used.)
//
// WHY stop-and-wait, both directions: a rapid BURST of self-addressed packets overruns Meshtastic's
// serial ingest — the first gets through and the rest are dropped before they ever reach a module.
// So each direction is paced one message at a time: /put's node ACKs every data chunk (phone-only,
// no LoRa airtime) and the client waits for it before sending the next; /get's client ACKs every
// chunk it receives and the node waits for that before sending the next. Either self-addressed
// exchange can also be DOUBLE-DELIVERED (each packet handed to modules twice) — /put's ack and /get's
// chunk-send are both computed into a single deferred "pending" slot in the RX handler and only
// flushed once per runOnce tick, so a duplicate delivery just re-sets the same pending value instead
// of emitting two sends.
//
// Wire protocol (each line is one addressed text message; @id = this node's last-4 hex):
//   /put @id begin <fid> <nchunks> <totalbytes> <crc32hex> <name>   -> PUT <fid> ready <n>
//   /put @id d <fid> <index> <base64>                               -> PUT <fid> a <highest-contiguous>
//   /put @id end <fid>            -> PUT <fid> ok <bytes> | need <nextIndex> | crcfail | sizefail
// A stalled transfer (no traffic for PUT_TIMEOUT_MS) is aborted from runOnce -> PUT <fid> timeout.
// Chunks are appended in order (stop-and-wait guarantees ordering), so no per-chunk seek/bitmap.
//
//   /ls @id                        -> one reply per file: "LS <name> <bytes>", then "LS end <count>"
//   /get @id begin <name>          -> GET <fid> begin <nchunks> <bytes> <crc32hex> <name>
//                                      (immediately followed by the first data line, unprompted)
//                                   -> GET <fid> d 0 <base64>
//   /get @id ack <fid> <index>     -> GET <fid> d <index+1> <base64>   (index was the node's last send)
//                                   -> GET <fid> ok <crc32hex>          (index was the LAST chunk)
//                                   -> GET <fid> d <index> <base64>     (any other ack: re-send current)
// Not found / too big -> "GET 0 notfound" / "GET 0 toobig max=<KB>KB". A stalled download is aborted
// from runOnce (same PUT_TIMEOUT_MS) -> "GET <fid> timeout". Replies are ROUTED to whoever asked (see
// getSendRouted): phone-only if that's this node's own USB/serial client, directed unicast otherwise
// — so a remote mesh client can /get a small payload too, just slower.
//
// Build-time API to sanity-check against tag v2.7.15.567b8ea (fix in one line if it shifted):
// FSCommon.h exposes `FSCom` (LittleFS on ESP32) + the FILE_O_WRITE / FILE_O_READ open-mode macros;
// File supports write()/read()/close()/size()/seek() (single-arg seek confirmed in src/xmodem.cpp);
// FSCom.totalBytes()/usedBytes() for the free check; FSCom.open(dir) + file.openNextFile() +
// file.name()/size()/isDirectory() for listing (confirmed in src/FSCommon.cpp's own listDir/getFiles).

#include "CommandModule.h"
#include "FSCommon.h"      // FSCom (LittleFS on ESP32), FILE_O_WRITE / FILE_O_READ
#include "NodeDB.h"        // nodeDB (targetsMe, self node num for off-air acks)
#include "GhostMeshConfig.h" // ghostmesh_config.repStatus (gates /ls, same class as /status)
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
#define GET_CHUNK         132     // raw bytes per outgoing chunk — matches /put's chunk size

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

// ── GET (download) state — mirrors PUT with roles reversed. Deferred single-pending-send slot
// (g_pendingReady/g_pendingIdx/g_pendingFinish) absorbs double-delivery of the client's ack: a
// duplicate ack just re-sets the same pending value, so only one line actually goes out per tick.
static bool     g_active = false;
static uint32_t g_fid = 0;
static uint32_t g_nchunks = 0;
static uint32_t g_total = 0;
static uint32_t g_crc = 0;        // CRC32 of the whole file, computed once at begin
static long     g_sentIdx = -1;   // highest chunk index actually sent (-1 = none yet)
static uint32_t g_lastRxAt = 0;
static uint32_t g_getTo = 0;      // who to route replies to (see getSendRouted)
static File     g_file;
static bool     g_pendingReady = false;
static long     g_pendingIdx = 0;
static bool     g_pendingFinish = false;

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

// ── base64 encode (standard alphabet, '=' padded) → count of chars written (excl. NUL) ──
static const char B64_ALPHA[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64encode(const uint8_t *in, int inlen, char *out, int outmax)
{
    int o = 0, i = 0;
    for (; i + 3 <= inlen && o + 4 < outmax; i += 3) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = B64_ALPHA[(v >> 18) & 0x3F];
        out[o++] = B64_ALPHA[(v >> 12) & 0x3F];
        out[o++] = B64_ALPHA[(v >> 6) & 0x3F];
        out[o++] = B64_ALPHA[v & 0x3F];
    }
    int rem = inlen - i;
    if (rem == 1 && o + 4 < outmax) {
        uint32_t v = (uint32_t)in[i] << 16;
        out[o++] = B64_ALPHA[(v >> 18) & 0x3F];
        out[o++] = B64_ALPHA[(v >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rem == 2 && o + 4 < outmax) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[o++] = B64_ALPHA[(v >> 18) & 0x3F];
        out[o++] = B64_ALPHA[(v >> 12) & 0x3F];
        out[o++] = B64_ALPHA[(v >> 6) & 0x3F];
        out[o++] = '=';
    }
    out[o] = '\0';
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

// ── /ls: one reply line per staged file, then a count sentinel — reuses the normal (paced) reply
// queue since this is a rare, operator-driven query, not a high-rate stream like /put or /get. ────
void CommandModule::doLs()
{
    if (!ghostmesh_config.repStatus) return; // same "read node state" class as /status
    File root = FSCom.open(PUT_DIR, FILE_O_READ); // matches FSCommon.cpp's own listDir/getFiles
    if (!root || !root.isDirectory()) {
        enqueueReply("LS end 0"); // no /ghostmesh dir yet == nothing staged
        return;
    }
    uint8_t count = 0;
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        if (f.isDirectory())
            continue;
        char line[64];
        snprintf(line, sizeof(line), "LS %s %u", f.name(), (unsigned)f.size());
        enqueueReply(line);
        count++;
    }
    char end[16];
    snprintf(end, sizeof(end), "LS end %u", count);
    enqueueReply(end);
}

static void abortGet()
{
    if (g_active && g_file)
        g_file.close();
    g_active = false;
    g_pendingReady = false;
}

// ── Route a GET reply to whoever asked: phone-only (no LoRa TX) if that's this node's own
// USB/serial client, directed unicast otherwise — mirrors the routing the reply queue already does
// for ordinary commands (see handleCommandText's curReplyTo), so a remote mesh client can /get a
// small payload too, just paced by LoRa airtime instead of instant over USB.
void CommandModule::getSendRouted(const char *msg)
{
    if (g_getTo == nodeDB->getNodeNum())
        sendTextToPhone(msg);
    else
        sendTextTo(msg, g_getTo);
}

// ── /get begin: open the named file, CRC it up front, reply begin + immediately send chunk 0.
// Tokens after "begin": <name>
void CommandModule::getBegin(char *save, uint32_t from)
{
    char *nameS = strtok_r(nullptr, " ", &save);
    if (!nameS) {
        g_getTo = from;
        getSendRouted("GET 0 badargs");
        return;
    }

    char name[40];
    sanitizeName(nameS, name, sizeof(name));
    char path[56];
    snprintf(path, sizeof(path), "%s/%s", PUT_DIR, name);

    abortGet(); // supersede any prior download
    g_getTo = from;

    g_file = FSCom.open(path, FILE_O_READ);
    if (!g_file) {
        getSendRouted("GET 0 notfound");
        return;
    }
    uint32_t total = g_file.size();
    if (total == 0 || total > PUT_MAX_BYTES) {
        g_file.close();
        char r[40];
        snprintf(r, sizeof(r), "GET 0 toobig max=%uKB", (unsigned)(PUT_MAX_BYTES / 1024));
        getSendRouted(r);
        return;
    }
    uint32_t crc = 0;
    uint8_t rbuf[256];
    int n;
    while ((n = g_file.read(rbuf, sizeof(rbuf))) > 0)
        crc = crc32_update(crc, rbuf, n);
    g_file.seek(0); // rewind — we'll re-read chunk-by-chunk as acks arrive

    static uint32_t s_getFidCounter = 0x1000; // monotonic; only one GET is active at a time
    g_active = true;
    g_fid = ++s_getFidCounter;
    g_total = total;
    g_crc = crc;
    g_nchunks = (total + GET_CHUNK - 1) / GET_CHUNK;
    g_sentIdx = -1;
    g_lastRxAt = millis();

    char reply[80];
    snprintf(reply, sizeof(reply), "GET %x begin %u %u %x %s", (unsigned)g_fid, (unsigned)g_nchunks,
             (unsigned)g_total, (unsigned)g_crc, name);
    getSendRouted(reply);
    LOG_INFO("GET begin fid=%x '%s' %u chunks / %u bytes", (unsigned)g_fid, name, (unsigned)g_nchunks,
             (unsigned)g_total);

    // Send chunk 0 immediately — the client doesn't have to ack "begin" separately.
    g_pendingIdx = 0;
    g_pendingFinish = false;
    g_pendingReady = true;
}

// ── /get ack: the client confirms it has chunk <index>. Tokens: <fid> <index> ──────────
// idx == our last-sent index -> advance (or finish, if that was the last chunk). Anything else
// (a stale/duplicate ack, or the last send's ack got lost) -> re-send what we last sent. Either way
// this only SETS the pending slot; serviceGetSend (runOnce) does the actual file read + send, so a
// double-delivered ack just re-sets the same value instead of sending twice.
void CommandModule::getAck(char *save)
{
    char *fidS = strtok_r(nullptr, " ", &save);
    char *idxS = strtok_r(nullptr, " ", &save);
    if (!fidS || !idxS)
        return;
    if (!g_active || (uint32_t)strtoul(fidS, nullptr, 16) != g_fid)
        return;

    long idx = (long)strtoul(idxS, nullptr, 10);
    g_lastRxAt = millis();

    if (idx == g_sentIdx) {
        if ((uint32_t)(g_sentIdx + 1) >= g_nchunks) {
            g_pendingFinish = true;
        } else {
            g_pendingIdx = g_sentIdx + 1;
            g_pendingFinish = false;
        }
    } else {
        // Not caught up to our latest send — re-send it (idempotent; harmless if it's a genuine dup).
        g_pendingIdx = g_sentIdx;
        g_pendingFinish = false;
    }
    g_pendingReady = true;
}

// ── Called every runOnce tick: flush the pending GET send (a data chunk, or the closing "ok") ──
void CommandModule::serviceGetSend()
{
    if (!g_pendingReady)
        return;
    g_pendingReady = false;

    if (g_pendingFinish) {
        char r[24];
        snprintf(r, sizeof(r), "GET %x ok %x", (unsigned)g_fid, (unsigned)g_crc);
        getSendRouted(r);
        LOG_INFO("GET ok fid=%x %u bytes crc=%x", (unsigned)g_fid, (unsigned)g_total, (unsigned)g_crc);
        if (g_file) g_file.close();
        g_active = false;
        return;
    }

    uint8_t raw[GET_CHUNK];
    g_file.seek((uint32_t)g_pendingIdx * GET_CHUNK);
    int n = g_file.read(raw, sizeof(raw));
    if (n <= 0) {
        // Shouldn't happen (nchunks is computed from the same size) — abort rather than hang forever.
        getSendRouted("GET readfail");
        abortGet();
        return;
    }
    char b64[192];
    b64encode(raw, n, b64, sizeof(b64));
    // "GET " + up to 8 hex fid digits + " d " + up to ~10 decimal index digits + " " + b64 + NUL —
    // sized generously (matches the ~231-byte MeshPacket text cap) so snprintf can't silently
    // truncate the base64 payload itself, which would corrupt the chunk instead of just erroring.
    char line[256];
    snprintf(line, sizeof(line), "GET %x d %ld %s", (unsigned)g_fid, g_pendingIdx, b64);
    getSendRouted(line);
    g_sentIdx = g_pendingIdx;
}

// ── Called from runOnce: abort a download that has gone quiet mid-stream ────────────────
void CommandModule::serviceGetTimeout(uint32_t now)
{
    if (!g_active)
        return;
    if (now - g_lastRxAt < PUT_TIMEOUT_MS)
        return;
    char reply[24];
    snprintf(reply, sizeof(reply), "GET %x timeout", (unsigned)g_fid);
    getSendRouted(reply); // send while g_fid/g_getTo are still valid, then clean up
    abortGet();
}

// ── Dispatcher: called from handleReceived for any '/get ...' line addressed to us ─────
void CommandModule::handleGet(char *text, uint32_t from)
{
    char *save = nullptr;
    strtok_r(text, " ", &save);               // "/get"
    char *tgt = strtok_r(nullptr, " ", &save); // "@f69c"
    if (!targetsMe(tgt))
        return; // someone else's download — silent
    char *sub = strtok_r(nullptr, " ", &save); // begin | ack
    if (!sub)
        return;

    if (strcasecmp(sub, "ack") == 0)
        getAck(save);
    else if (strcasecmp(sub, "begin") == 0)
        getBegin(save, from);
}
