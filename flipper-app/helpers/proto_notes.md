# PROTO Mode — Implementation Notes

## What Is Implemented

GhostMesh uses a hand-coded minimal protobuf encoder/decoder in `proto_mode.c/.h`. No external libraries (no nanopb). All field numbers were confirmed by serializing known messages with the official meshtastic Python library (v2.7.8) and reading the wire encoding.

---

## Protocol Path

```
Flipper USART1 TX (pin 13)  →  Heltec GPIO44 (UART0 RX)  →  PhoneAPI
Heltec GPIO43 (UART0 TX)    →  Flipper USART1 RX (pin 14) →  proto_mode decoder
```

GhostMesh connects to the **PhoneAPI on UART0**, not to the Meshtastic SerialModule. This is the same interface used by the Meshtastic phone app and Python library over USB. No Meshtastic serial module configuration is required.

---

## Handshake Flow

```
FAP startup → send ToRadio { want_config_id: 42 }
Node sends  → ~47 FromRadio config frames
Node sends  → FromRadio { config_complete_id: 42 }  ← FAP sets connected=true
FAP ready   → ToRadio { packet: MeshPacket { ... } } can now be sent
```

The Flipper title bar shows `...` during handshake and `RDY` when connected.

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
| `config_complete_id` | **7** | varint |
| `rebooted` | 8 | varint (bool) |

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
- Sender display uses the last 4 hex digits of the node ID (`from & 0xFFFF`), e.g. `f69c: Hello`. Long names would require parsing a `NodeInfo` FromRadio frame, which arrives during the config exchange but is currently not stored.
- The config exchange (~47 frames, ~1 KB) is received and processed by the UART callback state machine. The first 46 frames are decoded and discarded; only `config_complete_id` matters.

---

## Why Not the SerialModule PROTO Mode?

The Meshtastic SerialModule PROTO mode (configured via Module Config → Serial in the app) was tested and does not work reliably in Meshtastic 2.7.15 via GPIO UART. Even the official meshtastic Python library times out when connecting through the SerialModule path. The PhoneAPI on UART0 (GPIO43/44) is reliable and is the correct path for full-featured PROTO clients.
