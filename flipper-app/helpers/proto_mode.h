#pragma once

#include <furi.h>
#include <stdint.h>
#include <stdbool.h>

// Meshtastic PROTO serial framing: 0x94 0xC3 [len_hi len_lo] [protobuf payload]
// The payload is a serialized ToRadio (host->node) or FromRadio (node->host) message.

#define GHOSTMESH_UART_BAUD  115200
#define PROTO_MAX_PAYLOAD    512

typedef struct ProtoMode ProtoMode;

// Called when a complete text message packet is received from the mesh.
//   sender: last 4 hex digits of the source node ID (e.g. "f69c")
//   text:   null-terminated message text
//   rssi:   received signal strength in dBm (0 = not reported, e.g. local phone packet)
//   snr:    signal-to-noise ratio in dB (0.0 = not reported)
//
// HIGH-2 API CONTRACT: sender and text point into stack buffers inside the UART
// callback. They are only valid for the duration of this call. The implementation
// MUST copy any data it wants to retain before returning — never store these pointers.
typedef void (*ProtoRxCallback)(const char* sender, const char* text,
                                int16_t rssi, float snr, void* context);

// ── Telemetry (TELEMETRY_APP=67) and Position (POSITION_APP=3) ────────────────
// Optional decoded packet types. has_device/has_env say which Telemetry
// sub-message was present. Field numbers confirmed via meshtastic Python lib
// serialization (see proto_notes.md).
typedef struct {
    uint32_t from;           // source node ID
    bool     has_device;     // device_metrics present
    uint8_t  battery_level;  // 0-100, or 101 = powered/external (no battery)
    float    voltage;        // pack voltage, V
    bool     has_env;        // environment_metrics present
    float    temperature;    // degC
    float    humidity;       // %RH
    float    pressure;       // hPa
} ProtoTelemetry;

typedef struct {
    uint32_t from;
    int32_t  latitude_i;     // degrees * 1e7
    int32_t  longitude_i;    // degrees * 1e7
    int32_t  altitude;       // meters
} ProtoPosition;

// Same ISR-context contract as ProtoRxCallback: the pointer is valid only for
// the duration of the call — copy anything you keep before returning.
typedef void (*ProtoTelemetryCallback)(const ProtoTelemetry* t, void* context);
typedef void (*ProtoPositionCallback)(const ProtoPosition* p, void* context);

// Allocate and open UART in PROTO mode.
// Returns NULL if UART cannot be acquired.
ProtoMode* proto_mode_alloc(uint32_t baud, ProtoRxCallback rx_cb, void* context);
void proto_mode_free(ProtoMode* proto);

// Optional: register callbacks for telemetry / position packets (default: none).
void proto_mode_set_telemetry_callback(ProtoMode* proto, ProtoTelemetryCallback cb, void* context);
void proto_mode_set_position_callback(ProtoMode* proto, ProtoPositionCallback cb, void* context);

bool proto_mode_is_active(const ProtoMode* proto);

// Returns true once the want_config_id / config_complete_id handshake has
// completed. Sends are blocked until this returns true.
bool proto_mode_is_connected(const ProtoMode* proto);

// Re-send the want_config handshake request. The first request is sent
// automatically at alloc; call this periodically while proto_mode_is_connected()
// is still false so a missed or dropped request self-heals — the node isn't
// always listening the instant the FAP launches. This is what the phone app does.
void proto_mode_request_config(ProtoMode* proto);

// The local node's own ID (MyNodeInfo.my_node_num), learned during the config
// handshake. Returns 0 until known. Use it to filter telemetry/position to the
// locally-attached node and ignore other mesh nodes' metrics.
uint32_t proto_mode_get_local_node(const ProtoMode* proto);

// Encode and send a text message as a ToRadio PROTO packet (broadcast to all nodes).
// Returns bytes transmitted (header + payload), or 0 on error.
size_t proto_mode_send_text(ProtoMode* proto, const char* text);

// ── Config backup ─────────────────────────────────────────────────────────────
// During the want_config handshake the node streams its whole configuration. We capture the
// Config (FromRadio field 5), ModuleConfig (9), and Channel (10) sub-messages — everything needed
// to restore the device, including the channel PSKs — into a buffer, as a sequence of records:
//   [type:1][len_lo:1][len_hi:1][protobuf bytes:len]   (type = 5 / 9 / 10)
// The nodedb and other frames are intentionally skipped. Returns false until config_complete.
bool proto_mode_get_config_backup(const ProtoMode* proto, const uint8_t** out, uint16_t* len);
