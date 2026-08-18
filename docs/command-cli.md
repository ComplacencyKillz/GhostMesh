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

- **`@target`** = the last 4 hex digits of a node ID (e.g. `@f69c`), or **`ALL`**.
- A backpack acts (and replies) **only if `@target` matches its own node ID or is `ALL`**.
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

`ALL` is fine for `/help`, `/status`, `/led`, `/buzz`, `/vibrate` — **never** for `/wipe`.

## Wipe safety (defense in depth)

Wipe = Meshtastic **factory reset** (erases channel keys + config). **Precondition for every
path: the node must be ARMED.** Three independent ways to fire it, each with its own confirm:

1. **Mesh:** `/wipe @node` → the node replies a one-time token (e.g. `confirm: A3F9`); the
   operator must send `/wipe @node A3F9` within ~30 s. Nothing is pre-shared; the token can't
   be replayed.
2. **IR remote:** the button sequence **ARM → WIPE → CONFIRM WIPE**, in order, on the remote
   (`flipper-app/GhostMeshBackpack.ir`). Needs a `CONFIRM WIPE` button.
3. **Physical button (GPIO37):** node armed **and** a **double-press** of the wipe button, with
   the 2nd press **>2 s and <5 s** after the 1st.

## Addressing model

Every backpack hears every broadcast on the private channel. Targeting (`@node`) makes only the
intended node act; `ALL` hits everyone. This is what lets one operator control a teammate's
dropped backpack.

## Room to grow

The `/command @target` shape + one-message-per-reply means new commands (`/gps`, `/relay`,
`/photo`, …) can be added without redesigning the interface.
