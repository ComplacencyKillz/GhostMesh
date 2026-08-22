# heltec-firmware — Custom Meshtastic Modules

The GhostMesh Heltec "backpack" runs stock **Meshtastic** plus a small number of custom C++
modules for features Meshtastic doesn't provide natively (tamper detection, proximity, jammer
detection, MAX17048 state-of-charge). This directory holds **only the custom module source**
and build notes — not a fork of the full firmware.

## Pinned firmware version

Build against Meshtastic tag **`v2.7.15.567b8ea`** — the version deployed on the project's
nodes. A different tag may build fine but can shift file layout / APIs.

## Build (Ubuntu)

1. Clone the firmware at the pinned tag, with submodules:
   ```bash
   git clone --depth 1 --branch v2.7.15.567b8ea --recurse-submodules --shallow-submodules \
     https://github.com/meshtastic/firmware.git meshtastic-firmware
   ```
2. Copy the module `.cpp/.h` from this directory into `meshtastic-firmware/src/modules/`.
3. Register each module in `src/modules/Modules.cpp` — add an `#include` and a
   `new XxxModule();` line in `setupModules()`.
3b. **Apply the GPS vendor patch** (required for the silent-mode `gpsled` toggle). This is the ONE
   change that lives in the vendored tree, not in this directory, so it must be re-applied on a fresh
   checkout. In `meshtastic-firmware/src/gps/`:
   - `ubx.h` — add `_message_GM_CFG_TP5_DISABLE[32]` (all-zero CFG-TP5 payload → timepulse off).
   - `GPS.h` — declare `public: void setTimepulseEnabled(bool on);`.
   - `GPS.cpp` — implement it (send CFG-TP5 disable over the GPS UART; `on==true` is a no-op).
   See the "GhostMesh vendor patch" comment blocks in each. Without it, `CommandModule` won't link
   (it calls `gps->setTimepulseEnabled`). Best-effort: silences the BN-220 PPS/fix LED; may persist
   on modules whose LED isn't PPS-driven.
4. Build for the Heltec V3:
   ```bash
   pip install platformio           # once; a venv is fine
   cd meshtastic-firmware
   pio run -e heltec-v3
   ```
   Output: `.pio/build/heltec-v3/firmware.factory.bin`.
5. Flash `firmware.factory.bin` to the Heltec (from Windows or wherever your flasher runs).

## Modules

| Module | Purpose | Status |
|--------|---------|--------|
| ArmingModule | slide switch (GPIO4, SPDT) → broadcast `ARMED`/`DISARMED`; sets shared `ghostmesh_armed` | ✅ working |
| TiltModule | SW-520D tilt (GPIO2) → broadcast `TAMPER` (replaces built-in Detection Sensor) | ✅ working |
| LightTamperModule | photoresistor (GPIO5, light) → broadcast `TAMPER_LIGHT` | ✅ working |
| ProximityModule | RCWL-1601 (GPIO38 trig / GPIO47 echo) → broadcast `PERSON_DETECTED` | ✅ working — RCWL-1601 at 3.3V, no divider (HC-SR04 also works but needs 5V + an Echo divider) |
| IRModule | VS1838B (GPIO48) decodes the **GhostMesh NECext set** (addr `0x474D`: `01`ARM `02`DISARM `03`WIPE `04`CONFIRM) → arm/disarm (sets `ghostmesh_armed`, broadcasts) + out-of-band **wipe** via the `ARM→WIPE→CONFIRM` sequence (armed, CONFIRM within 10 s) | ✅ arm/disarm working; wipe sequence new. TX: `flipper-app/GhostMeshBackpack.ir` |
| CommandModule | **Listens** for `/cmd @target [args]` mesh text (per-node id only, **no `@ALL`**) → drives buzzer (GPIO39, passive/tone), vibration (GPIO40), LED, status, arm/disarm, the safety-gated wipe (mesh token + physical double-press on GPIO37), live config (`/set`/`/cfg`), and chunked file upload (`/put` → LittleFS, CRC32-verified; see `CommandModule_payload.cpp`) | ✅ `/buzz`+`/vibrate` confirmed on hardware 2026-08-18 — see `docs/command-cli.md` |

**Armed gate:** `ArmingModule` maintains `volatile bool ghostmesh_armed` (`GhostMeshArming.h`). Tilt/Light/Proximity only broadcast when armed, so the backpack can be handled while DISARMED without spamming the mesh.

**CommandModule is the first *receiving* module.** The others only broadcast; CommandModule overrides `handleReceived()` to parse incoming text. Backpack output pins (verified against the board header photo): buzzer **GPIO39** (passive — driven with a PWM `tone()`, not DC), vibration motor **GPIO40** (on/off), physical wipe button **GPIO37** (`INPUT_PULLUP`), RGB status LED on **GPIO26** (external SK6812, driven via `neopixelWrite` — colors + a green↔red gradient sweep, working on hardware; the onboard GPIO35 LED mirrors its on/off state). Registration is the same as any module (`#include` + `new CommandModule();` in `setupModules()`).

> **Build-time APIs to sanity-check against tag `v2.7.15.567b8ea`** (fix in one line if the layout shifted): `nodeDB->getNodeNum()`, `isFromUs()`/`getFrom()` (NodeDB.h), `powerStatus->getBatteryChargePercent()` (PowerStatus.h), `nodeDB->factoryReset()` + the global `rebootAtMsec` (main.h), and Arduino `tone()`/`noTone()` (fallback: LEDC, noted inline in `CommandModule.cpp`).

**Two hard requirements:** (1) **disable the built-in Detection Sensor** in the Meshtastic app — `TiltModule` owns GPIO2; (2) use a **private channel** — module broadcasts are blocked on the default public channel, and both nodes must share a frequency slot.

## Design note

Sensor alerts are broadcast as **LoRa mesh packets** — a short text message such as `TAMPER`
on the private channel — not sent over the Flipper serial link. They reach the GhostMesh FAP
as ordinary `FromRadio` PROTO frames, so the FAP's existing decoder handles them, and they
work even when the backpack is deployed away from the operator. See `docs/developer-guide.md`
and `docs/roadmap.md`.
