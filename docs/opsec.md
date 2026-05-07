# GhostMesh OPSEC Guide

## The Encryption Picture

GhostMesh operates across three segments with different security properties.

| Segment | Encrypted? | Protocol | Risk |
|---------|-----------|----------|------|
| Flipper → Heltec (UART) | No (Phase 14 plans it) | Plaintext serial | High if captured while powered |
| Heltec → Mesh (default channel) | Yes — AES-256, **public key** | Meshtastic LongFast | Medium — any Meshtastic node can read it |
| Heltec → Mesh (private channel) | Yes — AES-256, private key | Custom channel | Low — mathematically opaque to outsiders |

**The default channel (LongFast) uses the key `AQ==`.** This key is published in the
Meshtastic source code and baked into every Meshtastic device. Traffic on the default
channel is encrypted in the technical sense but readable by anyone with a Meshtastic node.
**Do not use the default channel for sensitive communications.**

---

## Creating a Private Channel (Required for Operational Use)

A private channel uses a random 256-bit key known only to your squad. Anyone without the
key cannot decrypt your traffic even if they capture the raw LoRa packets.

### Using the Meshtastic app

1. Open the Meshtastic app connected to your node via Bluetooth
2. Go to **Settings → Channels → Channel 0**
3. Tap the pencil icon to edit
4. Set a custom **Name** (the channel name is also part of the key derivation)
5. Tap **Generate Key** — this creates a random 256-bit PSK
6. **Share the channel QR code** with every squad member out-of-band (in person)
7. All nodes must use the identical name + key combination

### Planned: key generation from Flipper (Phase 6)

Phase 6 will add the ability to generate a channel key directly on the Flipper using its
hardware RNG and push it to the Heltec over the existing PROTO link — no phone app needed.

---

## UART Security

The serial connection between the Flipper and Heltec is plaintext. If your hardware is
captured while powered, an adversary with a logic analyzer on GPIO43/44 can read all
traffic in real time. Mitigations:

- **Never leave the rig powered and unattended** (unless intentionally deployed as a dead-drop)
- **Use the nuke button** (Phase 6) — one key combo wipes all channel keys from the Heltec instantly
- **Phase 14** will add ChaCha20-Poly1305 authenticated encryption to the UART link

---

## Metadata Leakage

Even on a private channel with AES-256, Meshtastic nodes broadcast metadata by default:

- **Node ID** — your hardware's unique ID is always visible to mesh relays
- **Position** — if GPS is enabled, your node broadcasts coordinates
- **Device metrics** — battery level and uptime are periodically broadcast

For covert deployment:

- Disable position broadcasting in Meshtastic Module Config → GPS
- Disable device metrics telemetry in Module Config → Telemetry
- Set device role to **ROUTER** — the node relays traffic but does not initiate NodeInfo
  announcements, making it invisible to standard "who's on the mesh" queries
- **Phase 6** will add a single-toggle Stealth Mode in the GhostMesh UI that sends all
  three of these config commands in one action

---

## The Nuke Button (Phase 6)

The nuke button sends `AdminMessage { factory_reset: true }` to the Heltec via the
existing PROTO UART connection. Meshtastic processes this natively — it wipes all channel
keys, node info, and configuration, then reboots to factory state. The radio becomes a
useless unconfigured device.

**Gating:** the nuke button only fires if the slide switch on the Flipper ProtoBoard is in
the ARMED position. This prevents accidental wipes.

**Confirmation:** the Heltec's onboard LED blinks once after the wipe completes.

**When to use:** any time capture is imminent or the hardware may be compromised.

---

## Backpack Dead-Drop Security (Phase 10+)

When deploying the Heltec backpack unattended:

1. Set the slide switch on the Heltec to ARMED before leaving
2. The tilt switch (GPIO2) will broadcast a TAMPER alert over LoRa if the backpack is moved
3. The photoresistor (GPIO5) will broadcast a TAMPER_LIGHT alert if the case is opened
4. The IR receiver (GPIO48) allows you to remotely arm/disarm from ~10m using any NEC
   remote or the Flipper's built-in IR transmitter — no need to touch the backpack

---

## Recommended Pre-Deployment Checklist

- [ ] Private channel configured on all nodes with a freshly generated key
- [ ] Default channel disabled or de-prioritized
- [ ] Position broadcasting disabled
- [ ] Device metrics telemetry disabled
- [ ] Device role set to ROUTER
- [ ] Nuke button tested in a lab environment before field use
- [ ] Slide switch confirmed functional (ARMED position verified)
- [ ] Each team member has the channel QR code stored offline

---

## What GhostMesh Does Not Do

- Jam or interfere with radio spectrum
- Capture or decrypt traffic on channels you do not own
- Implement unauthorized C2 of third-party devices
- Store credentials, PII, or exfiltrated data

Any use of GhostMesh against systems you do not own or have explicit written authorization
to test is outside the scope of this project and is your legal responsibility.
