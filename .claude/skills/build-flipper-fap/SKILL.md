---
name: build-flipper-fap
description: Build the GhostMesh Flipper Zero application (FAP) from flipper-app/ with ufbt, and deliver ghostmesh.fap to the VM shared folder for installing from Windows. Use whenever the user asks to build/compile the FAP, produce a .fap, or "deliver the fap to the shared folder."
---

# SKILL — Build & Deliver the Flipper FAP

## Purpose

Compile the GhostMesh Flipper Zero app (<code>flipper-app/</code>, C99) into <code>ghostmesh.fap</code> and drop it in the
VirtualBox shared folder so the user can install it from the Windows host (the Flipper lives on
Windows USB — see the <code>hardware-flashing-setup</code> memory). This is the companion to
[build-heltec-firmware](../build-heltec-firmware/SKILL.md); together they cover the whole build chain.
**Building is pure software and happens IN this Linux VM** — only *installing/flashing* needs Windows.

## Fixed locations (this machine)

| Thing | Path |
|-------|------|
| FAP source | <code>/home/servermonk/repos/ghostmesh/flipper-app/</code> |
| ufbt (NOT on PATH) | <code>/home/servermonk/.pio-venv/bin/ufbt</code> |
| Build output | <code>flipper-app/dist/ghostmesh.fap</code> |
| ufbt SDK/toolchain | <code>~/.ufbt/</code> |
| Shared-folder dropzone | <code>/media/sf_my-vm-share/repos/ghostmesh/</code> (user is in the <code>vboxsf</code> group → writable) |

## Steps

1. **Build** (fast — seconds to ~1 min):
   ```bash
   cd /home/servermonk/repos/ghostmesh/flipper-app && /home/servermonk/.pio-venv/bin/ufbt
<pre><code>
   Success ends with <code>INSTALL … dist/ghostmesh.fap</code> and an <code>APPCHK</code> line reporting <code>Target: 7, API: 87.1</code>.

2. **Deliver + verify integrity**:
   ```bash
   SRC=/home/servermonk/repos/ghostmesh/flipper-app/dist/ghostmesh.fap
   DST=/media/sf_my-vm-share/repos/ghostmesh/ghostmesh.fap
   cp "$SRC" "$DST" && sha256sum "$SRC" "$DST"   # the two hashes must match
</code></pre>

3. **Tell the user how to install** (they do it on Windows): copy <code>ghostmesh.fap</code> to the Flipper's
   <code>SD:/apps/Tools/</code> via qFlipper or an SD reader — then **Apps → Tools → GhostMesh**. (With the
   Flipper on USB and qFlipper closed, <code>ufbt launch</code> would build+deploy+run in one step, but that
   needs the board, so it runs on Windows, not here.)

## Gotchas

- **<code>ufbt</code> is not on PATH** — always use the full venv path <code>/home/servermonk/.pio-venv/bin/ufbt</code>.
- **Content-hash build:** ufbt tracks source *content*, not mtime. If nothing changed it will print
  <code>INSTALL</code> but leave <code>dist/ghostmesh.fap</code>'s timestamp untouched — that means the existing <code>.fap</code> is
  already current, not that the build failed. To force a from-scratch rebuild (e.g. to be 100% sure
  the <code>.fap</code> matches committed source): <code>ufbt -c</code> (clean) then <code>ufbt</code>.
- **Git may make source files look "newer" than the <code>.fap</code>** after a checkout/commit even when
  content is identical — don't trust <code>find -newer</code>; trust ufbt's own up-to-date decision or force a
  clean build.
- **API/Target compatibility:** the build targets Flipper API <code>87.1</code> / hardware target <code>7</code>. A Flipper
  on much older/newer firmware may refuse the <code>.fap</code> (<code>APPCHK</code> mismatch at install). Match the
  Flipper's firmware to the SDK ufbt pulled, or <code>ufbt update</code> to move the SDK.
- **The FAP source IS version-controlled** in the ghostmesh repo (<code>flipper-app/</code>), unlike the Heltec
  firmware whose Meshtastic checkout is separate. Commit FAP changes here.

## See also

- <code>build-heltec-firmware</code> skill — the Heltec <code>.factory.bin</code> half of the tooling.
- <code>CLAUDE.md</code> → "Build System" — the ufbt command reference.
- Memory: <code>hardware-flashing-setup</code> (why installing happens on Windows; all tool paths).
