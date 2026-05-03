# PROTO Mode — Implementation Notes

## Overview

Meshtastic PROTO (and DEFAULT) serial mode wraps protobuf payloads in a 4-byte framing header:

```
Byte 0:  0x94  (start magic)
Byte 1:  0xC3  (start magic)
Byte 2:  length MSB
Byte 3:  length LSB
Byte 4+: protobuf payload (ToRadio or FromRadio message)
```

The payload is a serialized `ToRadio` protobuf for host→node, and a `FromRadio` protobuf for node→host. Protobuf definitions are at https://github.com/meshtastic/protobufs.

---

## Why Not v0.1

Implementing PROTO mode requires:

1. **Protobuf codec** — nanopb is the standard choice for embedded C. It requires running `nanopb_generator.py` against the Meshtastic `.proto` files to produce `.pb.c` / `.pb.h` sources.
2. **Serial framing** — byte-by-byte state machine to detect start bytes, read the 2-byte length, buffer the payload, then decode.
3. **DMA-buffered UART receive** — TEXTMSG works fine with per-byte async callbacks. PROTO packets are multi-byte and require a buffer to accumulate before decoding.
4. **Test tooling** — validating protobuf encode/decode on the Flipper requires either a connected node in PROTO mode or a mock byte stream.

None of these are insurmountable, but they add meaningful complexity for a first commit. TEXTMSG gives 80% of the canned-message value with 10% of the code.

---

## Migration Plan (when ready for PROTO)

### Step 1: Get nanopb and meshtastic protobufs

```bash
pip install nanopb
git clone https://github.com/meshtastic/protobufs.git
python -m grpc_tools.protoc -I protobufs --nanopb_out=flipper-app/lib/nanopb/ \
    protobufs/meshtastic/mesh.proto \
    protobufs/meshtastic/portnums.proto \
    protobufs/meshtastic/telemetry.proto
```

Place the generated `.pb.c` / `.pb.h` files in `flipper-app/lib/nanopb/`.

### Step 2: Implement serial framing

Create `helpers/proto_mode.c` with a state machine:

```c
typedef enum {
    PROTO_FRAME_IDLE,
    PROTO_FRAME_START1,   // saw 0x94
    PROTO_FRAME_START2,   // saw 0xC3
    PROTO_FRAME_LEN_HI,
    PROTO_FRAME_LEN_LO,
    PROTO_FRAME_PAYLOAD,
} ProtoFrameState;
```

Each byte from the UART callback advances the state machine. When a full packet is buffered, post a `FuriMessageQueue` event to the main thread for decode.

### Step 3: Encode ToRadio messages

```c
ToRadio to_radio = ToRadio_init_zero;
to_radio.which_payload_variant = ToRadio_packet_tag;
to_radio.payload_variant.packet.decoded.portnum = PortNum_TEXT_MESSAGE_APP;
// ... fill payload ...

uint8_t buf[256];
pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
pb_encode(&stream, ToRadio_fields, &to_radio);

uint16_t len = stream.bytes_written;
uint8_t header[4] = {0x94, 0xC3, (len >> 8) & 0xFF, len & 0xFF};
uart_helper_send_bytes(helper, header, 4);
uart_helper_send_bytes(helper, buf, len);
```

### Step 4: Switch Meshtastic serial mode

In the Meshtastic app: **Settings → Module Config → Serial → Mode → PROTO**

### Step 5: Update `uart_helper` for buffered receive

The current per-byte callback approach works but is inefficient for PROTO. Consider switching to `furi_hal_serial_dma_rx_start` (if available in SDK) or accumulating bytes into a `FuriStreamBuffer` for batch processing.

---

## Reference

- Meshtastic serial API: https://meshtastic.org/docs/development/firmware/serial-module/
- Meshtastic protobuf definitions: https://github.com/meshtastic/protobufs
- nanopb documentation: https://jpa.kapsi.fi/nanopb/
- Flipper Zero SDK UART: `furi_hal_serial.h` in the Flipper firmware repo
