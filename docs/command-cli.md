# GhostMesh Mesh Command CLI

A text-based command interface carried over the Meshtastic mesh. Any operator can query and
control any backpack — or all of them — by sending ordinary mesh text messages. Implemented in
the Heltec custom firmware as `heltec-firmware/CommandModule`.

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
| `/arm` | — | Arm the node (sets `ghostmesh_armed`). Mesh reply gated by `rep_arm` (default **off**). |
| `/disarm` | — | Disarm the node. Mesh reply gated by `rep_arm` (default **off**). |
| `/led` | `<color\|gradient\|off>` | Set the idle RGB colour (default off = covert), or run the looping green↔red gradient. Reply gated by `rep_led`. |
| `/buzz` | `[ms]` | Sound the buzzer (default: short beep). Reply gated by `rep_buzz`. |
| `/vibrate` | `[ms]` | Run the vibration motor. Reply gated by `rep_vib`. |
| `/fx` | `<name>` | Play an indicator effect for tuning — `armed`/`disarmed`/`wipe`/`msg`/`cli`/`gradient`/`off`. Visual only; `/fx wipe` does **not** erase. Reply gated by `rep_led`. |
| `/set` | `<key> <val>` | Tune + persist a setting — see the key tables below. |
| `/cfg` | — | Report the current config as one compact bitmask line (see below). |
| `/wipe` | `<token>` | Complete flash erase. Requires armed + confirmation — see below. |
| `/put` | `begin\|d\|end …` | Chunked file upload to the node's flash. Machine protocol — the web configurator drives it, not humans. See below. |

## File transfer (`/put`)

A Meshtastic node exposes only its **PROTO StreamAPI** on serial — the USB port is the same
protobuf console the phone/web client does `want_config` over, not a raw TTY. So there is no pipe to
run YMODEM/XMODEM on: their framing would be parsed as malformed protobuf. A file instead rides the
**one channel we control** — `TEXT_MESSAGE_APP` — base64-chunked, reassembled on the node to
LittleFS, and CRC32-verified. USB is just the fast, reliable case; the identical protocol works
(slower) over the mesh. The web configurator's *Payload Upload* is the client.

```
/put @id begin <fid> <nchunks> <bytes> <crc32hex> <name>   → PUT <fid> ready <n>
/put @id d <fid> <index> <base64>                          → (silent; written to flash)
/put @id end <fid>                                         → PUT <fid> ok <bytes>
                                                              | need <i,i,…>   (client resends, re-ends)
                                                              | crcfail | sizefail | toobig | nospace | timeout
```

- **Chunk = 132 bytes** (base64 = 176 chars, no padding) — fits under the ~231-byte text cap.
- **fid** is a client-chosen id echoed in every reply, so overlapping/retried transfers don't collide.
- **Data chunks are silent** — no reply, no LED/buzzer effect — so a stream of hundreds doesn't flood
  airtime or strobe the node. Only `begin`/`end` reply.
- **Resumable:** `end` returns `need <list>` for any missing chunks; the client resends just those and
  re-sends `end`. On success the node CRC32s the reassembled file and replies `ok <bytes>`.
- **Lands in** `/ghostmesh/<name>` on the node's LittleFS. **Ceiling** is free flash (a few hundred
  KB); an oversized transfer is rejected at `begin` with `toobig`/`nospace`.
- A transfer that goes quiet mid-stream is aborted after ~15 s (`timeout`).

## Configuration (persisted)

`/set` tunes a deployed node live — no reflash — and the change is saved to NVS, so it survives
reboot (until a wipe, which erases NVS too). `/cfg` reads the current values back.

**Sensing**

| Key | Effect |
|-----|--------|
| `prox <cm>` | Proximity trip distance (`PERSON_DETECTED`) |
| `light <counts>` | Light-tamper ADC threshold (`TAMPER_LIGHT`) |

**Mesh replies & broadcasts.** Every message the node can emit is individually gateable. Two kinds:
*broadcasts* (`bc_*`, plus `rep_arm`'s arm/disarm announcements) go to the whole channel; *command
replies* (all other `rep_*`) are answers to a command and are **routed only to whoever sent it** — a
command from the web configurator or a wired Flipper is answered off-mesh with zero LoRa airtime; a
command from a remote node gets a directed unicast, never a broadcast. So a reply only rides the mesh
when the command came *over* the mesh. Turn any of these off to go quieter still.

| Key `<on\|off>` | Gates | Default |
|-----|--------|---------|
| `rep_arm` | `/arm`+`/disarm` replies **and** the slide-switch / IR `ARMED`/`DISARMED` broadcasts | **off** |
| `rep_buzz` / `rep_vib` / `rep_led` | the `/buzz` / `/vibrate` / `/led`+`/fx` confirmation replies | **off** |
| `rep_wipe` | the `/wipe` reply **text** only — wipe *safety* (armed + confirm token + erase) is unchanged | **on** |
| `bc_tilt` / `bc_light` / `bc_prox` | the `TAMPER` / `TAMPER_LIGHT` / `PERSON_DETECTED` broadcasts | **on** |
| `rep_help` / `rep_status` | the `/help` listing / the `/status` reply | **on** |
| `rep_err` / `rep_unknown` | `/set` error messages / the unknown-command reply | **on** |

> With `rep_wipe off`, the two-step mesh `/wipe` can't be completed (the token is never shown) — the
> physical double-press and IR `ARM→WIPE→CONFIRM` paths still work. **`/cfg` and the `/set` success
> echo are deliberately *not* gateable** — they're the control channel the configurator/FAP read to
> populate their UI and confirm a change; and since they route only to the requester, they never add
> mesh noise.

**Physical outputs** — does the hardware fire (the covert toggle)?

| Key `<on\|off>` | Effect | Default |
|-----|--------|---------|
| `led` / `buzz` / `vib` | RGB status LED / buzzer / vibration motor | on |
| `screen` | OLED display on/off (`Screen::setOn`) | on |
| `hbled` | onboard heartbeat LED (GPIO35) | on |
| `gpsled` | GPS PPS/fix LED — **best-effort** (UBX timepulse disable; may persist on some modules) | on |
| `notify` | led+buzz+vib at once (legacy covert toggle) | — |
| `silent <on\|off>` | **master:** `on` = all six outputs off (screen dark, no LEDs, no sound) | — |

**Sensor inputs (battery)** — does the module poll its hardware?

| Key `<on\|off>` | Effect | Default |
|-----|--------|---------|
| `in_tilt` / `in_light` / `in_prox` / `in_ir` | enable/disable each sensor's polling | on |
| `sensors <on\|off>` | **master:** all four inputs at once | — |

**GPS & telemetry (Meshtastic-native — applied to Meshtastic config, persisted, live/no reboot)**

| Key | Effect | Default |
|-----|--------|---------|
| `gps <on\|off>` | GPS on/off (`config.position.gps_mode`) | on |
| `gpsint <secs>` | GPS update interval (`0` = Meshtastic default) | 0 |
| `telint <secs>` | environment (BME280) telemetry interval (`0` = default) | 0 |

**Presets (stance)** — one-touch postures, surfaced as the STANCE controls in the web configurator and
the FAP Settings screen. Each applies a whole composite in **one** command, so a preset never fires a
burst of self-addressed `/set`s (the router drops all but the first of a self-addressed burst):

| Command | Preset | Effect |
|-----|--------|--------|
| `/arm` · `/disarm` | **SENTINEL** | arm / disarm the tamper watch |
| `silent <on\|off>` | **BLACKOUT** | all six physical outputs off / on |
| `mode <active\|deployed\|dormant>` | **HIBERNATE** | power & sensing stance (below) |

- `mode active` — GPS on, telemetry normal, all sensors watching (full field use).
- `mode deployed` — GPS off, telemetry slowed, **tamper sensors stay live** (long-haul dead-drop).
- `mode dormant` — GPS off, telemetry minimal, **sensors off** (transport/storage, lowest draw).

These three are orthogonal axes — an armed dead-drop that hides is `SENTINEL` + `BLACKOUT` +
`HIBERNATE:deployed`.

### `/cfg` reply format

`/cfg` returns one compact line with the booleans packed into three hex bitmasks:

```
CFG prox=<u> light=<u> rep=<hex> out=<hex> in=<hex> gps=<u> gpsint=<u> telint=<u> arm=<u>
```

| Mask | bit0 | bit1 | bit2 | bit3 | bit4 | bit5 | bit6 | bit7 | bit8 | bit9 | bit10 | bit11 |
|------|------|------|------|------|------|------|------|------|------|------|-------|-------|
| `rep` | arm | buzz | vib | led | wipe | tilt-bc | light-bc | prox-bc | help | status | err | unknown |
| `out` | led | buzz | vib | screen | hbled | gpsled | — | — | | | | |
| `in`  | tilt | light | prox | ir | — | — | — | — | | | | |

`arm=` is the live arm state (`1`/`0`) — it drives the SENTINEL preset's displayed posture.

Example: `/cfg @f69c` → `CFG prox=200 light=2000 rep=ff0 out=3f in=f gps=1 gpsint=0 telint=0 arm=0`
(here `rep=ff0` = wipe + all tamper broadcasts + all query replies on, routine command confirmations
off — the default).

### Three ways to configure a node

The same `/set`/`/cfg` backend is reachable three ways:

| Surface | Transport | Use |
|---------|-----------|-----|
| **Web configurator** (`ghostmesh.info/config`) | **USB Web Serial** (browser), self-addressed (off-air) | primary no-install tool: sliders/toggles for every setting, plus firmware flash + `/put` upload |
| FAP Settings screen | Flipper link, self-addressed (off-air) | configure the backpack on your Flipper |
| Mesh CLI | over the air, from another node | tune / silence a **deployed** node remotely |
| `tools/configure_backpack.py` | **USB serial → PC**, self-addressed (off-air) | older scripted path, **no Flipper needed** |

The web, FAP, and USB paths address the command to the node's own id, so Meshtastic delivers it
in-node without transmitting — config commands stay off the air.

```
pip install meshtastic
python tools/configure_backpack.py --port /dev/ttyUSB0              # show current config
python tools/configure_backpack.py --port /dev/ttyUSB0 --set prox 150
python tools/configure_backpack.py --port COM5 --set silent on       # all physical outputs off
```

Every command — including `/help` and `/status` — must name a specific node id. There is **no**
broadcast target, so no single message can address more than one node.

## Indicators (event-driven feedback)

The backpack's LED, buzzer, and vibration motor fire automatically on events, as synced
light/sound effects (`CommandModule`'s effect engine):

| Event | LED | Sound / haptic |
|-------|-----|----------------|
| Armed | green→red ramp | rising two-note + vibration |
| Disarmed | red→green ramp | falling two-note + vibration |
| Wipe (confirmed) | 3 blue flashes → fade | low tone + long vibration (plays **before** the erase) |
| Message received | 3 yellow flashes | buzz + vibration |
| CLI command received | 2 green flashes | short buzz |

Arm/disarm effects fire regardless of the source (switch, IR, or mesh). At rest the LED holds its
steady colour — **off by default, for covert deployment**. Per-element output enables
(`led`/`buzz`/`vib`/`screen`/`hbled`/`gpsled`) are the covert toggles, and the `silent` master
flips all six off at once — all configurable via the settings layer (`/set`, web, or FAP).

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
