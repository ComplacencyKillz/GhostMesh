# PROTO Mode — Implementation Notes

## What Is Implemented

GhostMesh uses a hand-coded minimal protobuf encoder/decoder in `proto_mode.c/.h`. No external libraries (no nanopb). All field numbers were confirmed by serializing known messages with the official meshtastic Python library (v2.7.8) and reading the wire encoding.

---

## Protocol Path

```
Flipper USART1 TX (pin 13)     →  Heltec GPIO7 (Serial module RX)  →  Meshtastic StreamAPI
Heltec GPIO6 (Serial module TX) →  Flipper USART1 RX (pin 14)      →  proto_mode decoder
```

GhostMesh talks to the Meshtastic **Serial module in PROTO mode** on free GPIO pins (**RX = 7, TX = 6**, 115200 baud). PROTO mode exposes the same StreamAPI protobuf stream — `ToRadio`/`FromRadio`, `want_config`/`config_complete` — used by the phone app and Python library. It **requires** Meshtastic config: *Module Config → Serial* → enabled, mode **PROTO**, RX **7**, TX **6**, baud **115200**, *override console serial port* **OFF**.

> **Why not UART0 / GPIO43-44 (the original path)?** Earlier builds connected to the PhoneAPI on UART0 (GPIO43/44). That path is abandoned: on the Heltec V3 the **CP2102 USB-UART bridge shares GPIO43/44**, and when the Heltec runs on battery (USB unplugged) the *unpowered* CP2102 clamps those lines — so the Flipper can't drive them and the link only worked while the Heltec was USB-powered (useless for field deployment). Free pins 6/7 have no CP2102 on them, so PROTO works on pure battery. See the closing section for the full story.

---

## Handshake Flow

```
FAP startup → send ToRadio { want_config_id: 42 }
Node sends  → ~47 FromRadio config frames
Node sends  → FromRadio { config_complete_id: 42 }  ← FAP sets connected=true
FAP ready   → ToRadio { packet: MeshPacket { ... } } can now be sent
```

The Flipper title bar shows `...` during handshake, then `RDY` when connected. It switches to the local node's battery `%` (or `PWR` when the node reports `battery_level == 101`, i.e. on external power) as soon as the battery level is known — see the NodeInfo note under Telemetry.

The `want_config` request is re-sent every ~2 s until `config_complete` arrives (`proto_mode_request_config` from the main loop), so a request the node misses at startup self-heals instead of hanging on `...`.

---

## Confirmed Field Numbers (Meshtastic 2.7.x)

All field numbers confirmed from meshtastic Python library 2.7.8 via:
```python
mp = mesh_pb2.MeshPacket()
mp.to = 0xFFFFFFFF
print(mp.SerializeToString().hex())   # → 15 ff ff ff ff (field 2, fixed32)
```

### ToRadio
| Field | Number | Wire |
|-------|--------|------|
| `want_config_id` | 3 | varint |
| `packet` (MeshPacket) | 1 | bytes |

### FromRadio
| Field | Number | Wire |
|-------|--------|------|
| `packet` (MeshPacket) | **2** | bytes |
| `my_info` (MyNodeInfo) | **3** | bytes — `MyNodeInfo.my_node_num` = field 1, varint (local node ID) |
| `node_info` (NodeInfo) | **4** | bytes — see NodeInfo below (decoded for local battery on connect) |
| `config_complete_id` | **7** | varint |
| `rebooted` | 8 | varint (bool) |

### NodeInfo (FromRadio field 4)
| Field | Number | Wire | Notes |
|-------|--------|------|-------|
| `num` | 1 | varint | node ID — matched against `my_node_num` to isolate the local node |
| `device_metrics` (DeviceMetrics) | **6** | bytes | same `DeviceMetrics` as Telemetry; `battery_level` = field 1 |

> The node sends its own `NodeInfo` during the `want_config` handshake, so `decode_node_info` pulls the local `battery_level` out of it and shows the `%` the instant we connect — otherwise the title would sit on `RDY` until the next live telemetry broadcast (which can be 30 min out on a long device-metrics interval).

### MeshPacket
| Field | Number | Wire | Notes |
|-------|--------|------|-------|
| `from` | **1** | **fixed32** | source node ID |
| `to` | **2** | **fixed32** | destination (0xFFFFFFFF = broadcast) |
| `decoded` (Data) | 4 | bytes | |
| `rx_snr` | 8 | fixed32 (float) | SNR in dB; 0.0 if not a radio packet |
| `hop_limit` | **9** | varint | |
| `rx_rssi` | 12 | varint (int32) | RSSI in dBm; 0 if not a radio packet |

### Data
| Field | Number | Wire |
|-------|--------|------|
| `portnum` | 1 | varint (`TEXT_MESSAGE_APP = 1`) |
| `payload` | 2 | bytes |

> **Note:** `to` and `from` use `fixed32` wire type (type 7 in the descriptor, wire type 5), NOT varint. Using the wrong wire type causes the firmware to silently drop the packet.

### Telemetry (`TELEMETRY_APP` = 67)

`Data.payload` for portnum 67 is a serialized `Telemetry`. Confirmed via meshtastic Python lib serialization.

| Message | Field | Number | Wire |
|---------|-------|--------|------|
| `Telemetry` | `device_metrics` | 2 | bytes |
| `Telemetry` | `environment_metrics` | 3 | bytes |
| `DeviceMetrics` | `battery_level` | 1 | varint (0–100; **101 = powered / no battery**) |
| `DeviceMetrics` | `voltage` | 2 | fixed32 (float) |
| `EnvironmentMetrics` | `temperature` | 1 | fixed32 (float, °C) |
| `EnvironmentMetrics` | `relative_humidity` | 2 | fixed32 (float, %RH) |
| `EnvironmentMetrics` | `barometric_pressure` | 3 | fixed32 (float, hPa) |

### Position (`POSITION_APP` = 3)

`Data.payload` for portnum 3 is a serialized `Position`.

| Field | Number | Wire | Notes |
|-------|--------|------|-------|
| `latitude_i` | 1 | sfixed32 | degrees × 1e7 |
| `longitude_i` | 2 | sfixed32 | degrees × 1e7 |
| `altitude` | 3 | varint (int32) | meters |

---

## UART Callback Context

`furi_hal_serial_async_rx_start` fires its callback from the UART ISR, not from a worker thread. This means the RX callback chain (`uart_internal_rx_cb` → `on_rx_byte` → `on_rx_text`) runs in interrupt context. Consequences:

- **Do not call `furi_mutex_acquire` from `on_rx_text`.** FuriMutex is backed by FreeRTOS mutexes, which cannot be taken from ISR context. Attempting this causes every receive to silently fail.
- `rx_updated` in `GhostMeshApp` is declared `volatile bool` so the main loop sees writes from the ISR without the compiler caching the value in a register. This is the correct ISR-safe signaling primitive for a single-producer/single-consumer flag on Cortex-M4.
- `rx_sender`, `rx_text_buf`, `rx_rssi`, and `rx_snr` are written from the ISR and read from the main loop. A torn read on these fields is theoretically possible but harmless — the display just shows a stale frame; the next message overwrites everything.

---

## Known Limitations

- `TEXT_MESSAGE_APP`, `TELEMETRY_APP`, and `POSITION_APP` are decoded and delivered via callbacks (`proto_mode_set_telemetry_callback` / `proto_mode_set_position_callback`). The app must register those callbacks and surface the data in the UI / CSV. Admin and other portnums are still skipped.
- Sender display uses the last 4 hex digits of the node ID (`from & 0xFFFF`), e.g. `f69c: Hello`. `NodeInfo` frames are now parsed (`decode_node_info`) but only the local node's `device_metrics.battery_level` is extracted — the `User.long_name` is still not stored, so long sender names are not shown.
- The config exchange (~47 frames, ~1 KB) is received and processed by the UART callback state machine. Most frames are decoded and discarded; `my_info` (local node ID), the local `node_info` (battery), and `config_complete_id` are the ones that matter.

---

## Why the Serial Module PROTO Mode (and why UART0 was abandoned)

GhostMesh now uses the Meshtastic **Serial module in PROTO mode** on GPIO7 (RX) / GPIO6 (TX). An earlier note here claimed the Serial module PROTO mode "does not work reliably" and that the PhoneAPI on UART0 (GPIO43/44) was the correct path. **That conclusion was a misdiagnosis** — the real culprit was the pins, not the module:

- On the Heltec V3 the **CP2102 USB-UART bridge is wired to UART0 (GPIO43/44)** — the same pads the Flipper was connected to.
- The CP2102 is powered from USB. With the Heltec on **battery** (USB unplugged), the unpowered CP2102 **clamps GPIO43/44 to ground**, so nothing external can drive a valid signal into the ESP32. The PhoneAPI link only ever worked while the Heltec was plugged into USB power — and the Serial module PROTO experiments that "failed" had *also* been configured on 43/44, so they hit the exact same clamp.
- **Confirmed 2026-07-01:** plugging the Heltec into any USB power (even a dumb charger) made the old 43/44 link connect instantly; on battery it never did. Moving the Serial module to **free pins (6/7), which have no CP2102 on them**, makes PROTO connect on pure battery — the deployable configuration.

The `want_config` / `config_complete` handshake and everything else in this document is identical either way; only the Heltec-side pins and the required Serial-module config changed. The Flipper side is unchanged (USART1, pins 13/14).
