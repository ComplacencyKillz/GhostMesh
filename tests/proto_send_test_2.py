#!/usr/bin/env python3
"""
proto_send_test.py

Sends a Meshtastic PROTO-mode text message directly over serial,
bypassing the Flipper entirely.

Usage:
    python proto_send_test.py [COM_PORT] [MESSAGE]

    COM_PORT  : Windows COM port. Default: COM3
    MESSAGE   : Text to send. Default: "TEST FROM PYTHON"

Protocol note:
    Meshtastic PROTO mode requires a want_config_id handshake before the
    node will process any ToRadio packets. This script sends the handshake
    first, waits for config_complete_id, then sends the text message.
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


def frame(payload: bytes) -> bytes:
    """Wrap payload in Meshtastic PROTO framing: 0x94 0xC3 [len_hi] [len_lo]"""
    return bytes([0x94, 0xC3, (len(payload) >> 8) & 0xFF, len(payload) & 0xFF]) + payload


# ── Packet builders ───────────────────────────────────────────────────────────

NONCE = 42  # arbitrary uint32 used to match want_config_id with config_complete_id

def build_want_config_id() -> bytes:
    """
    ToRadio { want_config_id: NONCE }
    want_config_id is field 100 in ToRadio (stable across Meshtastic versions).
    """
    payload = field_varint(100, NONCE)
    return frame(payload)


def build_text_packet(text: str) -> bytes:
    """
    ToRadio { packet: MeshPacket { to: 0xFFFFFFFF, decoded: Data {
        portnum: TEXT_MESSAGE_APP, payload: text } } }
    """
    data  = field_varint(1, 1) + field_bytes(2, text.encode())
    mesh  = field_varint(3, 0xFFFFFFFF) + field_bytes(4, data)
    radio = field_bytes(1, mesh)
    return frame(radio)


# ── FromRadio parser (minimal) ────────────────────────────────────────────────

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
            if field == 4 and val == nonce:   # FromRadio.config_complete_id
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
    Read incoming bytes, find PROTO frames (0x94 0xC3 + length + payload),
    check each for config_complete_id. Return True when found.
    """
    deadline = time.time() + timeout_s
    state = 'IDLE'
    length = 0
    buf = b''

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
            state = 'LEN_LO'
        elif state == 'LEN_LO':
            length |= c
            buf = b''
            state = 'PAYLOAD' if 0 < length <= 512 else 'IDLE'
        elif state == 'PAYLOAD':
            buf += b
            if len(buf) >= length:
                if check_config_complete(buf, nonce):
                    return True
                state = 'IDLE'

    return False


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    port    = sys.argv[1] if len(sys.argv) > 1 else 'COM3'
    message = sys.argv[2] if len(sys.argv) > 2 else 'TEST FROM PYTHON'

    want_cfg_pkt = build_want_config_id()
    text_pkt     = build_text_packet(message)

    print(f"Port    : {port}")
    print(f"Message : {message!r}")
    print(f"Handshake packet ({len(want_cfg_pkt)} bytes): {want_cfg_pkt.hex(' ')}")
    print(f"Text packet      ({len(text_pkt)} bytes): {text_pkt.hex(' ')}")
    print()

    try:
        s = serial.Serial(port, 115200, timeout=0.1)
    except serial.SerialException as e:
        print(f"ERROR opening {port}: {e}")
        sys.exit(1)

    time.sleep(1)

    # Step 1: send want_config_id
    print("Step 1: Sending want_config_id handshake...")
    s.write(want_cfg_pkt)

    # Step 2: wait for config_complete_id
    print("Step 2: Waiting for config_complete_id (up to 15 seconds)...")
    connected = read_proto_frames(s, timeout_s=15, nonce=NONCE)

    if connected:
        print("         config_complete_id received — node is ready!")
    else:
        print("         config_complete_id NOT received (timeout).")
        print("         Sending text anyway as a fallback test...")

    # Step 3: send text message
    print(f"Step 3: Sending text: {message!r}")
    s.write(text_pkt)
    time.sleep(2)

    s.close()
    print()
    print("Done. Check:")
    print("  1. Did ChUtil on the Heltec OLED tick up?")
    print("  2. Did the message appear on the second node's Meshtastic app?")


if __name__ == '__main__':
    main()
