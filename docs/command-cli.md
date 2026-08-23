# GhostMesh Mesh Command CLI

A text-based command interface carried over the Meshtastic mesh. Any operator can query and
control any backpack — or all of them — by sending ordinary mesh text messages. Implemented in
the Heltec custom firmware as <code>heltec-firmware/CommandModule</code>.

## Why it exists

GhostMesh is a peer **mesh of ≥2 Heltec backpacks** (Flippers optional). A backpack must be
controllable by its own operator *or* a teammate, over the mesh or IR, **without needing a
Flipper** — e.g. to sound a dropped backpack's buzzer, flash its LED, or wipe it. So backpacks
must *listen* for commands, not only broadcast events.

## Command format

<pre><code>
/command @target [args]
</code></pre>

- **<code>@target</code>** = the last 4 hex digits of a node ID (e.g. <code>@f69c</code>). **There is no <code>ALL</code>/broadcast
  target** — every command must name exactly one node (see *Security model*).
- A backpack acts (and replies) **only if <code>@target</code> matches its own node ID**.
- Commands are plain text on the **private channel**. Replies are plain text too, so they show
  on the Meshtastic app **and** the GhostMesh FAP.
- Commands are case-insensitive.

## Message-size rule

Meshtastic text is capped at **~200 characters**. Any multi-part output (notably <code>/help</code>) is
sent as **one message per item, in sequence** — never crammed into a single message. This keeps
every reply within the cap and leaves room for each command's help to grow.

## Commands

| Command | Args | Action |
|---------|------|--------|
| <code>/help</code> | — | Replies with **one message per command** (name + args + confirmation rule). |
| <code>/status</code> | — | Replies with node state: armed/disarmed, battery %, last tamper, GPS fix. |
| <code>/arm</code> | — | Arm the node (sets <code>ghostmesh_armed</code>). Mesh reply gated by <code>rep_arm</code> (default **off**). |
| <code>/disarm</code> | — | Disarm the node. Mesh reply gated by <code>rep_arm</code> (default **off**). |
| <code>/led</code> | <code><color\|gradient\|off></code> | Set the idle RGB colour (default off = covert), or run the looping green↔red gradient. Reply gated by <code>rep_led</code>. |
| <code>/buzz</code> | <code>[ms]</code> | Sound the buzzer (default: short beep). Reply gated by <code>rep_buzz</code>. |
| <code>/vibrate</code> | <code>[ms]</code> | Run the vibration motor. Reply gated by <code>rep_vib</code>. |
| <code>/fx</code> | <code><name></code> | Play an indicator effect for tuning — <code>armed</code>/<code>disarmed</code>/<code>wipe</code>/<code>msg</code>/<code>cli</code>/<code>gradient</code>/<code>off</code>. Visual only; <code>/fx wipe</code> does **not** erase. Reply gated by <code>rep_led</code>. |
| <code>/set</code> | <code><key> <val></code> | Tune + persist a setting — see the key tables below. |
| <code>/cfg</code> | — | Report the current config as one compact bitmask line (see below). |
| <code>/wipe</code> | <code><token></code> | Complete flash erase. Requires armed + confirmation — see below. |
| <code>/put</code> | <code>begin\|d\|end …</code> | Chunked file upload to the node's flash. Machine protocol — the web configurator drives it, not humans. See below. |
| <code>/ls</code> | — | Lists files staged in <code>/ghostmesh/</code> on the node's flash — one reply per file, then a count. Reply gated by <code>rep_status</code>. |
| <code>/get</code> | <code>begin\|ack …</code> | Chunked file download — the mirror of <code>/put</code>. Machine protocol. See below. |
| <code>/run</code> | <code><name></code> | Request a Bad USB launch **on whichever Flipper is wired to this backpack**, by payload name. Requires armed. The Heltec never executes anything — see below. |

## File transfer (<code>/put</code>)

A Meshtastic node exposes only its **PROTO StreamAPI** on serial — the USB port is the same
protobuf console the phone/web client does <code>want_config</code> over, not a raw TTY. So there is no pipe to
run YMODEM/XMODEM on: their framing would be parsed as malformed protobuf. A file instead rides the
**one channel we control** — <code>TEXT_MESSAGE_APP</code> — base64-chunked, reassembled on the node to
LittleFS, and CRC32-verified. USB is just the fast, reliable case; the identical protocol works
(slower) over the mesh. The web configurator's *Payload Upload* is the client.

<pre><code>
/put @id begin <fid> <nchunks> <bytes> <crc32hex> <name>   → PUT <fid> ready <n>
/put @id d <fid> <index> <base64>                          → (silent; written to flash)
/put @id end <fid>                                         → PUT <fid> ok <bytes>
                                                              | need <i,i,…>   (client resends, re-ends)
                                                              | crcfail | sizefail | toobig | nospace | timeout
</code></pre>

- **Chunk = 132 bytes** (base64 = 176 chars, no padding) — fits under the ~231-byte text cap.
- **fid** is a client-chosen id echoed in every reply, so overlapping/retried transfers don't collide.
- **Data chunks are silent** — no reply, no LED/buzzer effect — so a stream of hundreds doesn't flood
  airtime or strobe the node. Only <code>begin</code>/<code>end</code> reply.
- **Resumable:** <code>end</code> returns <code>need <list></code> for any missing chunks; the client resends just those and
  re-sends <code>end</code>. On success the node CRC32s the reassembled file and replies <code>ok <bytes></code>.
- **Lands in** <code>/ghostmesh/<name></code> on the node's LittleFS. **Ceiling** is free flash (a few hundred
  KB); an oversized transfer is rejected at <code>begin</code> with <code>toobig</code>/<code>nospace</code>.
- A transfer that goes quiet mid-stream is aborted after ~15 s (<code>timeout</code>).

## Listing and download (<code>/ls</code>, <code>/get</code>)

<code>/get</code> is <code>/put</code> with the roles reversed — the node reads a file and streams it out, paced by the
client's per-chunk ack instead of the client pacing the node. <code>/ls</code> is the directory listing that
tells you what's there to <code>/get</code> in the first place.

<pre><code>
/ls @id                        → LS <name> <bytes>   (one per file)
                                  LS end <count>
/get @id begin <name>          → GET <fid> begin <nchunks> <bytes> <crc32hex> <name>
                                  GET <fid> d 0 <base64>            (sent immediately, unprompted)
                                | GET 0 notfound | GET 0 toobig max=<KB>KB
/get @id ack <fid> <index>     → GET <fid> d <index+1> <base64>     (index was the node's last send)
                                | GET <fid> ok <crc32hex>            (index was the LAST chunk — done)
                                | GET <fid> d <index> <base64>       (any other ack: re-send current)
</code></pre>

Replies are routed to whoever asked — phone-only (no airtime) for the local USB/serial client, a
directed unicast for a remote node — so a small payload can be <code>/get</code> over the mesh too, just paced
by LoRa airtime instead of instant over USB. A stalled download aborts after ~15 s
(<code>GET <fid> timeout</code>), same as <code>/put</code>.

This is how a payload actually gets used, not just uploaded: <code>/put</code> a script onto a backpack over
USB, then either pull it onto the wired Flipper's own SD card (<code>/ext/badusb/</code>) via <code>/get</code>, or copy it
there directly with qFlipper — either way, once it's staged, <code>/run</code> (below) can fire it from
anywhere on the mesh.

## Running a payload (<code>/run</code>)

The Heltec **never executes anything** — <code>/run @id <name></code> only checks <code>ghostmesh_armed</code> and
acks/denies (gated by <code>rep_run</code>, default **on**). The actual trigger happens on the FAP side: because
every command the node processes still flows through to its own wired StreamAPI client (that's how
<code>/cfg</code>/<code>ARMED</code>/tamper broadcasts already reach the FAP), the Flipper attached to backpack <code>@id</code> sees
the exact same <code>/run @id <name></code> line and offers to launch it — on the **Payloads** screen, requiring
armed *and* a physical OK press there, and then Bad USB's own OK press on top of that before anything
actually fires. Three independent gates (mesh armed-check, FAP armed + OK, Bad USB's own OK) between
a mesh command and a keystroke. The Payloads screen also works with no mesh command at all — Up/Down
browses whatever's staged in <code>/ext/badusb/</code>, OK launches the selected one, same armed gate.

<code>/run</code> never carries bytes — only a name — matching the design constraint in
<code>docs/red-team-lab-use-cases.md</code> §5: payloads are pre-staged and selected by name, never injected.

## Configuration (persisted)

<code>/set</code> tunes a deployed node live — no reflash — and the change is saved to NVS, so it survives
reboot (until a wipe, which erases NVS too). <code>/cfg</code> reads the current values back.

**Sensing**

| Key | Effect |
|-----|--------|
| <code>prox <cm></code> | Proximity trip distance (<code>PERSON_DETECTED</code>) |
| <code>light <counts></code> | Light-tamper ADC threshold (<code>TAMPER_LIGHT</code>) |

**Mesh replies & broadcasts.** Every message the node can emit is individually gateable. Two kinds:
*broadcasts* (<code>bc_*</code>, plus <code>rep_arm</code>'s arm/disarm announcements) go to the whole channel; *command
replies* (all other <code>rep_*</code>) are answers to a command and are **routed only to whoever sent it** — a
command from the web configurator or a wired Flipper is answered off-mesh with zero LoRa airtime; a
command from a remote node gets a directed unicast, never a broadcast. So a reply only rides the mesh
when the command came *over* the mesh. Turn any of these off to go quieter still.

| Key <code><on\|off></code> | Gates | Default |
|-----|--------|---------|
| <code>rep_arm</code> | <code>/arm</code>+<code>/disarm</code> replies **and** the slide-switch / IR <code>ARMED</code>/<code>DISARMED</code> broadcasts | **off** |
| <code>rep_buzz</code> / <code>rep_vib</code> / <code>rep_led</code> | the <code>/buzz</code> / <code>/vibrate</code> / <code>/led</code>+<code>/fx</code> confirmation replies | **off** |
| <code>rep_wipe</code> | the <code>/wipe</code> reply **text** only — wipe *safety* (armed + confirm token + erase) is unchanged | **on** |
| <code>bc_tilt</code> / <code>bc_light</code> / <code>bc_prox</code> | the <code>TAMPER</code> / <code>TAMPER_LIGHT</code> / <code>PERSON_DETECTED</code> broadcasts | **on** |
| <code>rep_help</code> / <code>rep_status</code> | the <code>/help</code> listing / the <code>/status</code> reply | **on** |
| <code>rep_err</code> / <code>rep_unknown</code> | <code>/set</code> error messages / the unknown-command reply | **on** |
| <code>rep_run</code> | the <code>/run</code> accept/deny reply (the launch itself is never gated by this — see "Running a payload") | **on** |

> With <code>rep_wipe off</code>, the two-step mesh <code>/wipe</code> can't be completed (the token is never shown) — the
> physical double-press and IR <code>ARM→WIPE→CONFIRM</code> paths still work. **<code>/cfg</code> and the <code>/set</code> success
> echo are deliberately *not* gateable** — they're the control channel the configurator/FAP read to
> populate their UI and confirm a change; and since they route only to the requester, they never add
> mesh noise.

**Physical outputs** — does the hardware fire (the covert toggle)?

| Key <code><on\|off></code> | Effect | Default |
|-----|--------|---------|
| <code>led</code> / <code>buzz</code> / <code>vib</code> | RGB status LED / buzzer / vibration motor | on |
| <code>screen</code> | OLED display on/off (<code>Screen::setOn</code>) | on |
| <code>hbled</code> | onboard heartbeat LED (GPIO35) | on |
| <code>gpsled</code> | GPS PPS/fix LED — **best-effort** (UBX timepulse disable; may persist on some modules) | on |
| <code>notify</code> | led+buzz+vib at once (legacy covert toggle) | — |
| <code>silent <on\|off></code> | **master:** <code>on</code> = all six outputs off (screen dark, no LEDs, no sound) | — |

**Sensor inputs (battery)** — does the module poll its hardware?

| Key <code><on\|off></code> | Effect | Default |
|-----|--------|---------|
| <code>in_tilt</code> / <code>in_light</code> / <code>in_prox</code> / <code>in_ir</code> | enable/disable each sensor's polling | on |
| <code>sensors <on\|off></code> | **master:** all four inputs at once | — |

**GPS & telemetry (Meshtastic-native — applied to Meshtastic config, persisted, live/no reboot)**

| Key | Effect | Default |
|-----|--------|---------|
| <code>gps <on\|off></code> | GPS on/off (<code>config.position.gps_mode</code>). *Stops Meshtastic using GPS; on the current hardware it can't cut the module's power (the Vext power-gate on the PCB does that).* | on |
| <code>tel <on\|off></code> | environment (BME280) telemetry on/off (<code>moduleConfig.telemetry.environment_measurement_enabled</code>) | on |
| <code>gpsint <secs></code> | GPS update interval (<code>0</code> = Meshtastic default) | 0 |
| <code>telint <secs></code> | environment telemetry interval (<code>0</code> = default) | 0 |

**Presets (stance)** — one-touch postures, surfaced as the STANCE controls in the web configurator and
the FAP Settings screen. Each applies a whole composite in **one** command, so a preset never fires a
burst of self-addressed <code>/set</code>s (the router drops all but the first of a self-addressed burst):

| Command | Preset | Effect |
|-----|--------|--------|
| <code>/arm</code> · <code>/disarm</code> | **SENTINEL** | arm / disarm the tamper watch |
| <code>silent <on\|off></code> | **BLACKOUT** | all six physical outputs off / on |
| <code>mode <active\|deployed\|dormant></code> | **HIBERNATE** | power & sensing stance (below) |

- <code>mode active</code> — GPS on, telemetry normal, all sensors watching (full field use).
- <code>mode deployed</code> — GPS off, telemetry slowed, **tamper sensors stay live** (long-haul dead-drop).
- <code>mode dormant</code> — GPS off, telemetry minimal, **sensors off** (transport/storage, lowest draw).

These three are orthogonal axes — an armed dead-drop that hides is <code>SENTINEL</code> + <code>BLACKOUT</code> +
<code>HIBERNATE:deployed</code>.

### <code>/cfg</code> reply format

<code>/cfg</code> returns one compact line with the booleans packed into three hex bitmasks:

<pre><code>
CFG prox=<u> light=<u> rep=<hex> out=<hex> in=<hex> gps=<u> tel=<u> gpsint=<u> telint=<u> arm=<u>
</code></pre>

| Mask | bit0 | bit1 | bit2 | bit3 | bit4 | bit5 | bit6 | bit7 | bit8 | bit9 | bit10 | bit11 | bit12 |
|------|------|------|------|------|------|------|------|------|------|------|-------|-------|-------|
| <code>rep</code> | arm | buzz | vib | led | wipe | tilt-bc | light-bc | prox-bc | help | status | err | unknown | run |
| <code>out</code> | led | buzz | vib | screen | hbled | gpsled | — | — | | | | | |
| <code>in</code>  | tilt | light | prox | ir | — | — | — | — | | | | | |

<code>arm=</code> is the live arm state (<code>1</code>/<code>0</code>) — it drives the SENTINEL preset's displayed posture.

Example: <code>/cfg @f69c</code> → <code>CFG prox=200 light=2000 rep=1ff0 out=3f in=f gps=1 tel=1 gpsint=0 telint=0 arm=0</code>
(here <code>rep=1ff0</code> = wipe + all tamper broadcasts + all query replies + run accept/deny on, routine
command confirmations off — the default).

### Three ways to configure a node

The same <code>/set</code>/<code>/cfg</code> backend is reachable three ways:

| Surface | Transport | Use |
|---------|-----------|-----|
| **Web configurator** (<code>ghostmesh.info/config</code>) | **USB Web Serial** (browser), self-addressed (off-air) | primary no-install tool: sliders/toggles for every setting, plus firmware flash + <code>/put</code> upload |
| FAP Settings screen | Flipper link, self-addressed (off-air) | configure the backpack on your Flipper |
| Mesh CLI | over the air, from another node | tune / silence a **deployed** node remotely |
| <code>tools/configure_backpack.py</code> | **USB serial → PC**, self-addressed (off-air) | older scripted path, **no Flipper needed** |

The web, FAP, and USB paths address the command to the node's own id, so Meshtastic delivers it
in-node without transmitting — config commands stay off the air.

<pre><code>
pip install meshtastic
python tools/configure_backpack.py --port /dev/ttyUSB0              # show current config
python tools/configure_backpack.py --port /dev/ttyUSB0 --set prox 150
python tools/configure_backpack.py --port COM5 --set silent on       # all physical outputs off
</code></pre>

Every command — including <code>/help</code> and <code>/status</code> — must name a specific node id. There is **no**
broadcast target, so no single message can address more than one node.

## Indicators (event-driven feedback)

The backpack's LED, buzzer, and vibration motor fire automatically on events, as synced
light/sound effects (<code>CommandModule</code>'s effect engine):

| Event | LED | Sound / haptic |
|-------|-----|----------------|
| Armed | green→red ramp | rising two-note + vibration |
| Disarmed | red→green ramp | falling two-note + vibration |
| Wipe (confirmed) | 3 blue flashes → fade | low tone + long vibration (plays **before** the erase) |
| Message received | 3 yellow flashes | buzz + vibration |
| CLI command received | 2 green flashes | short buzz |

Arm/disarm effects fire regardless of the source (switch, IR, or mesh). At rest the LED holds its
steady colour — **off by default, for covert deployment**. Per-element output enables
(<code>led</code>/<code>buzz</code>/<code>vib</code>/<code>screen</code>/<code>hbled</code>/<code>gpsled</code>) are the covert toggles, and the <code>silent</code> master
flips all six off at once — all configurable via the settings layer (<code>/set</code>, web, or FAP).

## Wipe safety (defense in depth)

Wipe = **complete flash erase** — NVS (device key, BLE bonds), every data partition (the LittleFS
config + channel PSKs), *and the firmware itself* are erased; the ESP32-S3 drops to **USB download
mode** and must be reflashed (<code>firmware.factory.bin</code>) + reconfigured to return. Recoverable — the
ROM bootloader can't be erased — but it is a true nuke, not a config reset. Shared implementation:
<code>heltec-firmware/GhostMeshWipe.cpp</code> (<code>ghostmesh_complete_wipe()</code>). **Precondition for every path:
the node must be ARMED.** Three independent ways to fire it, each with its own confirm:

1. **Mesh:** <code>/wipe @node</code> → the node replies a one-time token (e.g. <code>confirm: A3F9</code>); the
   operator must send <code>/wipe @node A3F9</code> within ~30 s. Nothing is pre-shared; the token can't
   be replayed.
2. **IR remote (out-of-band):** send **ARM → WIPE → CONFIRM** in order (GhostMesh NECext, addr
   <code>0x474D</code>, cmds <code>01</code>/<code>03</code>/<code>04</code>), while armed, with CONFIRM inside ~10 s — decoded by <code>IRModule</code>,
   which factory-resets the node. This path never touches the mesh, so it survives a compromised
   radio/key. Remote: <code>flipper-app/GhostMeshBackpack.ir</code>.
3. **Physical button (GPIO37):** node armed **and** a **double-press** of the wipe button, with
   the 2nd press **>2 s and <5 s** after the 1st.

## Addressing model

Every backpack hears every command on the private channel, but a node acts **only** when <code>@target</code>
matches its own last-4 id. There is **no <code>ALL</code>** — a command can never address the whole mesh at
once. To act on several nodes, an operator sends one addressed command per node. This is still what
lets one operator control a teammate's dropped backpack — you target it by its id.

## Security model

- **The channel PSK is the outer gate.** Commands are ordinary text on the *encrypted* private
  channel; the mesh drops any packet it can't decrypt. An attacker cannot inject commands without
  the channel key — so the command syntax being open-source is **not** a weakness (the secret is the
  key, not the words). Kerckhoffs's principle: security rests on the key, not on obscurity.
- **No <code>@ALL</code>.** Every command names a single node, so no one message can hit the fleet — even a
  key-holder must know and name each node individually. This bounds the blast radius of a leaked key
  or a captured node, and defeats "spam a broadcast wipe until something answers."
- **<code>/wipe</code> is layered on top:** the target must be ARMED, and the wipe needs a second confirm — a
  one-time mesh token, the physical double-press, or the IR ARM→WIPE→CONFIRM sequence.
- **Residual risk:** a holder of the channel key is an insider with full channel access; per-node
  targeting and the armed gate slow them but don't stop them. True per-node authentication (signed
  <code>AdminMessage</code>s / per-node keys) is future work (v1.4). Keep the PSK secret and disarm staged nodes.

## Room to grow

The <code>/command @target</code> shape + one-message-per-reply means new commands (<code>/gps</code>, <code>/relay</code>,
<code>/photo</code>, …) can be added without redesigning the interface.
