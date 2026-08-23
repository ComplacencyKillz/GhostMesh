# Meshtastic Serial Protocol — GhostMesh Implementation

## What GhostMesh Uses

GhostMesh communicates with the Heltec node using Meshtastic's **PROTO serial protocol**, served by the Meshtastic **Serial module in PROTO mode** on **GPIO7 (RX) / GPIO6 (TX)**. PROTO mode exposes the same StreamAPI protobuf stream the official Meshtastic Python library and phone app use.

This **requires** Serial-module config (*Module Config → Serial*: enabled, mode PROTO, RX 7, TX 6, 115200, override-console OFF) — see [meshtastic-setup.md](meshtastic-setup.md).

> **Not UART0 / GPIO43-44.** Earlier builds used the PhoneAPI on UART0, but the CP2102 USB bridge shares those pins and clamps them when the Heltec runs on battery, so that link only worked on USB power. The Serial module on free pins 6/7 works on pure battery. See [wiring.md](wiring.md) for the full CP2102 story.

---

## Protocol Overview

### Framing

All packets use Meshtastic's binary framing:

~~~
[0x94] [0xC3] [len_hi] [len_lo] [protobuf payload]
~~~

- `0x94 0xC3` — magic start bytes
- `len_hi len_lo` — 16-bit big-endian payload length
- payload — a serialized `ToRadio` (host→node) or `FromRadio` (node→host) protobuf message

### Connection Handshake

On startup the FAP sends a `ToRadio { want_config_id: 42 }` packet. The node responds with ~47 `FromRadio` configuration frames (node info, channels, config, module config, known nodes, file manifest) followed by `FromRadio { config_complete_id: 42 }`. Once that is received the connection is ready and text messages can be sent.

The Flipper title bar shows `...` during the handshake (the request re-sends every ~2 s until answered), `RDY` once connected, then the node's battery `%` (or `PWR` on external power) once the level is read from the config exchange.

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
| `my_info` (MyNodeInfo) | 3 | bytes | `my_node_num` (field 1) = local node ID, for filtering |
| `node_info` (NodeInfo) | 4 | bytes | local node's `device_metrics` (field 6) → battery `%` on connect |
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

## The Meshtastic App Serial Module Setting

The Meshtastic app's **Module Config → Serial** settings put a *SerialModule* UART on the GPIO pins you specify. **GhostMesh depends on this** — it must be set to PROTO mode on GPIO7 (RX) / GPIO6 (TX) at 115200 with *override console* off. Without it, the Flipper link is dead.

(This reverses earlier docs that said the SerialModule "has no effect on GhostMesh." That held only while GhostMesh used the UART0 PhoneAPI — which was abandoned because the CP2102 clamps GPIO43/44 on battery.)

---

## Why Not TEXTMSG Mode?

TEXTMSG serial mode was evaluated as an MVP option and briefly implemented. It was replaced by PROTO for two reasons:

1. **RF noise** — TEXTMSG broadcasts any bytes that arrive on the serial RX pin. Electromagnetic interference from the nearby SX1262 LoRa antenna occasionally induced false UART frames, which Meshtastic broadcast as garbage mesh messages. PROTO's `0x94 0xC3` framing means random noise almost never produces a valid packet.

2. **No metadata** — TEXTMSG provides only raw text. PROTO delivers sender node ID, RSSI, SNR, hop count, and access to the full Meshtastic packet graph.
