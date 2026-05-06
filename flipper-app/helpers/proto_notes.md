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

The Flipper shows `PROTO:...` during handshake and `PROTO:RDY` when connected.

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
| `config_complete_id` | **7** | varint |
| `rebooted` | 8 | varint (bool) |

### MeshPacket
| Field | Number | Wire |
|-------|--------|------|
| `from` | **1** | **fixed32** |
| `to` | **2** | **fixed32** |
| `decoded` (Data) | 4 | bytes |
| `hop_limit` | **9** | varint |

### Data
| Field | Number | Wire |
|-------|--------|------|
| `portnum` | 1 | varint (`TEXT_MESSAGE_APP = 1`) |
| `payload` | 2 | bytes |

> **Note:** `to` and `from` use `fixed32` wire type (type 7 in the descriptor, wire type 5), NOT varint. Using the wrong wire type causes the firmware to silently drop the packet.

---

## Known Limitations

- Only `TEXT_MESSAGE_APP` packets are decoded on receive. Other portnums (telemetry, position, admin) are decoded but not surfaced to the UI yet.
- Sender display uses the last 4 hex digits of the node ID (`from & 0xFFFF`), e.g. `f69c: Hello`. Long names would require parsing a `NodeInfo` FromRadio frame, which arrives during the config exchange but is currently not stored.
- The config exchange (~47 frames, ~1 KB) is received and processed by the UART callback state machine. The first 46 frames are decoded and discarded; only `config_complete_id` matters.

---

## Why Not the SerialModule PROTO Mode?

The Meshtastic SerialModule PROTO mode (configured via Module Config → Serial in the app) was tested and does not work reliably in Meshtastic 2.7.15 via GPIO UART. Even the official meshtastic Python library times out when connecting through the SerialModule path. The PhoneAPI on UART0 (GPIO43/44) is reliable and is the correct path for full-featured PROTO clients.
