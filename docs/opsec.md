# GhostMesh OPSEC

How GhostMesh keeps traffic private, what it leaks, how a node denies itself on capture, and how you get it back.

## The encryption picture

Three segments, three security properties.

| Segment | Encrypted | Detail | Risk |
|---------|-----------|--------|------|
| Flipper ↔ backpack (UART) | No | Plaintext PROTO serial (Phase 14 plans ChaCha20-Poly1305) | High if captured while powered and probed |
| Backpack ↔ mesh (default channel) | Yes — AES-256, **public key** | Meshtastic LongFast, key `AQ==` | Medium — any Meshtastic node can read it |
| Backpack ↔ mesh (private channel) | Yes — AES-256, private key | Your channel + random PSK | Low — opaque without the key |

**The default channel key `AQ==` is public** — baked into every Meshtastic device. Traffic there is encrypted in name only. Never run an engagement on it.

**The private channel is the security boundary of the whole platform.** A node can't be read, commanded, or joined without the PSK. Everything below assumes you're on a private channel.

---

## Creating a private channel (required)

A private channel uses a random 256-bit PSK known only to your team.

1. Open the Meshtastic app on the node (Bluetooth).
2. **Settings → Channels → Channel 0**, edit.
3. Set a custom **Name** (the name feeds key derivation and the frequency slot).
4. **Generate Key** — a random 256-bit PSK.
5. Share the channel **QR code** in person with every operator.
6. All nodes must share the identical name + key.

See [meshtastic-setup.md](meshtastic-setup.md) for the frequency-slot detail that trips up custom channels.

> Planned (Phase 6): generate the PSK on the Flipper from its hardware RNG and push it to the backpack over PROTO — no phone app in the field.

---

## Command security

Backpacks take commands two ways — a text CLI over the mesh (`/cmd @target`, see [command-cli.md](command-cli.md)) and line-of-sight IR. The security model:

- **The PSK is the gate.** Commands are plain text on the encrypted channel; the mesh drops anything it can't decrypt. Nobody off the channel can command anything. The command syntax being open-source is **not** a weakness — the secret is the key, not the words.
- **No broadcast target.** Every mesh command names one node's last-4 id; there is no `@ALL`. A key-holder can't one-shot the fleet — they must know and name each node. This bounds the blast radius of a leaked key or a captured node.
- **Residual risk:** a holder of the PSK is an insider with full channel access. Per-node targeting and the arming gate slow them; they don't stop them. Keep the PSK off compromised hardware, and disarm staged nodes. True per-node authentication (signed admin messages) is future work (Phase 14).

---

## The destruct

The destruct is a **complete flash erase** — not a config reset. It wipes NVS, the filesystem (config + channel keys), **and the firmware itself**, leaving the ESP32-S3 in USB download mode. Implementation: `heltec-firmware/GhostMeshWipe.cpp`.

**Precondition for every path: the node must be ARMED.** Three independent ways to fire it, each with its own confirm:

1. **Mesh** — `/wipe @node` returns a one-time token; `/wipe @node <token>` within ~30 s fires it. Nothing is pre-shared; the token can't be replayed.
2. **IR** — the sequence **ARM → WIPE → CONFIRM**, in order, while armed, CONFIRM within ~10 s. This path never touches the mesh, so it survives a compromised radio or key. The FAP's Control screen sends it behind an on-screen confirm.
3. **Physical** — armed + a double-press of the wipe button, 2nd press 2–5 s after the 1st.

It erases the operator's **own** device and nothing else. It is recoverable — the ROM bootloader can't be erased, so a wiped node always reflashes over USB — but it is a true burn, not a factory reset. Recover with a reflash + an encrypted config backup.

**When to use:** capture is imminent, or the hardware may already be compromised.

---

## Encrypted config backup

Because a wipe destroys the channel keys, you keep a way back. The FAP captures the node's config — device/module config **and the channel PSK** — during the connect handshake, encrypts it with an **operator passphrase (AES-256-GCM)**, and writes `SD:/apps_data/ghostmesh/backup_<id>.gmb`. Restore after reflashing with `tools/restore_backpack.py`.

- **The passphrase is never stored.** A captured Flipper yields only ciphertext.
- **The tension:** a backup *is* the key you just wiped, encrypted. If the Flipper carrying it is also captured, the whole thing rests on the passphrase strength. Use a strong one, and don't carry backups you don't need into the field.

---

## Metadata leakage

Even on a private channel, Meshtastic broadcasts metadata by default:

- **Node ID** — always visible to relays.
- **Position** — broadcast if GPS is enabled.
- **Device metrics** — battery and uptime, periodically.

For covert deployment:
- Silence emissions with GhostMesh's own config (`/set`, web configurator, or FAP Settings): `silent on`
  kills all physical outputs (OLED screen, RGB + onboard LEDs, buzzer, vibration; `gpsled` disables the
  GPS PPS LED, best-effort), `gps off` / `gpsint` / `telint` cut or slow position + telemetry, and the
  `rep_*`/`bc_*` flags stop mesh chatter (routine command replies are off by default).
- Set the role to **ROUTER** (Meshtastic Module Config) — relays traffic without announcing itself,
  invisible to standard "who's on the mesh" queries.

> Built: per-element output silencing + a `silent` master + GPS/telemetry control all ship in the
> config layer. Still Phase 6: a single one-press Stealth Mode toggle in the FAP that also flips the
> ROUTER role in the same action.

---

## Pre-deployment checklist

- [ ] Private channel on all nodes, freshly generated key
- [ ] Default channel not in use
- [ ] Position broadcasting disabled
- [ ] Device-metrics telemetry disabled
- [ ] Role set to ROUTER (covert deployments)
- [ ] Destruct verified on a **spare** board (it self-erases the firmware — prove it lands in download mode before trusting it)
- [ ] Each operator holds the channel QR offline
- [ ] Config backed up (encrypted) if you'll need the node back

---

## What GhostMesh does not do

- Jam or interfere with radio spectrum
- Capture or decrypt traffic on channels you do not own
- Control third-party Meshtastic nodes you do not own
- Store credentials, PII, or exfiltrated data
