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
        if(*pos + tmp > len) return false;
        *pos += (size_t)tmp;
        return true;
    }
    case 5: if(*pos + 4 > len) return false; *pos += 4; return true;
    default: return false;
    }
}

// ── ToRadio encoder ───────────────────────────────────────────────────────────
//
// Encodes:
//   ToRadio { packet: MeshPacket { to: 0xFFFFFFFF, decoded: Data {
//     portnum: TEXT_MESSAGE_APP, payload: <text>
//   }}}

size_t proto_encode_text(const char* text, uint8_t* out, size_t out_max) {
    uint8_t data_buf[96];
    uint8_t mesh_buf[160];
    uint8_t radio_buf[256];
    size_t dl = 0, ml = 0, rl = 0;

    if(!text || out_max < 8) return 0;
    size_t text_len = strlen(text);
    if(text_len > 80) text_len = 80;  // mesh text limit

    // Data { portnum=1, payload=text }
    dl += write_varint_field(data_buf + dl, 1, PORTNUM_TEXT_MESSAGE);
    dl += write_bytes_field(data_buf + dl, 2, (const uint8_t*)text, text_len);

    // MeshPacket { to=0xFFFFFFFF, decoded=Data }
    ml += write_varint_field(mesh_buf + ml, 3, MESH_BROADCAST_ADDR);
    ml += write_bytes_field(mesh_buf + ml, 4, data_buf, dl);

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

        if(wire == PB_WIRE_VARINT) {
            uint64_t val;
            if(!read_varint(buf, len, &pos, &val)) break;
            if(field == 2) *from_out = (uint32_t)val;  // from
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

struct ProtoMode {
    UartHelper*    uart;
    ProtoRxCallback rx_cb;
    void*          rx_ctx;

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

                if(wire == PB_WIRE_BYTES) {
                    uint64_t blen;
                    if(!read_varint(buf, len, &pos, &blen)) break;
                    if(pos + blen > len) break;

                    if(field == 3 && p->rx_cb) {  // FromRadio.packet = MeshPacket
                        uint32_t from_node = 0;
                        const uint8_t* text = NULL;
                        size_t text_len = 0;
                        decode_mesh_packet(buf + pos, (size_t)blen,
                                           &from_node, &text, &text_len);
                        if(text && text_len > 0) {
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

size_t proto_mode_send_text(ProtoMode* p, const char* text) {
    if(!p || !text) return 0;
    uint8_t buf[300];
    size_t len = proto_encode_text(text, buf, sizeof(buf));
    if(len == 0) return 0;
    uart_helper_send_bytes(p->uart, buf, len);
    return len;
}
