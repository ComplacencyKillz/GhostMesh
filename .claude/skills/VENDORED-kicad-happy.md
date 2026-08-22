# Vendored skills — kicad-happy

The skills `bom, datasheets, digikey, element14, emc, jlcpcb, kicad, lcsc, mouser,
pcbway, spice` are vendored from **aklofas/kicad-happy** (MIT).

- Source: https://github.com/aklofas/kicad-happy
- Version: 2.2.0  (commit 43dad23)
- Imported: 2026-08-22
- License: MIT — see `LICENSE.kicad-happy`

Pure-Python stdlib; no `pip install` needed. The `kicad` analyzer has its own
s-expression parser and does **not** require `kicad-cli`.

Readiness in this project:
- Ready now (local, no keys): `kicad`, `emc`, `datasheets`, `bom`, `jlcpcb`, `pcbway`, `lcsc` (free community API)
- Need an API key: `digikey` (OAuth), `mouser`, `element14` (free key)
- Needs a simulator on PATH: `spice` (ngspice / LTspice / Xyce)

To update: re-pull ~/repos/kicad-happy and re-run the copy.
