# GhostMesh Mesh Command CLI

A text-based command interface carried over the Meshtastic mesh. Any operator can query and
control any backpack — or all of them — by sending ordinary mesh text messages. Implemented in
the Heltec custom firmware as `heltec-firmware/CommandModule` (planned).

## Why it exists

GhostMesh is a peer **mesh of ≥2 Heltec backpacks** (Flippers optional). A backpack must be
controllable by its own operator *or* a teammate, over the mesh or IR, **without needing a
Flipper** — e.g. to sound a dropped backpack's buzzer, flash its LED, or wipe it. So backpacks
must *listen* for commands, not only broadcast events.

## Command format

```
/command @target [args]
```

- **`@target`** = the last 4 hex digits of a node ID (e.g. `@f69c`). **There is no `ALL`/broadcast
  target** — every command must name exactly one node (see *Security model*).
- A backpack acts (and replies) **only if `@target` matches its own node ID**.
- Commands are plain text on the **private channel**. Replies are plain text too, so they show
  on the Meshtastic app **and** the GhostMesh FAP.
- Commands are case-insensitive.

## Message-size rule

Meshtastic text is capped at **~200 characters**. Any multi-part output (notably `/help`) is
sent as **one message per item, in sequence** — never crammed into a single message. This keeps
every reply within the cap and leaves room for each command's help to grow.

## Commands

| Command | Args | Action |
|---------|------|--------|
| `/help` | — | Replies with **one message per command** (name + args + confirmation rule). |
| `/status` | — | Replies with node state: armed/disarmed, battery %, last tamper, GPS fix. |
| `/arm` | — | Arm the node (sets `ghostmesh_armed`); replies `ARMED`. |
| `/disarm` | — | Disarm the node; replies `DISARMED`. |
| `/led` | `<color/state>` | Set the RGB status LED (e.g. `green`, `red`, `off`). |
| `/buzz` | `[ms]` | Sound the buzzer (default: short beep). |
| `/vibrate` | `[ms]` | Run the vibration motor. |
| `/wipe` | `<token>` | Factory-reset the node. Requires armed + confirmation — see below. |

Every command — including `/help` and `/status` — must name a specific node id. There is **no**
broadcast target, so no single message can address more than one node.

## Wipe safety (defense in depth)

Wipe = **complete flash erase** — NVS (device key, BLE bonds), every data partition (the LittleFS
config + channel PSKs), *and the firmware itself* are erased; the ESP32-S3 drops to **USB download
mode** and must be reflashed (`firmware.factory.bin`) + reconfigured to return. Recoverable — the
ROM bootloader can't be erased — but it is a true nuke, not a config reset. Shared implementation:
`heltec-firmware/GhostMeshWipe.cpp` (`ghostmesh_complete_wipe()`). **Precondition for every path:
the node must be ARMED.** Three independent ways to fire it, each with its own confirm:

1. **Mesh:** `/wipe @node` → the node replies a one-time token (e.g. `confirm: A3F9`); the
   operator must send `/wipe @node A3F9` within ~30 s. Nothing is pre-shared; the token can't
   be replayed.
2. **IR remote (out-of-band):** send **ARM → WIPE → CONFIRM** in order (GhostMesh NECext, addr
   `0x474D`, cmds `01`/`03`/`04`), while armed, with CONFIRM inside ~10 s — decoded by `IRModule`,
   which factory-resets the node. This path never touches the mesh, so it survives a compromised
   radio/key. Remote: `flipper-app/GhostMeshBackpack.ir`.
3. **Physical button (GPIO37):** node armed **and** a **double-press** of the wipe button, with
   the 2nd press **>2 s and <5 s** after the 1st.

## Addressing model

Every backpack hears every command on the private channel, but a node acts **only** when `@target`
matches its own last-4 id. There is **no `ALL`** — a command can never address the whole mesh at
once. To act on several nodes, an operator sends one addressed command per node. This is still what
lets one operator control a teammate's dropped backpack — you target it by its id.

## Security model

- **The channel PSK is the outer gate.** Commands are ordinary text on the *encrypted* private
  channel; the mesh drops any packet it can't decrypt. An attacker cannot inject commands without
  the channel key — so the command syntax being open-source is **not** a weakness (the secret is the
  key, not the words). Kerckhoffs's principle: security rests on the key, not on obscurity.
- **No `@ALL`.** Every command names a single node, so no one message can hit the fleet — even a
  key-holder must know and name each node individually. This bounds the blast radius of a leaked key
  or a captured node, and defeats "spam a broadcast wipe until something answers."
- **`/wipe` is layered on top:** the target must be ARMED, and the wipe needs a second confirm — a
  one-time mesh token, the physical double-press, or the IR ARM→WIPE→CONFIRM sequence.
- **Residual risk:** a holder of the channel key is an insider with full channel access; per-node
  targeting and the armed gate slow them but don't stop them. True per-node authentication (signed
  `AdminMessage`s / per-node keys) is future work (v1.4). Keep the PSK secret and disarm staged nodes.

## Room to grow

The `/command @target` shape + one-message-per-reply means new commands (`/gps`, `/relay`,
`/photo`, …) can be added without redesigning the interface.
