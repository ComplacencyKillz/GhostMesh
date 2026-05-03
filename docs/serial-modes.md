# Meshtastic Serial Modes

## Available Modes

Meshtastic exposes a serial module that can be configured to one of these modes:

| Mode | Description |
|------|-------------|
| DEFAULT | Protobuf framing — same as PROTO |
| SIMPLE | Raw byte echo / passthrough |
| PROTO | Full protobuf serial framing (recommended long-term) |
| TEXTMSG | Plain UTF-8 text lines → broadcast as mesh text messages |
| NMEA | NMEA GPS sentence output |
| CALTOPO | CalTopo mapping format output |
| WS85 | Weather station sensor format |
| VE_DIRECT | Victron Energy battery monitor format |
| MS_CONFIG | Internal config format |
| LOG | Raw firmware log output |
| LOGTEXT | Firmware log as plain text |

---

## MVP Recommendation: TEXTMSG

### Why TEXTMSG for v0.1/v0.2

**TEXTMSG is the correct mode for the GhostMesh MVP.** Here is why:

- **Send:** Write a UTF-8 string terminated with `\n` to the UART. The Meshtastic node broadcasts it as a text message on the mesh. No framing, no protobuf, no header.
- **Receive:** Incoming mesh text messages are output to the UART as plain UTF-8 lines. Easy to parse.
- **No library dependencies:** Works with raw `furi_hal_serial_tx` — no additional parsing code needed on the Flipper.
- **Human-readable:** Easy to debug with PuTTY or any serial terminal.

**TEXTMSG limitations:**
- Only handles text messages. Cannot read node info, telemetry, routing data, or position updates.
- No access to mesh metadata (RSSI, SNR, hop count, sender node ID) unless Meshtastic serializes them into the text line (it does not in TEXTMSG mode).
- Not suitable for full mesh client control.

### TEXTMSG message format

Sending:
```
CHECKIN OK\n
```
The Meshtastic node broadcasts `CHECKIN OK` as a mesh text message.

Receiving (from UART, one message per line):
```
NodeName: Hello from the mesh\n
```
Exact format depends on Meshtastic firmware version. Test with your setup.

---

## SIMPLE mode

SIMPLE mode provides a minimal passthrough. It echoes bytes received on UART out to mesh and vice versa. Less structured than TEXTMSG. Documentation is thinner and behavior has varied between Meshtastic firmware versions.

**Not recommended for GhostMesh.** TEXTMSG is better documented and more predictable.

---

## Long-term Recommendation: PROTO

### Why PROTO for v0.3+

PROTO mode is Meshtastic's full serial API. It transmits and receives protobuf-encoded `ToRadio` and `FromRadio` packets framed with a 4-byte header:

```
[0x94 0xC3] [length_MSB length_LSB] [protobuf payload...]
```

With PROTO mode you can:
- Read and send text messages with full metadata (sender, RSSI, SNR, hop count)
- Query node info (node ID, name, hardware, battery)
- Read telemetry (temperature, battery voltage, GPS position)
- Send admin commands
- Subscribe to mesh events

**PROTO limitations for v0.1:**
- Requires protobuf encoding/decoding on the Flipper (nanopb or a hand-rolled minimal parser)
- Requires serial framing (start bytes + length prefix + CRC)
- Non-trivial to implement correctly without a full test rig
- Adds a dependency (nanopb .c/.h generated files for meshtastic.proto)

### PROTO upgrade path

When GhostMesh is ready for PROTO mode:

1. Generate nanopb sources from the [Meshtastic protobufs](https://github.com/meshtastic/protobufs)
2. Add nanopb to `flipper-app/lib/nanopb/`
3. Replace `textmsg_mode.c` with `proto_mode.c` implementing `ToRadio`/`FromRadio` framing
4. Update `uart_helper` to support DMA-buffered receive for multi-byte packets
5. Set Meshtastic serial mode to `PROTO` (DEFAULT also works)

See `helpers/proto_notes.md` for more detail.

---

## Configuration Summary

| Goal | Mode | Flipper code |
|------|------|--------------|
| MVP canned messages | TEXTMSG | `textmsg_send(helper, "msg")` |
| Debug / byte counting | Any | `uart_helper_send` raw bytes |
| Full mesh client | PROTO | nanopb + framing (future) |
| GPS passthrough | NMEA | Raw parse — not planned |

---

## Setting the Mode in Meshtastic

1. Open the Meshtastic mobile app
2. Connect to your Heltec node via Bluetooth
3. Navigate to: **Settings → Module Config → Serial**
4. Set **Enabled: On**
5. Set **Mode: TEXTMSG**
6. Set **Baud Rate: 115200**
7. Set **RX pin** and **TX pin** to the GPIO numbers on your Heltec board (defaults are usually correct)
8. Save and reboot the node

After reboot, confirm the serial module is active by checking Settings → Module Config → Serial — the Enabled toggle should remain on.
