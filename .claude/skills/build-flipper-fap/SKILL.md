---
name: build-flipper-fap
description: Build the GhostMesh Flipper Zero application (FAP) from flipper-app/ with ufbt, and deliver ghostmesh.fap to the VM shared folder for installing from Windows. Use whenever the user asks to build/compile the FAP, produce a .fap, or "deliver the fap to the shared folder."
---

# SKILL — Build & Deliver the Flipper FAP

## Purpose

Compile the GhostMesh Flipper Zero app (`flipper-app/`, C99) into `ghostmesh.fap` and drop it in the
VirtualBox shared folder so the user can install it from the Windows host (the Flipper lives on
Windows USB — see the `hardware-flashing-setup` memory). This is the companion to
[build-heltec-firmware](../build-heltec-firmware/SKILL.md); together they cover the whole build chain.
**Building is pure software and happens IN this Linux VM** — only *installing/flashing* needs Windows.

## Fixed locations (this machine)

| Thing | Path |
|-------|------|
| FAP source | `/home/servermonk/repos/ghostmesh/flipper-app/` |
| ufbt (NOT on PATH) | `/home/servermonk/.pio-venv/bin/ufbt` |
| Build output | `flipper-app/dist/ghostmesh.fap` |
| ufbt SDK/toolchain | `~/.ufbt/` |
| Shared-folder dropzone | `/media/sf_my-vm-share/repos/ghostmesh/` (user is in the `vboxsf` group → writable) |

## Steps

1. **Build** (fast — seconds to ~1 min):
   ```bash
   cd /home/servermonk/repos/ghostmesh/flipper-app && /home/servermonk/.pio-venv/bin/ufbt
   ```
   Success ends with `INSTALL … dist/ghostmesh.fap` and an `APPCHK` line reporting `Target: 7, API: 87.1`.

2. **Deliver + verify integrity**:
   ```bash
   SRC=/home/servermonk/repos/ghostmesh/flipper-app/dist/ghostmesh.fap
   DST=/media/sf_my-vm-share/repos/ghostmesh/ghostmesh.fap
   cp "$SRC" "$DST" && sha256sum "$SRC" "$DST"   # the two hashes must match
   ```

3. **Tell the user how to install** (they do it on Windows): copy `ghostmesh.fap` to the Flipper's
   `SD:/apps/Tools/` via qFlipper or an SD reader — then **Apps → Tools → GhostMesh**. (With the
   Flipper on USB and qFlipper closed, `ufbt launch` would build+deploy+run in one step, but that
   needs the board, so it runs on Windows, not here.)

## Gotchas

- **`ufbt` is not on PATH** — always use the full venv path `/home/servermonk/.pio-venv/bin/ufbt`.
- **Content-hash build:** ufbt tracks source *content*, not mtime. If nothing changed it will print
  `INSTALL` but leave `dist/ghostmesh.fap`'s timestamp untouched — that means the existing `.fap` is
  already current, not that the build failed. To force a from-scratch rebuild (e.g. to be 100% sure
  the `.fap` matches committed source): `ufbt -c` (clean) then `ufbt`.
- **Git may make source files look "newer" than the `.fap`** after a checkout/commit even when
  content is identical — don't trust `find -newer`; trust ufbt's own up-to-date decision or force a
  clean build.
- **API/Target compatibility:** the build targets Flipper API `87.1` / hardware target `7`. A Flipper
  on much older/newer firmware may refuse the `.fap` (`APPCHK` mismatch at install). Match the
  Flipper's firmware to the SDK ufbt pulled, or `ufbt update` to move the SDK.
- **The FAP source IS version-controlled** in the ghostmesh repo (`flipper-app/`), unlike the Heltec
  firmware whose Meshtastic checkout is separate. Commit FAP changes here.

## See also

- `build-heltec-firmware` skill — the Heltec `.factory.bin` half of the tooling.
- `CLAUDE.md` → "Build System" — the ufbt command reference.
- Memory: `hardware-flashing-setup` (why installing happens on Windows; all tool paths).
