#!/usr/bin/env python3
"""Configure a GhostMesh backpack over USB — no Flipper needed.

Plug the Heltec into a PC and tune the GhostMesh firmware settings: proximity and light
thresholds, and the LED / buzzer / vibration notify toggles (the covert switch). It talks to the
node's CommandModule with the same `/set` and `/cfg` commands the mesh CLI and the FAP use —
addressed to the node itself, so nothing is broadcast over LoRa.

  pip install meshtastic
  python tools/configure_backpack.py --port /dev/ttyUSB0                 # show current config
  python tools/configure_backpack.py --port /dev/ttyUSB0 --set prox 150
  python tools/configure_backpack.py --port COM5 --set notify off        # silence everything

Keys: prox <cm> · light <counts> · led|buzz|vib <on|off> · notify <on|off> (all three).

Requires GhostMesh firmware that processes self-directed commands (2026-08 or later).
"""
import argparse
import sys
import time


def main():
    ap = argparse.ArgumentParser(description="Configure a GhostMesh backpack over USB serial.")
    ap.add_argument("--port", required=True, help="serial port (e.g. /dev/ttyUSB0, COM5)")
    ap.add_argument("--set", nargs=2, metavar=("KEY", "VAL"), help="set a config value, then exit")
    ap.add_argument("--timeout", type=float, default=6.0, help="seconds to wait for the reply")
    args = ap.parse_args()

    import meshtastic.serial_interface
    from pubsub import pub

    # Collect any text the node sends back while we're connected.
    replies = []

    def on_receive(packet, interface):
        try:
            dec = packet.get("decoded", {})
            if dec.get("portnum") == "TEXT_MESSAGE_APP":
                replies.append(dec.get("text", ""))
        except Exception:
            pass

    pub.subscribe(on_receive, "meshtastic.receive.text")

    iface = meshtastic.serial_interface.SerialInterface(devPath=args.port)
    my_num = iface.myInfo.my_node_num
    last4 = f"{my_num & 0xFFFF:04x}"
    print(f"Connected to node !{my_num:08x} (@{last4})")

    if args.set:
        key, val = args.set
        cmd = f"/set @{last4} {key} {val}"
    else:
        cmd = f"/cfg @{last4}"

    # Self-addressed so Meshtastic delivers it in-node without transmitting.
    replies.clear()
    iface.sendText(cmd, destinationId=my_num, wantAck=False)
    print(f"> {cmd}")

    # Wait for the node's reply (a /set echo, or the CFG line).
    deadline = time.time() + args.timeout
    seen = None
    while time.time() < deadline:
        for r in replies:
            if r.startswith("CFG ") or "=" in r:
                seen = r
                break
        if seen:
            break
        time.sleep(0.1)

    iface.close()

    if seen:
        print(f"< {seen}")
    else:
        print("No reply — is this GhostMesh firmware that processes self-directed commands, "
              "and is the node fully booted (RDY)?", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
