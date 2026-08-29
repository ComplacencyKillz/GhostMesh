---
---
# PROTO Mode — Implementation Notes

## What Is Implemented

GhostMesh uses a hand-coded minimal protobuf encoder/decoder in <code>proto_mode.c/.h</code>. No external libraries (no nanopb). All field numbers were confirmed by serializing known messages with the official meshtastic Python library (v2.7.8) and reading the wire encoding.

---

## Protocol Path

<pre><code>
Flipper USART1 TX (pin 13)     →  Heltec GPIO7 (Serial module RX)  →  Meshtastic StreamAPI
Heltec GPIO6 (Serial module TX) →  Flipper USART1 RX (pin 14)      →  proto_mode decoder
</code></pre>

GhostMesh talks to the Meshtastic **Serial module in PROTO mode** on free GPIO pins (**RX = 7, TX = 6**, 115200 baud). PROTO mode exposes the same StreamAPI protobuf stream — <code>ToRadio</code>/<code>FromRadio</code>, <code>want_config</code>/<code>config_complete</code> — used by the phone app and Python library. It **requires** Meshtastic config: *Module Config → Serial* → enabled, mode **PROTO**, RX **7**, TX **6**, baud **115200**, *override console serial port* **OFF**.

> **Why not UART0 / GPIO43-44 (the original path)?** Earlier builds connected to the PhoneAPI on UART0 (GPIO43/44). That path is abandoned: on the Heltec V3 the **CP2102 USB-UART bridge shares GPIO43/44**, and when the Heltec runs on battery (USB unplugged) the *unpowered* CP2102 clamps those lines — so the Flipper can't drive them and the link only worked while the Heltec was USB-powered (useless for field deployment). Free pins 6/7 have no CP2102 on them, so PROTO works on pure battery. See the closing section for the full story.

---

## Handshake Flow

<pre><code>
FAP startup → send ToRadio { want_config_id: 42 }
Node sends  → ~47 FromRadio config frames
Node sends  → FromRadio { config_complete_id: 42 }  ← FAP sets connected=true
FAP ready   → ToRadio { packet: MeshPacket { ... } } can now be sent
</code></pre>

The Flipper title bar shows <code>...</code> during handshake, then <code>RDY</code> when connected. It switches to the local node's battery <code>%</code> (or <code>PWR</code> when the node reports <code>battery_level == 101</code>, i.e. on external power) as soon as the battery level is known — see the NodeInfo note under Telemetry.

The <code>want_config</code> request is re-sent every ~2 s until <code>config_complete</code> arrives (<code>proto_mode_request_config</code> from the main loop), so a request the node misses at startup self-heals instead of hanging on <code>...</code>.

---

## Confirmed Field Numbers (Meshtastic 2.7.x)

All field numbers confirmed from meshtastic Python library 2.7.8 via:
<pre><code>
mp = mesh_pb2.MeshPacket()
mp.to = 0xFFFFFFFF
print(mp.SerializeToString().hex())   # → 15 ff ff ff ff (field 2, fixed32)
</code></pre>

### ToRadio
| Field | Number | Wire |
|-------|--------|------|
| <code>want_config_id</code> | 3 | varint |
| <code>packet</code> (MeshPacket) | 1 | bytes |

### FromRadio
| Field | Number | Wire |
|-------|--------|------|
| <code>packet</code> (MeshPacket) | **2** | bytes |
| <code>my_info</code> (MyNodeInfo) | **3** | bytes — <code>MyNodeInfo.my_node_num</code> = field 1, varint (local node ID) |
| <code>node_info</code> (NodeInfo) | **4** | bytes — see NodeInfo below (decoded for local battery on connect) |
| <code>config_complete_id</code> | **7** | varint |
| <code>rebooted</code> | 8 | varint (bool) |

### NodeInfo (FromRadio field 4)
| Field | Number | Wire | Notes |
|-------|--------|------|-------|
| <code>num</code> | 1 | varint | node ID — matched against <code>my_node_num</code> to isolate the local node |
| <code>device_metrics</code> (DeviceMetrics) | **6** | bytes | same <code>DeviceMetrics</code> as Telemetry; <code>battery_level</code> = field 1 |

> The node sends its own <code>NodeInfo</code> during the <code>want_config</code> handshake, so <code>decode_node_info</code> pulls the local <code>battery_level</code> out of it and shows the <code>%</code> the instant we connect — otherwise the title would sit on <code>RDY</code> until the next live telemetry broadcast (which can be 30 min out on a long device-metrics interval).

### MeshPacket
| Field | Number | Wire | Notes |
|-------|--------|------|-------|
| <code>from</code> | **1** | **fixed32** | source node ID |
| <code>to</code> | **2** | **fixed32** | destination (0xFFFFFFFF = broadcast) |
| <code>decoded</code> (Data) | 4 | bytes | |
| <code>rx_snr</code> | 8 | fixed32 (float) | SNR in dB; 0.0 if not a radio packet |
| <code>hop_limit</code> | **9** | varint | |
| <code>rx_rssi</code> | 12 | varint (int32) | RSSI in dBm; 0 if not a radio packet |

### Data
| Field | Number | Wire |
|-------|--------|------|
| <code>portnum</code> | 1 | varint (<code>TEXT_MESSAGE_APP = 1</code>) |
| <code>payload</code> | 2 | bytes |

> **Note:** <code>to</code> and <code>from</code> use <code>fixed32</code> wire type (type 7 in the descriptor, wire type 5), NOT varint. Using the wrong wire type causes the firmware to silently drop the packet.

### Telemetry (<code>TELEMETRY_APP</code> = 67)

<code>Data.payload</code> for portnum 67 is a serialized <code>Telemetry</code>. Confirmed via meshtastic Python lib serialization.

| Message | Field | Number | Wire |
|---------|-------|--------|------|
| <code>Telemetry</code> | <code>device_metrics</code> | 2 | bytes |
| <code>Telemetry</code> | <code>environment_metrics</code> | 3 | bytes |
| <code>DeviceMetrics</code> | <code>battery_level</code> | 1 | varint (0–100; **101 = powered / no battery**) |
| <code>DeviceMetrics</code> | <code>voltage</code> | 2 | fixed32 (float) |
| <code>EnvironmentMetrics</code> | <code>temperature</code> | 1 | fixed32 (float, °C) |
| <code>EnvironmentMetrics</code> | <code>relative_humidity</code> | 2 | fixed32 (float, %RH) |
| <code>EnvironmentMetrics</code> | <code>barometric_pressure</code> | 3 | fixed32 (float, hPa) |

### Position (<code>POSITION_APP</code> = 3)

<code>Data.payload</code> for portnum 3 is a serialized <code>Position</code>.

| Field | Number | Wire | Notes |
|-------|--------|------|-------|
| <code>latitude_i</code> | 1 | sfixed32 | degrees × 1e7 |
| <code>longitude_i</code> | 2 | sfixed32 | degrees × 1e7 |
| <code>altitude</code> | 3 | varint (int32) | meters |

---

## UART Callback Context

<code>furi_hal_serial_async_rx_start</code> fires its callback from the UART ISR, not from a worker thread. This means the RX callback chain (<code>uart_internal_rx_cb</code> → <code>on_rx_byte</code> → <code>on_rx_text</code>) runs in interrupt context. Consequences:

- **Do not call <code>furi_mutex_acquire</code> from <code>on_rx_text</code>.** FuriMutex is backed by FreeRTOS mutexes, which cannot be taken from ISR context. Attempting this causes every receive to silently fail.
- <code>rx_updated</code> in <code>GhostMeshApp</code> is declared <code>volatile bool</code> so the main loop sees writes from the ISR without the compiler caching the value in a register. This is the correct ISR-safe signaling primitive for a single-producer/single-consumer flag on Cortex-M4.
- <code>rx_sender</code>, <code>rx_text_buf</code>, <code>rx_rssi</code>, and <code>rx_snr</code> are written from the ISR and read from the main loop. A torn read on these fields is theoretically possible but harmless — the display just shows a stale frame; the next message overwrites everything.

---

## Known Limitations

- <code>TEXT_MESSAGE_APP</code>, <code>TELEMETRY_APP</code>, and <code>POSITION_APP</code> are decoded and delivered via callbacks (<code>proto_mode_set_telemetry_callback</code> / <code>proto_mode_set_position_callback</code>). The app must register those callbacks and surface the data in the UI / CSV. Admin and other portnums are still skipped.
- Sender display uses the last 4 hex digits of the node ID (<code>from & 0xFFFF</code>), e.g. <code>f69c: Hello</code>. <code>NodeInfo</code> frames are now parsed (<code>decode_node_info</code>) but only the local node's <code>device_metrics.battery_level</code> is extracted — the <code>User.long_name</code> is still not stored, so long sender names are not shown.
- The config exchange (~47 frames, ~1 KB) is received and processed by the UART callback state machine. Most frames are decoded and discarded; <code>my_info</code> (local node ID), the local <code>node_info</code> (battery), and <code>config_complete_id</code> are the ones that matter.

---

## Why the Serial Module PROTO Mode (and why UART0 was abandoned)

GhostMesh now uses the Meshtastic **Serial module in PROTO mode** on GPIO7 (RX) / GPIO6 (TX). An earlier note here claimed the Serial module PROTO mode "does not work reliably" and that the PhoneAPI on UART0 (GPIO43/44) was the correct path. **That conclusion was a misdiagnosis** — the real culprit was the pins, not the module:

- On the Heltec V3 the **CP2102 USB-UART bridge is wired to UART0 (GPIO43/44)** — the same pads the Flipper was connected to.
- The CP2102 is powered from USB. With the Heltec on **battery** (USB unplugged), the unpowered CP2102 **clamps GPIO43/44 to ground**, so nothing external can drive a valid signal into the ESP32. The PhoneAPI link only ever worked while the Heltec was plugged into USB power — and the Serial module PROTO experiments that "failed" had *also* been configured on 43/44, so they hit the exact same clamp.
- **Confirmed 2026-07-01:** plugging the Heltec into any USB power (even a dumb charger) made the old 43/44 link connect instantly; on battery it never did. Moving the Serial module to **free pins (6/7), which have no CP2102 on them**, makes PROTO connect on pure battery — the deployable configuration.

The <code>want_config</code> / <code>config_complete</code> handshake and everything else in this document is identical either way; only the Heltec-side pins and the required Serial-module config changed. The Flipper side is unchanged (USART1, pins 13/14).
