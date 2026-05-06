#include "proto_mode.h"
#include "uart_helper.h"

#include <furi.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ── Protobuf wire-type constants ─────────────────────────────────────────────

#define PB_WIRE_VARINT  0
#define PB_WIRE_BYTES   2

// Build a field tag byte for field_num with the given wire type.
// For field numbers ≤ 15 this fits in one varint byte.
#define PB_TAG(field, wire) (((field) << 3) | (wire))

// Meshtastic PortNum values
#define PORTNUM_TEXT_MESSAGE 1

// Broadcast node ID
#define MESH_BROADCAST_ADDR 0xFFFFFFFFu

// ── Varint encoder/decoder ────────────────────────────────────────────────────

static size_t write_varint(uint64_t v, uint8_t* out) {
    size_t n = 0;
    do {
        uint8_t b = v & 0x7F;
        v >>= 7;
        if(v) b |= 0x80;
        out[n++] = b;
    } while(v);
    return n;
}

static bool read_varint(const uint8_t* buf, size_t len, size_t* pos, uint64_t* out) {
    *out = 0;
    int shift = 0;
    // MED-4: A protobuf varint is at most 10 bytes (64-bit value, 7 bits per byte).
    // shift runs 0, 7, 14, ... 63 — 10 iterations, all satisfy shift < 64.
    // An 11-byte (overlong) varint exits the loop and returns false.
    while(*pos < len && shift < 64) {
        uint8_t b = buf[(*pos)++];
        *out |= (uint64_t)(b & 0x7F) << shift;
        if(!(b & 0x80)) return true;
        shift += 7;
    }
    return false;
}

// ── Protobuf field writers ────────────────────────────────────────────────────

static size_t write_varint_field(uint8_t* out, uint32_t field, uint64_t value) {
    size_t n = 0;
    n += write_varint(PB_TAG(field, PB_WIRE_VARINT), out + n);
    n += write_varint(value, out + n);
    return n;
}

static size_t write_bytes_field(uint8_t* out, uint32_t field,
                                 const uint8_t* data, size_t data_len) {
    size_t n = 0;
    n += write_varint(PB_TAG(field, PB_WIRE_BYTES), out + n);
    n += write_varint(data_len, out + n);
    memcpy(out + n, data, data_len);
    n += data_len;
    return n;
}

// ── Protobuf field readers ────────────────────────────────────────────────────

// Skip one unknown field given its wire type.
static bool skip_field(const uint8_t* buf, size_t len, size_t* pos, uint32_t wire) {
    uint64_t tmp;
    switch(wire) {
    case 0: return read_varint(buf, len, pos, &tmp);
    case 1: if(*pos + 8 > len) return false; *pos += 8; return true;
    case 2: {
        if(!read_varint(buf, len, pos, &tmp)) return false;
        // HIGH-3: Use subtraction form to avoid size_t truncation of 64-bit tmp.
        // len >= *pos is guaranteed because read_varint only advances within len.
        if(tmp > (uint64_t)(len - *pos)) return false;
        *pos += (size_t)tmp;
        return true;
    }
    case 5: if(*pos + 4 > len) return false; *pos += 4; return true;
    default: return false;
    }
}

// ── ToRadio encoder ───────────────────────────────────────────────────────────
//
// Field numbers confirmed from meshtastic Python library serialization:
//   MeshPacket.to        = field 2, fixed32 (wire type 5) — NOT field 3 varint
//   MeshPacket.decoded   = field 4, bytes
//   MeshPacket.hop_limit = field 9, varint               — NOT field 13
//
// MED-5: Stack buffer sizes in proto_encode_text are bounded by text_len <= 80:
//   data_buf[96]:  portnum(2) + payload_hdr(2) + text(80) = 84 bytes max
//   mesh_buf[160]: to(5) + decoded_hdr(2) + data(84) + hop_limit(2) = 93 bytes max
//   radio_buf[256]: packet_hdr(2) + mesh(93) = 95 bytes max
// If PROTO_TEXT_MAX_LEN is increased beyond 80, these buffers must be recalculated.
#define PROTO_TEXT_MAX_LEN 80

static size_t write_fixed32_field(uint8_t* out, uint32_t field, uint32_t value) {
    size_t n = 0;
    // Wire type 5 = 32-bit fixed
    n += write_varint(PB_TAG(field, 5), out + n);
    // Little-endian 4 bytes
    out[n++] = (uint8_t)(value);
    out[n++] = (uint8_t)(value >> 8);
    out[n++] = (uint8_t)(value >> 16);
    out[n++] = (uint8_t)(value >> 24);
    return n;
}

size_t proto_encode_text(const char* text, uint8_t* out, size_t out_max) {
    uint8_t data_buf[96];
    uint8_t mesh_buf[160];
    uint8_t radio_buf[256];
    size_t dl = 0, ml = 0, rl = 0;

    if(!text || out_max < 8) return 0;
    size_t text_len = strlen(text);
    if(text_len > PROTO_TEXT_MAX_LEN) text_len = PROTO_TEXT_MAX_LEN;

    // Data { portnum=1, payload=text }
    dl += write_varint_field(data_buf + dl, 1, PORTNUM_TEXT_MESSAGE);
    dl += write_bytes_field(data_buf + dl, 2, (const uint8_t*)text, text_len);

    // MeshPacket { to=0xFFFFFFFF (field 2, fixed32), decoded=Data, hop_limit=3 }
    ml += write_fixed32_field(mesh_buf + ml, 2, MESH_BROADCAST_ADDR);
    ml += write_bytes_field(mesh_buf + ml, 4, data_buf, dl);
    ml += write_varint_field(mesh_buf + ml, 9, 3);  // hop_limit = 3

    // ToRadio { packet=MeshPacket }
    rl += write_bytes_field(radio_buf, 1, mesh_buf, ml);

    if(4 + rl > out_max) return 0;

    // PROTO framing header
    out[0] = 0x94;
    out[1] = 0xC3;
    out[2] = (rl >> 8) & 0xFF;
    out[3] = rl & 0xFF;
    memcpy(out + 4, radio_buf, rl);

    return 4 + rl;
}

// ── FromRadio decoder ─────────────────────────────────────────────────────────
//
// Walks the FromRadio protobuf and extracts TEXT_MESSAGE_APP packets.

static void decode_data(const uint8_t* buf, size_t len,
                        uint8_t* portnum_out,
                        const uint8_t** payload_out, size_t* payload_len_out) {
    size_t pos = 0;
    while(pos < len) {
        uint64_t tag;
        if(!read_varint(buf, len, &pos, &tag)) break;
        uint32_t field = (uint32_t)(tag >> 3);
        uint32_t wire  = (uint32_t)(tag & 0x7);

        if(wire == PB_WIRE_VARINT) {
            uint64_t val;
            if(!read_varint(buf, len, &pos, &val)) break;
            if(field == 1) *portnum_out = (uint8_t)val;
        } else if(wire == PB_WIRE_BYTES) {
            uint64_t blen;
            if(!read_varint(buf, len, &pos, &blen)) break;
            if(pos + blen > len) break;
            if(field == 2) {
                *payload_out     = buf + pos;
                *payload_len_out = (size_t)blen;
            }
            pos += (size_t)blen;
        } else {
            if(!skip_field(buf, len, &pos, wire)) break;
        }
    }
}

static void decode_mesh_packet(const uint8_t* buf, size_t len,
                                uint32_t* from_out,
                                const uint8_t** text_out, size_t* text_len_out) {
    size_t pos = 0;
    while(pos < len) {
        uint64_t tag;
        if(!read_varint(buf, len, &pos, &tag)) break;
        uint32_t field = (uint32_t)(tag >> 3);
        uint32_t wire  = (uint32_t)(tag & 0x7);

        if(wire == 5) {  // fixed32 — from (field 1) and to (field 2) are fixed32
            if(pos + 4 > len) break;
            if(field == 1) {  // MeshPacket.from = field 1, fixed32
                *from_out = (uint32_t)buf[pos]
                          | ((uint32_t)buf[pos+1] << 8)
                          | ((uint32_t)buf[pos+2] << 16)
                          | ((uint32_t)buf[pos+3] << 24);
            }
            pos += 4;
        } else if(wire == PB_WIRE_VARINT) {
            uint64_t val;
            if(!read_varint(buf, len, &pos, &val)) break;
            UNUSED(val);
        } else if(wire == PB_WIRE_BYTES) {
            uint64_t blen;
            if(!read_varint(buf, len, &pos, &blen)) break;
            if(pos + blen > len) break;
            if(field == 4) {  // decoded = Data
                uint8_t portnum = 0;
                const uint8_t* payload = NULL;
                size_t payload_len = 0;
                decode_data(buf + pos, (size_t)blen, &portnum, &payload, &payload_len);
                if(portnum == PORTNUM_TEXT_MESSAGE && payload) {
                    *text_out     = payload;
                    *text_len_out = payload_len;
                }
            }
            pos += (size_t)blen;
        } else {
            if(!skip_field(buf, len, &pos, wire)) break;
        }
    }
}

// ── RX state machine ──────────────────────────────────────────────────────────

typedef enum {
    PROTO_RX_IDLE,
    PROTO_RX_MAGIC2,    // saw 0x94, waiting for 0xC3
    PROTO_RX_LEN_HI,
    PROTO_RX_LEN_LO,
    PROTO_RX_PAYLOAD,
} ProtoRxState;

// want_config_id nonce — any non-zero uint32; we match it in config_complete_id
#define PROTO_NONCE 42u

struct ProtoMode {
    UartHelper*    uart;
    ProtoRxCallback rx_cb;
    void*          rx_ctx;

    // True once config_complete_id is received — only then will we send packets
    bool           connected;

    // Framing state machine
    ProtoRxState   rx_state;
    uint16_t       rx_expected;
    uint16_t       rx_pos;
    uint8_t        rx_buf[PROTO_MAX_PAYLOAD];
};

static void on_rx_byte(uint8_t byte, void* ctx) {
    ProtoMode* p = ctx;

    switch(p->rx_state) {
    case PROTO_RX_IDLE:
        if(byte == 0x94) p->rx_state = PROTO_RX_MAGIC2;
        break;

    case PROTO_RX_MAGIC2:
        p->rx_state = (byte == 0xC3) ? PROTO_RX_LEN_HI : PROTO_RX_IDLE;
        break;

    case PROTO_RX_LEN_HI:
        p->rx_expected = (uint16_t)((uint16_t)byte << 8);
        p->rx_state    = PROTO_RX_LEN_LO;
        break;

    case PROTO_RX_LEN_LO:
        p->rx_expected |= byte;
        p->rx_pos = 0;
        if(p->rx_expected == 0 || p->rx_expected > PROTO_MAX_PAYLOAD) {
            p->rx_state = PROTO_RX_IDLE;
        } else {
            p->rx_state = PROTO_RX_PAYLOAD;
        }
        break;

    case PROTO_RX_PAYLOAD:
        // HIGH-1: bounds-check before write, not after.
        if(p->rx_pos >= PROTO_MAX_PAYLOAD) {
            p->rx_state = PROTO_RX_IDLE;
            break;
        }
        p->rx_buf[p->rx_pos++] = byte;
        if(p->rx_pos < p->rx_expected) break;

        // Full packet received — decode FromRadio
        {
            const uint8_t* buf = p->rx_buf;
            size_t len = p->rx_expected;
            size_t pos = 0;

            while(pos < len) {
                uint64_t tag;
                if(!read_varint(buf, len, &pos, &tag)) break;
                uint32_t field = (uint32_t)(tag >> 3);
                uint32_t wire  = (uint32_t)(tag & 0x7);

                if(wire == PB_WIRE_VARINT) {
                    uint64_t val;
                    if(!read_varint(buf, len, &pos, &val)) break;
                    // FromRadio.config_complete_id = field 7
                    if(field == 7 && (uint32_t)val == PROTO_NONCE)
                        p->connected = true;
                } else if(wire == PB_WIRE_BYTES) {
                    uint64_t blen;
                    if(!read_varint(buf, len, &pos, &blen)) break;
                    // HIGH-3: subtraction form avoids size_t truncation of 64-bit blen.
                    if(blen > (uint64_t)(len - pos)) break;

                    if(field == 2 && p->rx_cb) {  // FromRadio.packet = MeshPacket (field 2)
                        uint32_t from_node = 0;
                        const uint8_t* text = NULL;
                        size_t text_len = 0;
                        decode_mesh_packet(buf + pos, (size_t)blen,
                                           &from_node, &text, &text_len);
                        if(text && text_len > 0) {
                            // LOW-2: mask limits to 4 hex chars; sender[8] is sufficient.
                            char sender[8];
                            snprintf(sender, sizeof(sender), "%04lx",
                                     (unsigned long)(from_node & 0xFFFF));
                            char text_buf[64];
                            size_t copy = text_len < sizeof(text_buf) - 1
                                              ? text_len : sizeof(text_buf) - 1;
                            memcpy(text_buf, text, copy);
                            text_buf[copy] = '\0';
                            p->rx_cb(sender, text_buf, p->rx_ctx);
                        }
                    }
                    // HIGH-3: safe cast — blen <= len - pos guaranteed by guard above.
                    pos += (size_t)blen;
                } else {
                    if(!skip_field(buf, len, &pos, wire)) break;
                }
            }
        }
        p->rx_state = PROTO_RX_IDLE;
        break;
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

// Send ToRadio { want_config_id: PROTO_NONCE } — field 3, uint32.
// Confirmed field number from meshtastic Python library:
//   ToRadio(want_config_id=42).SerializeToString() == b'\x18\x2a'
//   -> tag 0x18 = (3<<3)|0 = field 3, varint
static void send_want_config_id(ProtoMode* p) {
    // Payload: field 3 varint tag (0x18) + varint(PROTO_NONCE)
    uint8_t payload[8];
    size_t pl = 0;
    pl += write_varint(PB_TAG(3, PB_WIRE_VARINT), payload + pl);
    pl += write_varint(PROTO_NONCE, payload + pl);

    uint8_t pkt[12];
    pkt[0] = 0x94;
    pkt[1] = 0xC3;
    pkt[2] = (pl >> 8) & 0xFF;
    pkt[3] = pl & 0xFF;
    memcpy(pkt + 4, payload, pl);
    uart_helper_send_bytes(p->uart, pkt, 4 + pl);
}

ProtoMode* proto_mode_alloc(uint32_t baud, ProtoRxCallback rx_cb, void* context) {
    ProtoMode* p = malloc(sizeof(ProtoMode));
    if(!p) return NULL;
    memset(p, 0, sizeof(ProtoMode));

    p->rx_cb  = rx_cb;
    p->rx_ctx = context;

    p->uart = uart_helper_alloc(baud, on_rx_byte, p);
    if(!p->uart) {
        free(p);
        return NULL;
    }

    // Trigger the config handshake immediately. The node will respond with
    // its full config and finally config_complete_id, after which
    // proto_mode_send_text() will be unblocked.
    send_want_config_id(p);
    return p;
}

void proto_mode_free(ProtoMode* p) {
    if(!p) return;
    uart_helper_free(p->uart);
    free(p);
}

bool proto_mode_is_active(const ProtoMode* p) {
    return p && uart_helper_is_active(p->uart);
}

bool proto_mode_is_connected(const ProtoMode* p) {
    return p && p->connected;
}

size_t proto_mode_send_text(ProtoMode* p, const char* text) {
    if(!p || !text || !p->connected) return 0;
    uint8_t buf[300];
    size_t len = proto_encode_text(text, buf, sizeof(buf));
    if(len == 0) return 0;
    uart_helper_send_bytes(p->uart, buf, len);
    return len;
}
