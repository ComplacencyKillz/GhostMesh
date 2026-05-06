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
// sender: last 4 hex digits of the source node ID (e.g. "f69c")
// text:   null-terminated message text
typedef void (*ProtoRxCallback)(const char* sender, const char* text, void* context);

// Allocate and open UART in PROTO mode.
// Returns NULL if UART cannot be acquired.
ProtoMode* proto_mode_alloc(uint32_t baud, ProtoRxCallback rx_cb, void* context);
void proto_mode_free(ProtoMode* proto);

bool proto_mode_is_active(const ProtoMode* proto);

// Returns true once the want_config_id / config_complete_id handshake has
// completed. Sends are blocked until this returns true.
bool proto_mode_is_connected(const ProtoMode* proto);

// Encode and send a text message as a ToRadio PROTO packet (broadcast to all nodes).
// Returns bytes transmitted (header + payload), or 0 on error.
size_t proto_mode_send_text(ProtoMode* proto, const char* text);
