# Meshtastic Serial Protocol — GhostMesh Implementation

## What GhostMesh Uses

GhostMesh communicates with the Heltec node using Meshtastic's **PROTO serial protocol** via the **PhoneAPI on UART0 (GPIO43/44)**. This is the same interface used by the official Meshtastic Python library and the Meshtastic phone app over USB.

No special Meshtastic serial module configuration is required. The PhoneAPI is permanently available on UART0 regardless of the serial module settings in the Meshtastic app.

---

## Protocol Overview

### Framing

All packets use Meshtastic's binary framing:

```
[0x94] [0xC3] [len_hi] [len_lo] [protobuf payload]
```

- `0x94 0xC3` — magic start bytes
- `len_hi len_lo` — 16-bit big-endian payload length
- payload — a serialized `ToRadio` (host→node) or `FromRadio` (node→host) protobuf message

### Connection Handshake

On startup the FAP sends a `ToRadio { want_config_id: 42 }` packet. The node responds with ~47 `FromRadio` configuration frames (node info, channels, config, module config, known nodes, file manifest) followed by `FromRadio { config_complete_id: 42 }`. Once that is received the connection is ready and text messages can be sent.

The Flipper title bar shows `...` during the handshake and `RDY` once connected.

---

## Confirmed Field Numbers

Determined from the meshtastic Python library (v2.7.8) by serializing known messages and reading the wire encoding.

### ToRadio

| Field | Number | Wire type | Notes |
|-------|--------|-----------|-------|
| `want_config_id` | 3 | varint | sent on startup to trigger config exchange |
| `packet` (MeshPacket) | 1 | bytes | used to send text messages |

### FromRadio

| Field | Number | Wire type | Notes |
|-------|--------|-----------|-------|
| `packet` (MeshPacket) | 2 | bytes | incoming mesh message |
| `config_complete_id` | 7 | varint | signals handshake complete |
| `rebooted` | 8 | varint (bool) | node just rebooted |

### MeshPacket (send)

| Field | Number | Wire type | Notes |
|-------|--------|-----------|-------|
| `from` | 1 | fixed32 | filled by node, not sent by host |
| `to` | 2 | fixed32 | `0xFFFFFFFF` = broadcast |
| `decoded` (Data) | 4 | bytes | contains portnum + payload |
| `hop_limit` | 9 | varint | set to 3 |

### MeshPacket (receive — decoded by GhostMesh)

| Field | Number | Wire type | Notes |
|-------|--------|-----------|-------|
| `from` | 1 | fixed32 | source node ID; last 4 hex digits shown as sender |
| `decoded` (Data) | 4 | bytes | text payload |
| `rx_snr` | 8 | fixed32 (float) | SNR in dB; 0.0 if not a radio packet |
| `rx_rssi` | 12 | varint (int32) | RSSI in dBm; 0 if not a radio packet |

### Data

| Field | Number | Wire type | Notes |
|-------|--------|-----------|-------|
| `portnum` | 1 | varint | `1` = `TEXT_MESSAGE_APP` |
| `payload` | 2 | bytes | UTF-8 message text |

---

## What the Meshtastic App Serial Module Setting Does

The Meshtastic app's **Module Config → Serial** settings configure a separate *SerialModule* UART on the GPIO pins you specify. GhostMesh does **not** use the SerialModule. GhostMesh connects directly to UART0 (GPIO43/44), bypassing the SerialModule entirely.

You can leave the serial module at its default settings or disable it — it has no effect on GhostMesh operation.

---

## Why Not TEXTMSG Mode?

TEXTMSG serial mode was evaluated as an MVP option and briefly implemented. It was replaced by PROTO for two reasons:

1. **RF noise** — TEXTMSG broadcasts any bytes that arrive on the serial RX pin. Electromagnetic interference from the nearby SX1262 LoRa antenna occasionally induced false UART frames, which Meshtastic broadcast as garbage mesh messages. PROTO's `0x94 0xC3` framing means random noise almost never produces a valid packet.

2. **No metadata** — TEXTMSG provides only raw text. PROTO delivers sender node ID, RSSI, SNR, hop count, and access to the full Meshtastic packet graph.
