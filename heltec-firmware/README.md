---
---
# heltec-firmware — Custom Meshtastic Modules

The GhostMesh Heltec "backpack" runs stock **Meshtastic** plus a small number of custom C++
modules for features Meshtastic doesn't provide natively (tamper detection, proximity, jammer
detection, MAX17048 state-of-charge). This directory holds **only the custom module source**
and build notes — not a fork of the full firmware.

## Pinned firmware version

Build against Meshtastic tag **<code>v2.7.15.567b8ea</code>** — the version deployed on the project's
nodes. A different tag may build fine but can shift file layout / APIs.

## Build (Ubuntu)

1. Clone the firmware at the pinned tag, with submodules:
   ```bash
   git clone --depth 1 --branch v2.7.15.567b8ea --recurse-submodules --shallow-submodules \
     https://github.com/meshtastic/firmware.git meshtastic-firmware
<pre><code>
2. Run the setup script from this directory — copies the modules in, registers them in
   <code>src/modules/Modules.cpp</code>, and applies the GPS vendor patch. Idempotent (safe to re-run):
   ```bash
   ./setup.sh path/to/meshtastic-firmware      # defaults to ~/repos/meshtastic-firmware
</code></pre>
   Everything GhostMesh-specific lives in this directory: the modules (<code>*.cpp/.h</code>), the module
   registration (encoded in <code>setup.sh</code>), and the one change to Meshtastic's *own* source —
   <code>gps-timepulse.patch</code> (a <code>git diff</code> vs the pinned tag adding <code>GPS::setTimepulseEnabled()</code>, needed
   for the silent-mode <code>gpsled</code> toggle; the build won't link without it). We vendor **only** these,
   not a full Meshtastic fork — <code>setup.sh</code> drops them into a stock checkout.
3. Build for the Heltec V3:
   ```bash
   pip install platformio           # once; a venv is fine
   cd meshtastic-firmware
   pio run -e heltec-v3
<pre><code>
   Output: <code>.pio/build/heltec-v3/firmware.factory.bin</code>.
4. Flash <code>firmware.factory.bin</code> to the Heltec (from Windows or wherever your flasher runs). Or flash
   from the browser at **ghostmesh.info/config** (Web Serial + esptool-js — hosts the latest build).

## Modules

| Module | Purpose | Status |
|--------|---------|--------|
| ArmingModule | slide switch (GPIO4, SPDT) → broadcast <code>ARMED</code>/<code>DISARMED</code>; sets shared <code>ghostmesh_armed</code> | ✅ working |
| TiltModule | SW-520D tilt (GPIO2) → broadcast <code>TAMPER</code> (replaces built-in Detection Sensor) | ✅ working |
| LightTamperModule | photoresistor (GPIO5, light) → broadcast <code>TAMPER_LIGHT</code> | ✅ working |
| ProximityModule | RCWL-1601 (GPIO38 trig / GPIO47 echo) → broadcast <code>PERSON_DETECTED</code> | ✅ working — RCWL-1601 at 3.3V, no divider (HC-SR04 also works but needs 5V + an Echo divider) |
| IRModule | VS1838B (GPIO48) decodes the **GhostMesh NECext set** (addr <code>0x474D</code>: <code>01</code>ARM <code>02</code>DISARM <code>03</code>WIPE <code>04</code>CONFIRM) → arm/disarm (sets <code>ghostmesh_armed</code>, broadcasts) + out-of-band **wipe** via the <code>ARM→WIPE→CONFIRM</code> sequence (armed, CONFIRM within 10 s) | ✅ arm/disarm working; wipe sequence new. TX: <code>flipper-app/GhostMeshBackpack.ir</code> |
| CommandModule | **Listens** for <code>/cmd @target [args]</code> mesh text (per-node id only, **no <code>@ALL</code>**) → drives buzzer (GPIO39, passive/tone), vibration (GPIO40), LED, status, arm/disarm, the safety-gated wipe (mesh token + physical double-press on GPIO37), live config (<code>/set</code>/<code>/cfg</code>), and chunked file upload (<code>/put</code> → LittleFS, CRC32-verified; see <code>CommandModule_payload.cpp</code>) | ✅ <code>/buzz</code>+<code>/vibrate</code> confirmed on hardware 2026-08-18 — see <code>docs/command-cli.md</code> |

**Armed gate:** <code>ArmingModule</code> maintains <code>volatile bool ghostmesh_armed</code> (<code>GhostMeshArming.h</code>). Tilt/Light/Proximity only broadcast when armed, so the backpack can be handled while DISARMED without spamming the mesh.

**CommandModule is the first *receiving* module.** The others only broadcast; CommandModule overrides <code>handleReceived()</code> to parse incoming text. Backpack output pins (verified against the board header photo): buzzer **GPIO39** (passive — driven with a PWM <code>tone()</code>, not DC), vibration motor **GPIO40** (on/off), physical wipe button **GPIO37** (<code>INPUT_PULLUP</code>), RGB status LED on **GPIO26** (external SK6812, driven via <code>neopixelWrite</code> — colors + a green↔red gradient sweep, working on hardware; the onboard GPIO35 LED mirrors its on/off state). Registration is the same as any module (<code>#include</code> + <code>new CommandModule();</code> in <code>setupModules()</code>).

> **Build-time APIs to sanity-check against tag <code>v2.7.15.567b8ea</code>** (fix in one line if the layout shifted): <code>nodeDB->getNodeNum()</code>, <code>isFromUs()</code>/<code>getFrom()</code> (NodeDB.h), <code>powerStatus->getBatteryChargePercent()</code> (PowerStatus.h), <code>nodeDB->factoryReset()</code> + the global <code>rebootAtMsec</code> (main.h), and Arduino <code>tone()</code>/<code>noTone()</code> (fallback: LEDC, noted inline in <code>CommandModule.cpp</code>).

**Two hard requirements:** (1) **disable the built-in Detection Sensor** in the Meshtastic app — <code>TiltModule</code> owns GPIO2; (2) use a **private channel** — module broadcasts are blocked on the default public channel, and both nodes must share a frequency slot.

## Design note

Sensor alerts are broadcast as **LoRa mesh packets** — a short text message such as <code>TAMPER</code>
on the private channel — not sent over the Flipper serial link. They reach the GhostMesh FAP
as ordinary <code>FromRadio</code> PROTO frames, so the FAP's existing decoder handles them, and they
work even when the backpack is deployed away from the operator. See <code>docs/developer-guide.md</code>
and <code>docs/roadmap.md</code>.
