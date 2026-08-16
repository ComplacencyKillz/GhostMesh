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
| _(tamper)_ | tilt / photoresistor → broadcast `TAMPER` mesh packet | in progress |

## Design note

Sensor alerts are broadcast as **LoRa mesh packets** — a short text message such as `TAMPER`
on the private channel — not sent over the Flipper serial link. They reach the GhostMesh FAP
as ordinary `FromRadio` PROTO frames, so the FAP's existing decoder handles them, and they
work even when the backpack is deployed away from the operator. See `docs/developer-guide.md`
and `docs/roadmap.md`.
