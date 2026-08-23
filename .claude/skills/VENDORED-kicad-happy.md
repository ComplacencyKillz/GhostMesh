---
---
# Vendored skills — kicad-happy

The skills `bom, datasheets, digikey, element14, emc, jlcpcb, kicad, lcsc, mouser,
pcbway, spice` are vendored from **aklofas/kicad-happy** (MIT).

- Source: https://github.com/aklofas/kicad-happy
- Version: 2.2.0  (commit 43dad23)
- Imported: 2026-08-22
- License: MIT — see <code>LICENSE.kicad-happy</code>

Pure-Python stdlib; no <code>pip install</code> needed. The <code>kicad</code> analyzer has its own
s-expression parser and does **not** require <code>kicad-cli</code>.

Readiness in this project:
- Ready now (local, no keys): <code>kicad</code>, <code>emc</code>, <code>datasheets</code>, <code>bom</code>, <code>jlcpcb</code>, <code>pcbway</code>, <code>lcsc</code> (free community API)
- Need an API key: <code>digikey</code> (OAuth), <code>mouser</code>, <code>element14</code> (free key)
- Needs a simulator on PATH: <code>spice</code> (ngspice / LTspice / Xyce)

To update: re-pull ~/repos/kicad-happy and re-run the copy.
