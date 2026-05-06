#!/usr/bin/env python3
"""
proto_send_test.py

Sends a Meshtastic PROTO-mode text message directly over serial,
bypassing the Flipper entirely.

Usage:
    python proto_send_test.py [COM_PORT] [MESSAGE]

    COM_PORT : Windows COM port. Default: COM3
    MESSAGE  : Text to send. Default: "TEST FROM PYTHON"

Protocol:
    Meshtastic PROTO mode requires a want_config_id handshake before the
    node will process any ToRadio packets. This script sends the handshake
    first, waits for config_complete_id, then sends the text message.

    Field numbers confirmed from installed meshtastic Python library:
      ToRadio.want_config_id  = field 3  (verified: ToRadio(want_config_id=42)
                                          serializes to 0x182a)
      FromRadio.config_complete_id = field 4
"""

import sys
import time
import serial


# ── Protobuf helpers ──────────────────────────────────────────────────────────

def varint(v: int) -> bytes:
    out = []
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            b |= 0x80
        out.append(b)
        if not v:
            break
    return bytes(out)


def field_varint(fnum: int, val: int) -> bytes:
    return varint((fnum << 3) | 0) + varint(val)


def field_bytes(fnum: int, data: bytes) -> bytes:
    return varint((fnum << 3) | 2) + varint(len(data)) + data


def field_fixed32(fnum: int, val: int) -> bytes:
    """Encode a field with wire type 5 (32-bit fixed), little-endian."""
    import struct
    return varint((fnum << 3) | 5) + struct.pack('<I', val)


def frame(payload: bytes) -> bytes:
    """Wrap payload in Meshtastic PROTO framing: 0x94 0xC3 [len_hi] [len_lo]"""
    return bytes([0x94, 0xC3, (len(payload) >> 8) & 0xFF, len(payload) & 0xFF]) + payload


# ── Packet builders ───────────────────────────────────────────────────────────

NONCE = 42


def build_want_config_id() -> bytes:
    """ToRadio { want_config_id: NONCE } — field 3, uint32."""
    return frame(field_varint(3, NONCE))


def build_text_packet(text: str) -> bytes:
    """ToRadio { packet: MeshPacket { to: broadcast, decoded: Data { TEXT_MESSAGE_APP, text }, hop_limit: 3 } }
    Field numbers confirmed from meshtastic library serialization:
      to        = field 2, fixed32 (wire type 5)  — NOT field 3 varint
      decoded   = field 4, bytes                  — correct
      hop_limit = field 9, varint                 — NOT field 13
    """
    data  = field_varint(1, 1) + field_bytes(2, text.encode())
    mesh  = (field_fixed32(2, 0xFFFFFFFF)  # to = broadcast (field 2, fixed32)
           + field_bytes(4, data)           # decoded = Data  (field 4)
           + field_varint(9, 3))            # hop_limit = 3   (field 9)
    radio = field_bytes(1, mesh)
    return frame(radio)


# ── FromRadio parser ──────────────────────────────────────────────────────────

def read_varint_from(buf: bytes, pos: int):
    val, shift = 0, 0
    while pos < len(buf):
        b = buf[pos]; pos += 1
        val |= (b & 0x7F) << shift
        if not (b & 0x80):
            return val, pos
        shift += 7
    return None, pos


def check_config_complete(payload: bytes, nonce: int) -> bool:
    """Return True if this FromRadio payload contains config_complete_id == nonce."""
    pos = 0
    while pos < len(payload):
        tag, pos = read_varint_from(payload, pos)
        if tag is None:
            break
        field, wire = tag >> 3, tag & 7
        if wire == 0:
            val, pos = read_varint_from(payload, pos)
            if val is None:
                break
            if field == 7 and val == nonce:   # FromRadio.config_complete_id (field 7)
                return True
        elif wire == 2:
            length, pos = read_varint_from(payload, pos)
            if length is None:
                break
            pos += length
        elif wire == 1:
            pos += 8
        elif wire == 5:
            pos += 4
        else:
            break
    return False


def read_proto_frames(s: serial.Serial, timeout_s: float, nonce: int) -> bool:
    """
    Read bytes, find PROTO frames (0x94 0xC3 + len + payload),
    check each for config_complete_id. Return True when found.
    Prints each complete frame for debugging.
    """
    deadline = time.time() + timeout_s
    state  = 'IDLE'
    length = 0
    buf    = b''
    frames_seen = 0

    while time.time() < deadline:
        b = s.read(1)
        if not b:
            continue
        c = b[0]

        if state == 'IDLE':
            if c == 0x94:
                state = 'MAGIC2'
        elif state == 'MAGIC2':
            state = 'LEN_HI' if c == 0xC3 else 'IDLE'
        elif state == 'LEN_HI':
            length = c << 8
            state  = 'LEN_LO'
        elif state == 'LEN_LO':
            length |= c
            buf    = b''
            state  = 'PAYLOAD' if 0 < length <= 512 else 'IDLE'
        elif state == 'PAYLOAD':
            buf += b
            if len(buf) >= length:
                frames_seen += 1
                print(f"         Frame {frames_seen} ({len(buf)} bytes): {buf.hex(' ')}")
                if check_config_complete(buf, nonce):
                    return True
                state = 'IDLE'

    print(f"         Total PROTO frames seen: {frames_seen}")
    return False


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    port    = sys.argv[1] if len(sys.argv) > 1 else 'COM3'
    message = sys.argv[2] if len(sys.argv) > 2 else 'TEST FROM PYTHON'

    want_cfg_pkt = build_want_config_id()
    text_pkt     = build_text_packet(message)

    print(f"Port    : {port}")
    print(f"Message : {message!r}")
    print(f"Handshake ({len(want_cfg_pkt)} bytes): {want_cfg_pkt.hex(' ')}")
    print(f"Text pkt  ({len(text_pkt)} bytes): {text_pkt.hex(' ')}")
    print()

    try:
        s = serial.Serial(port, 115200, timeout=0.1,
                          rtscts=False, dsrdtr=False)
    except serial.SerialException as e:
        print(f"ERROR opening {port}: {e}")
        sys.exit(1)

    # Pulse DTR to reset, then start reading IMMEDIATELY — do not flush.
    # The node sends FromRadio{rebooted:true} during boot on UART0 (PhoneAPI).
    # The meshtastic library reads this while booting; we must do the same.
    print("Resetting device via DTR...")
    s.setDTR(False)
    time.sleep(0.1)
    s.setDTR(True)
    # No flush — we need the rebooted frame and any early config frames.

    print("Step 1: Waiting 1 s for boot, then sending want_config_id...")
    time.sleep(1)
    s.write(want_cfg_pkt)

    print("Step 2: Waiting for config_complete_id (up to 15 s)...")
    connected = read_proto_frames(s, timeout_s=15, nonce=NONCE)

    if connected:
        print("         config_complete_id received — ready!")
    else:
        print("         config_complete_id NOT received. Sending text as fallback...")

    # Flush any remaining config frames left in the buffer
    time.sleep(0.5)
    s.flushInput()

    print(f"Step 3: Sending text: {message!r}  <<< WATCH CHUTIL NOW >>>")
    s.write(text_pkt)

    # Read any PROTO frames the node sends in response (e.g. queueStatus, ack)
    print("Step 4: Reading node response for 5 s...")
    deadline = time.time() + 5
    state2 = 'IDLE'; length2 = 0; buf2 = b''; fn = 0
    while time.time() < deadline:
        b = s.read(1)
        if not b:
            continue
        c = b[0]
        if state2 == 'IDLE':
            if c == 0x94: state2 = 'M2'
        elif state2 == 'M2':
            state2 = 'LH' if c == 0xC3 else 'IDLE'
        elif state2 == 'LH':
            length2 = c << 8; state2 = 'LL'
        elif state2 == 'LL':
            length2 |= c; buf2 = b''
            state2 = 'PL' if 0 < length2 <= 512 else 'IDLE'
        elif state2 == 'PL':
            buf2 += b
            if len(buf2) >= length2:
                fn += 1
                print(f"  Response frame {fn} ({len(buf2)} bytes): {buf2.hex(' ')}")
                state2 = 'IDLE'

    s.close()

    print()
    print("Done. Check:")
    print("  1. Did ChUtil on the Heltec OLED tick up?")
    print("  2. Did the message appear on 2f74?")


if __name__ == '__main__':
    main()
