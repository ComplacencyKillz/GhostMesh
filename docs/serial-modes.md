---
---
# Meshtastic Serial Protocol — GhostMesh Implementation

## What GhostMesh Uses

GhostMesh communicates with the Heltec node using Meshtastic's **PROTO serial protocol**, served by the Meshtastic **Serial module in PROTO mode** on **GPIO7 (RX) / GPIO6 (TX)**. PROTO mode exposes the same StreamAPI protobuf stream the official Meshtastic Python library and phone app use.

This **requires** Serial-module config (*Module Config → Serial*: enabled, mode PROTO, RX 7, TX 6, 115200, override-console OFF) — see [meshtastic-setup.md](meshtastic-setup.md).

> **Not UART0 / GPIO43-44.** Earlier builds used the PhoneAPI on UART0, but the CP2102 USB bridge shares those pins and clamps them when the Heltec runs on battery, so that link only worked on USB power. The Serial module on free pins 6/7 works on pure battery. See [wiring.md](wiring.md) for the full CP2102 story.

---

## Protocol Overview

### Framing

All packets use Meshtastic's binary framing:

<pre><code>
[0x94] [0xC3] [len_hi] [len_lo] [protobuf payload]
</code></pre>

- <code>0x94 0xC3</code> — magic start bytes
- <code>len_hi len_lo</code> — 16-bit big-endian payload length
- payload — a serialized <code>ToRadio</code> (host→node) or <code>FromRadio</code> (node→host) protobuf message

### Connection Handshake

On startup the FAP sends a <code>ToRadio { want_config_id: 42 }</code> packet. The node responds with ~47 <code>FromRadio</code> configuration frames (node info, channels, config, module config, known nodes, file manifest) followed by <code>FromRadio { config_complete_id: 42 }</code>. Once that is received the connection is ready and text messages can be sent.

The Flipper title bar shows <code>...</code> during the handshake (the request re-sends every ~2 s until answered), <code>RDY</code> once connected, then the node's battery <code>%</code> (or <code>PWR</code> on external power) once the level is read from the config exchange.

---

## Confirmed Field Numbers

Determined from the meshtastic Python library (v2.7.8) by serializing known messages and reading the wire encoding.

### ToRadio

| Field | Number | Wire type | Notes |
|-------|--------|-----------|-------|
| <code>want_config_id</code> | 3 | varint | sent on startup to trigger config exchange |
| <code>packet</code> (MeshPacket) | 1 | bytes | used to send text messages |

### FromRadio

| Field | Number | Wire type | Notes |
|-------|--------|-----------|-------|
| <code>packet</code> (MeshPacket) | 2 | bytes | incoming mesh message |
| <code>my_info</code> (MyNodeInfo) | 3 | bytes | <code>my_node_num</code> (field 1) = local node ID, for filtering |
| <code>node_info</code> (NodeInfo) | 4 | bytes | local node's <code>device_metrics</code> (field 6) → battery <code>%</code> on connect |
| <code>config_complete_id</code> | 7 | varint | signals handshake complete |
| <code>rebooted</code> | 8 | varint (bool) | node just rebooted |

### MeshPacket (send)

| Field | Number | Wire type | Notes |
|-------|--------|-----------|-------|
| <code>from</code> | 1 | fixed32 | filled by node, not sent by host |
| <code>to</code> | 2 | fixed32 | <code>0xFFFFFFFF</code> = broadcast |
| <code>decoded</code> (Data) | 4 | bytes | contains portnum + payload |
| <code>hop_limit</code> | 9 | varint | set to 3 |

### MeshPacket (receive — decoded by GhostMesh)

| Field | Number | Wire type | Notes |
|-------|--------|-----------|-------|
| <code>from</code> | 1 | fixed32 | source node ID; last 4 hex digits shown as sender |
| <code>decoded</code> (Data) | 4 | bytes | text payload |
| <code>rx_snr</code> | 8 | fixed32 (float) | SNR in dB; 0.0 if not a radio packet |
| <code>rx_rssi</code> | 12 | varint (int32) | RSSI in dBm; 0 if not a radio packet |

### Data

| Field | Number | Wire type | Notes |
|-------|--------|-----------|-------|
| <code>portnum</code> | 1 | varint | <code>1</code> = <code>TEXT_MESSAGE_APP</code> |
| <code>payload</code> | 2 | bytes | UTF-8 message text |

---

## The Meshtastic App Serial Module Setting

The Meshtastic app's **Module Config → Serial** settings put a *SerialModule* UART on the GPIO pins you specify. **GhostMesh depends on this** — it must be set to PROTO mode on GPIO7 (RX) / GPIO6 (TX) at 115200 with *override console* off. Without it, the Flipper link is dead.

(This reverses earlier docs that said the SerialModule "has no effect on GhostMesh." That held only while GhostMesh used the UART0 PhoneAPI — which was abandoned because the CP2102 clamps GPIO43/44 on battery.)

---

## Why Not TEXTMSG Mode?

TEXTMSG serial mode was evaluated as an MVP option and briefly implemented. It was replaced by PROTO for two reasons:

1. **RF noise** — TEXTMSG broadcasts any bytes that arrive on the serial RX pin. Electromagnetic interference from the nearby SX1262 LoRa antenna occasionally induced false UART frames, which Meshtastic broadcast as garbage mesh messages. PROTO's <code>0x94 0xC3</code> framing means random noise almost never produces a valid packet.

2. **No metadata** — TEXTMSG provides only raw text. PROTO delivers sender node ID, RSSI, SNR, hop count, and access to the full Meshtastic packet graph.
