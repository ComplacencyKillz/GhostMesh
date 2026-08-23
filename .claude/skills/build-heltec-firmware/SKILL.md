---
name: build-heltec-firmware
description: Build a GhostMesh Heltec V3 Meshtastic firmware .bin from the custom modules in heltec-firmware/ and deliver it to the VM shared folder for flashing from Windows. Use whenever the user asks to build/compile the Heltec firmware, produce a .factory.bin, or "deliver a bin to the shared folder."
---

# SKILL — Build & Deliver Heltec Firmware

## Purpose

Build the GhostMesh Heltec "backpack" firmware (stock Meshtastic + the custom modules in
<code>heltec-firmware/</code>) into a flashable <code>.factory.bin</code>, and drop it in the VirtualBox shared folder
so the user can flash it from the Windows host (the boards live on Windows USB — see the
<code>hardware-flashing-setup</code> memory). This is the exact, proven workflow — follow it verbatim.

## Fixed locations (this machine)

| Thing | Path |
|-------|------|
| Meshtastic checkout (pinned) | <code>/home/servermonk/repos/meshtastic-firmware</code> — tag **<code>v2.7.15.567b8ea</code>** |
| PlatformIO (NOT on PATH) | <code>/home/servermonk/.pio-venv/bin/pio</code> |
| Custom module source | <code>/home/servermonk/repos/ghostmesh/heltec-firmware/*.cpp *.h</code> |
| Shared-folder dropzone | <code>/media/sf_my-vm-share/repos/ghostmesh/</code> (user is in the <code>vboxsf</code> group → writable) |
| Bin naming convention | <code>ghostmesh-heltec-v3-<feature>.factory.bin</code> (e.g. <code>-command</code>, <code>-lighttamper</code>) |

## Steps

1. **Confirm the checkout is at the pinned tag** (a wrong tag shifts APIs/layout):
   ```bash
   cd /home/servermonk/repos/meshtastic-firmware && git describe --tags   # → v2.7.15.567b8ea
   ```

2. **Run the setup script** — copies the modules in, registers them in <code>Modules.cpp</code>, and applies the
   GPS timepulse vendor patch. Idempotent (safe to re-run); the one command replaces the old manual
   copy + <code>sed</code>-register + patch dance:
   ```bash
   /home/servermonk/repos/ghostmesh/heltec-firmware/setup.sh   # defaults to ~/repos/meshtastic-firmware
   ```
   - Adding a **new** module? Add its <code>cp</code>'d source (automatic) plus its <code>#include</code> + <code>new XxxModule();</code>
     lines to the <code>INCLUDES</code>/<code>REGISTER</code> arrays in <code>setup.sh</code>'s embedded Python (keep <code>ArmingModule</code>
     before the tampers — it sets <code>ghostmesh_armed</code>, which they read), and re-run.
   - <code>SystemCommandsModule</code> in the checkout is **stock Meshtastic** (keyboard/screen input) — not ours.
   - If the checkout's <code>Modules.cpp</code> is already GhostMesh-registered from an older manual run, revert it
     first (<code>git checkout src/modules/Modules.cpp</code>) so the script's marker-guarded insert is clean.

3. **Pre-flight any new Meshtastic APIs** (cheaper than a failed 6-min build). Grep the checkout to
   confirm the symbols/headers a new module uses actually exist at this tag. Known-good references:
   <code>getFrom</code>/<code>isFromUs</code> → <code>src/mesh/MeshTypes.h</code>; <code>powerStatus->getBatteryChargePercent()</code> →
   <code>src/PowerStatus.h</code>; <code>nodeDB->factoryReset(bool=false)</code> → <code>src/mesh/NodeDB.h</code>; <code>rebootAtMsec</code> →
   <code>src/main.h</code>; <code>tone()</code>/<code>noTone()</code> → Arduino core; <code>esp_random()</code> → <code><esp_random.h></code>.

4. **Build** (~6–7 min incremental off the cache; a clean build is longer):
   ```bash
   cd /home/servermonk/repos/meshtastic-firmware && /home/servermonk/.pio-venv/bin/pio run -e heltec-v3
   ```
   Success ends with <code>[SUCCESS]</code> and writes <code>.pio/build/heltec-v3/firmware.factory.bin</code>.

5. **Deliver + verify integrity**:
   ```bash
   SRC=/home/servermonk/repos/meshtastic-firmware/.pio/build/heltec-v3/firmware.factory.bin
   DST=/media/sf_my-vm-share/repos/ghostmesh/ghostmesh-heltec-v3-<feature>.factory.bin
   cp "$SRC" "$DST" && sha256sum "$SRC" "$DST"   # the two hashes must match
   ```
   **Also refresh the web flasher's hosted copy** so ghostmesh.info/config offers the latest build:
   ```bash
   cp "$SRC" /home/servermonk/repos/ghostmesh/ghostmesh.info/public/firmware/ghostmesh-heltec-v3.factory.bin
   ```
   (Then rebuild + deploy the site — <code>ghostmesh-website-access</code> skill. That bin is committed so the
   deploy always has it; the flasher's "latest" dropdown fetches it from <code>/firmware/</code>.)

6. **Tell the user how to flash** (they do it on Windows — esptool-js web flasher or <code>esptool</code>):
   flash the delivered bin at offset **<code>0x0</code>**, **NO erase** — that preserves channel keys + config
   so they don't have to re-pair. Then hard requirements still apply: **private channel**, and the
   **built-in Detection Sensor disabled** (TiltModule owns GPIO2).

## Gotchas

- **<code>pio</code> is not on PATH** — always use the full venv path <code>/home/servermonk/.pio-venv/bin/pio</code>.
- **Incremental builds can leave a stale <code>firmware.factory.bin</code>.** An incremental <code>pio run</code> may relink
  <code>firmware.elf</code> (new mtime) without regenerating <code>firmware.bin</code> / <code>firmware.factory.bin</code> (old mtime).
  If the <code>.factory.bin</code> timestamp is older than <code>firmware.elf</code>, force a fresh flashable image:
  <code>rm .pio/build/heltec-v3/firmware.bin .pio/build/heltec-v3/firmware.factory.bin && <pio> run -e heltec-v3</code>
  (quick — just the objcopy + esptool merge steps). Confirm <code>firmware.factory.bin</code> is newer than <code>.elf</code>.
- **Flash at <code>0x0</code>, no erase.** <code>firmware.factory.bin</code> is the merged image (bootloader + partitions
  + app). Erasing wipes the user's channel/config.
- **The <code>meshtastic-firmware</code> checkout is a separate repo** — its <code>Modules.cpp</code> change and the copied
  modules are NOT part of the <code>ghostmesh</code> repo and are not committed there. Only the <code>heltec-firmware/</code>
  source is version-controlled (in the ghostmesh repo).
- If the build errors on a Meshtastic symbol, it's almost always a header path or a signature that
  shifted — grep the checkout for the real declaration and fix the one line (see step 4).

## See also

- <code>heltec-firmware/README.md</code> — module list + how to author a new module.
- Memories: <code>hardware-flashing-setup</code> (why we flash from Windows), <code>heltec-custom-firmware-modules</code>,
  <code>command-cli-wipe-safety</code>.
