---
name: build-heltec-firmware
description: Build a GhostMesh Heltec V3 Meshtastic firmware .bin from the custom modules in heltec-firmware/ and deliver it to the VM shared folder for flashing from Windows. Use whenever the user asks to build/compile the Heltec firmware, produce a .factory.bin, or "deliver a bin to the shared folder."
---

# SKILL — Build & Deliver Heltec Firmware

## Purpose

Build the GhostMesh Heltec "backpack" firmware (stock Meshtastic + the custom modules in
`heltec-firmware/`) into a flashable `.factory.bin`, and drop it in the VirtualBox shared folder
so the user can flash it from the Windows host (the boards live on Windows USB — see the
`hardware-flashing-setup` memory). This is the exact, proven workflow — follow it verbatim.

## Fixed locations (this machine)

| Thing | Path |
|-------|------|
| Meshtastic checkout (pinned) | `/home/servermonk/repos/meshtastic-firmware` — tag **`v2.7.15.567b8ea`** |
| PlatformIO (NOT on PATH) | `/home/servermonk/.pio-venv/bin/pio` |
| Custom module source | `/home/servermonk/repos/ghostmesh/heltec-firmware/*.cpp *.h` |
| Shared-folder dropzone | `/media/sf_my-vm-share/repos/ghostmesh/` (user is in the `vboxsf` group → writable) |
| Bin naming convention | `ghostmesh-heltec-v3-<feature>.factory.bin` (e.g. `-command`, `-lighttamper`) |

## Steps

1. **Confirm the checkout is at the pinned tag** (a wrong tag shifts APIs/layout):
   ```bash
   cd /home/servermonk/repos/meshtastic-firmware && git describe --tags   # → v2.7.15.567b8ea
   ```

2. **Copy the custom modules in** (idempotent — refreshes all, adds any new one):
   ```bash
   cp /home/servermonk/repos/ghostmesh/heltec-firmware/*.cpp \
      /home/servermonk/repos/ghostmesh/heltec-firmware/*.h \
      /home/servermonk/repos/meshtastic-firmware/src/modules/
   ```

3. **Register any NEW module in `src/modules/Modules.cpp`** (existing ones are already wired).
   Two edits, guarded so re-runs don't duplicate:
   ```bash
   cd /home/servermonk/repos/meshtastic-firmware
   grep -q 'modules/XxxModule.h' src/modules/Modules.cpp || \
     sed -i 's|#include "modules/IRModule.h"|#include "modules/IRModule.h"\n#include "modules/XxxModule.h"|' src/modules/Modules.cpp
   grep -q 'new XxxModule()' src/modules/Modules.cpp || \
     sed -i 's|    irModule = new IRModule();|    irModule = new IRModule();\n    xxxModule = new XxxModule();|' src/modules/Modules.cpp
   ```
   - The GhostMesh `#include`s sit in a block right after `DetectionSensorModule.h`.
   - The `new XxxModule();` calls sit together after `irModule = new IRModule();`, **before** the
     `#if !MESHTASTIC_EXCLUDE_ATAK` block. **Register `ArmingModule` before the tamper modules**
     (it sets `ghostmesh_armed`, which they read).
   - `SystemCommandsModule` in the checkout is **stock Meshtastic** (keyboard/screen input) — not
     ours, don't touch it.

4. **Pre-flight any new Meshtastic APIs** (cheaper than a failed 6-min build). Grep the checkout to
   confirm the symbols/headers a new module uses actually exist at this tag. Known-good references:
   `getFrom`/`isFromUs` → `src/mesh/MeshTypes.h`; `powerStatus->getBatteryChargePercent()` →
   `src/PowerStatus.h`; `nodeDB->factoryReset(bool=false)` → `src/mesh/NodeDB.h`; `rebootAtMsec` →
   `src/main.h`; `tone()`/`noTone()` → Arduino core; `esp_random()` → `<esp_random.h>`.

5. **Build** (~6–7 min incremental off the cache; a clean build is longer):
   ```bash
   cd /home/servermonk/repos/meshtastic-firmware && /home/servermonk/.pio-venv/bin/pio run -e heltec-v3
   ```
   Success ends with `[SUCCESS]` and writes `.pio/build/heltec-v3/firmware.factory.bin`.

6. **Deliver + verify integrity**:
   ```bash
   SRC=/home/servermonk/repos/meshtastic-firmware/.pio/build/heltec-v3/firmware.factory.bin
   DST=/media/sf_my-vm-share/repos/ghostmesh/ghostmesh-heltec-v3-<feature>.factory.bin
   cp "$SRC" "$DST" && sha256sum "$SRC" "$DST"   # the two hashes must match
   ```

7. **Tell the user how to flash** (they do it on Windows — esptool-js web flasher or `esptool`):
   flash the delivered bin at offset **`0x0`**, **NO erase** — that preserves channel keys + config
   so they don't have to re-pair. Then hard requirements still apply: **private channel**, and the
   **built-in Detection Sensor disabled** (TiltModule owns GPIO2).

## Gotchas

- **`pio` is not on PATH** — always use the full venv path `/home/servermonk/.pio-venv/bin/pio`.
- **Flash at `0x0`, no erase.** `firmware.factory.bin` is the merged image (bootloader + partitions
  + app). Erasing wipes the user's channel/config.
- **The `meshtastic-firmware` checkout is a separate repo** — its `Modules.cpp` change and the copied
  modules are NOT part of the `ghostmesh` repo and are not committed there. Only the `heltec-firmware/`
  source is version-controlled (in the ghostmesh repo).
- If the build errors on a Meshtastic symbol, it's almost always a header path or a signature that
  shifted — grep the checkout for the real declaration and fix the one line (see step 4).

## See also

- `heltec-firmware/README.md` — module list + how to author a new module.
- Memories: `hardware-flashing-setup` (why we flash from Windows), `heltec-custom-firmware-modules`,
  `command-cli-wipe-safety`.
