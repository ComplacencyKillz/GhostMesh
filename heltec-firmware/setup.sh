#!/usr/bin/env bash
# GhostMesh firmware setup — drop our custom modules into a stock Meshtastic checkout, register them
# in Modules.cpp, and apply the GPS timepulse vendor patch. Idempotent: safe to re-run. After this,
# build with:  cd <checkout> && ~/.pio-venv/bin/pio run -e heltec-v3
#
# Usage:  heltec-firmware/setup.sh [path-to-meshtastic-firmware]   (default: ~/repos/meshtastic-firmware)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"        # this directory (heltec-firmware/)
MESH="${1:-$HOME/repos/meshtastic-firmware}" # target Meshtastic checkout
PINNED="v2.7.15.567b8ea"                     # the pinned, tested Meshtastic tag
MARK="GhostMesh custom modules (added by"    # idempotency marker in Modules.cpp

[ -d "$MESH/src/modules" ] || { echo "ERROR: '$MESH' is not a Meshtastic checkout (no src/modules)."; exit 1; }

# 1. Tag sanity (warn only — building off a different tag risks API drift)
tag="$(git -C "$MESH" describe --tags 2>/dev/null || echo unknown)"
if [ "$tag" != "$PINNED" ]; then
    echo "WARN: checkout is at '$tag', not the pinned '$PINNED' — Meshtastic APIs may have drifted."
fi

# 2. Copy our module sources in
cp "$HERE"/*.cpp "$HERE"/*.h "$MESH/src/modules/"
echo "OK  copied $(ls "$HERE"/*.cpp "$HERE"/*.h | wc -l) module files -> src/modules/"

# 3. Apply the GPS timepulse vendor patch (touches src/gps/, outside src/modules/) — idempotent
if git -C "$MESH" apply --reverse --check "$HERE/gps-timepulse.patch" >/dev/null 2>&1; then
    echo "OK  GPS timepulse patch already applied"
elif git -C "$MESH" apply --check "$HERE/gps-timepulse.patch" >/dev/null 2>&1; then
    git -C "$MESH" apply "$HERE/gps-timepulse.patch"
    echo "OK  applied gps-timepulse.patch"
else
    echo "WARN: gps-timepulse.patch neither applies nor is already applied — re-create it for '$tag'"
fi

# 4. Register the modules in Modules.cpp (idempotent — guarded by the marker)
MOD="$MESH/src/modules/Modules.cpp"
if grep -q "$MARK" "$MOD"; then
    echo "OK  modules already registered in Modules.cpp"
else
    python3 - "$MOD" <<'PY'
import sys
path = sys.argv[1]
lines = open(path).read().splitlines(keepends=True)

INCLUDES = [
    '// ── GhostMesh custom modules (added by heltec-firmware/setup.sh) ──\n',
    '#include "modules/LightTamperModule.h"\n',
    '#include "modules/ProximityModule.h"\n',
    '#include "modules/ArmingModule.h"\n',
    '#include "modules/TiltModule.h"\n',
    '#include "modules/IRModule.h"\n',
    '#include "modules/CommandModule.h"\n',
    '// ── end GhostMesh modules ──\n',
]
REGISTER = [
    '    // ── GhostMesh custom modules (added by heltec-firmware/setup.sh) ──\n',
    '    // ArmingModule registers before the tampers — it sets ghostmesh_armed, which they read.\n',
    '    lightTamperModule = new LightTamperModule();\n',
    '    proximityModule = new ProximityModule();\n',
    '    armingModule = new ArmingModule();\n',
    '    tiltModule = new TiltModule();\n',
    '    irModule = new IRModule();\n',
    '    commandModule = new CommandModule();\n',
    '    // ── end GhostMesh modules ──\n',
]

# Includes: right after the unconditional `#include "configuration.h"`. (Do NOT anchor on the last
# `#include "modules/..."` — that one is DropzoneModule.h, inside a `#if !MESHTASTIC_EXCLUDE_DROPZONE`
# guard, so our headers would be conditionally compiled out and the registration wouldn't link.)
inc_at = next(i for i, l in enumerate(lines) if l.startswith('#include "configuration.h"')) + 1

# Registration: inside setupModules(), right after the #endif that closes the DetectionSensor block
# (a stable, unambiguous anchor — unlike MESHTASTIC_EXCLUDE_ATAK, which also appears near the top).
ds = next(i for i, l in enumerate(lines) if 'new DetectionSensorModule()' in l)
reg_at = next(i for i in range(ds, len(lines)) if lines[i].strip() == '#endif') + 1

# Splice the higher index first so the lower index stays valid.
if reg_at >= inc_at:
    lines[reg_at:reg_at] = REGISTER
    lines[inc_at:inc_at] = INCLUDES
else:
    lines[inc_at:inc_at] = INCLUDES
    lines[reg_at:reg_at] = REGISTER
open(path, 'w').write(''.join(lines))
print("OK  registered 6 modules in Modules.cpp")
PY
fi

echo "Done. Build: cd \"$MESH\" && ~/.pio-venv/bin/pio run -e heltec-v3"
