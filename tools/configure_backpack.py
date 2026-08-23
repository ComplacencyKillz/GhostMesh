#!/usr/bin/env python3
"""Configure a GhostMesh backpack over USB — no Flipper needed.

Plug the Heltec into a PC and tune the GhostMesh firmware settings with the same `/set` and `/cfg`
commands the web configurator, the mesh CLI, and the FAP use — addressed to the node itself, so
nothing is broadcast over LoRa. (For a no-install GUI, prefer the web configurator at
ghostmesh.info/config; this script is the older scripted path.)

  pip install meshtastic
  python tools/configure_backpack.py --port /dev/ttyUSB0                 # show current config
  python tools/configure_backpack.py --port /dev/ttyUSB0 --set prox 150
  python tools/configure_backpack.py --port COM5 --set silent on         # all physical outputs off

Keys (see docs/command-cli.md for the full table):
  numerics   prox <cm> · light <counts> · gpsint <s> · telint <s>
  outputs    led|buzz|vib|screen|hbled|gpsled <on|off>   (silent <on|off> = all six at once)
  replies    rep_arm|rep_buzz|rep_vib|rep_led|rep_wipe <on|off>   bc_tilt|bc_light|bc_prox <on|off>
             rep_help|rep_status|rep_err|rep_unknown <on|off>   (/cfg + /set echo are never gated)
  inputs     in_tilt|in_light|in_prox|in_ir <on|off>     (sensors <on|off> = all four)
  native     gps <on|off>   tel <on|off>   notify <on|off> = led+buzz+vib only
  stance     mode <active|deployed|dormant>   (HIBERNATE composite; SENTINEL is /arm//disarm, not /set)

Requires GhostMesh firmware that processes self-directed commands (2026-08 or later).
"""
import argparse
import re
import sys
import time

# /cfg bitmask layout — must match the firmware's doCfg() (heltec-firmware/CommandModule.cpp).
_REP = ["arm", "buzz", "vib", "led", "wipe", "bc_tilt", "bc_light", "bc_prox",
        "help", "status", "err", "unknown"]
_OUT = ["led", "buzz", "vib", "screen", "hbled", "gpsled"]
_IN = ["tilt", "light", "prox", "ir"]


def decode_cfg(line):
    """Turn 'CFG prox=.. light=.. rep=<hex> out=<hex> in=<hex> gps=.. gpsint=.. telint=..'
    into a human-readable per-flag breakdown."""

    def num(k, base=10):
        m = re.search(r"(?:^| )" + k + r"=([0-9a-fA-F]+)", line)
        return int(m.group(1), base) if m else None

    def flags(mask, names):
        if mask is None:
            return "?"
        return " ".join(f"{n}={'on' if (mask >> i) & 1 else 'off'}" for i, n in enumerate(names))

    prox, light = num("prox"), num("light")
    gps, tel, gi, ti = num("gps"), num("tel"), num("gpsint"), num("telint")
    arm = num("arm")
    out = [f"  sensing : prox={prox}cm light={light}"]
    out.append(f"  replies : {flags(num('rep', 16), _REP)}")
    out.append(f"  outputs : {flags(num('out', 16), _OUT)}")
    out.append(f"  inputs  : {flags(num('in', 16), _IN)}")
    out.append(f"  gps/tel : gps={'on' if gps else 'off'} tel={'on' if tel else 'off'} gpsint={gi}s telint={ti}s")
    if arm is not None:
        out.append(f"  armed   : {'yes' if arm else 'no'}")
    return "\n".join(out)


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

    # Wait for the node's reply: a CFG line, a /set echo ("key=val"), or an error echo
    # ("SET: bad key/val" etc.) — so a bad key surfaces instead of a misleading "No reply".
    deadline = time.time() + args.timeout
    seen = None
    while time.time() < deadline:
        for r in replies:
            if r.startswith("CFG ") or "=" in r or r.startswith("SET") or "bad" in r.lower():
                seen = r
                break
        if seen:
            break
        time.sleep(0.1)

    iface.close()

    if seen:
        print(f"< {seen}")
        if seen.startswith("CFG "):
            print(decode_cfg(seen))
    else:
        print("No reply — is this GhostMesh firmware that processes self-directed commands, "
              "and is the node fully booted (RDY)?", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
