# PCB Design Review — FlipperZeroModule (GhostMesh backpack shield)

**Board:** `kicad/FlipperZeroModule/FlipperZeroModule` · 67.34 × 41.56 mm, 2-layer, 20 footprints, fully routed
**Reviewed:** 2026-08-22
**Tooling:** `kicad-happy` v2.2.0 analyzers (schematic + PCB + cross-domain + EMC + thermal), vendored under `.claude/skills/`, plus manual net tracing against the raw `.kicad_sch`.

## Verdict

**Not ready to fab.** One functional blocker (the entire operator-output section is unwired), a vestigial/incomplete power MOSFET, and several board-edge placement violations. None are hard to fix, but they must be fixed before ordering — DRC/ERC will *not* catch the big one, because the missing connections aren't nets yet.

> **Verification basis — consistency only.** No `datasheets/` directory and **0 % MPN coverage** (0/19 parts). Every claim below is an internal-consistency check (schematic ↔ PCB ↔ analyzer agree), **not** a datasheet correctness check. Nothing here is "verified against the manufacturer's part." Populate MPNs and sync datasheets before any claim of electrical correctness.

## Blockers (fix before fab)

| # | Severity | Area | Finding |
|---|----------|------|---------|
| 1 | **Blocker** | Schematic | Operator-output section is **unwired**: buzzer/vibration/RGB/wipe (GPIO39/40/26/37) are floating single-pin nets, and the buzzer driver **Q1 (PN2222) has a floating base and collector**. As drawn, the board can drive **none** of the backpack outputs. |
| 2 | **Blocker** | Schematic | **+3V3 rail has no source.** It feeds the buzzer (J10), coin cell (J11) and D1, but the Heltec 3V3-output pins (U1.2/U1.3) are left unconnected — the rail is never fed. |
| 3 | **High** | Schematic | **Q3 (IRLZ44N) switches nothing** — gate floating, drain floating, source→GND. Either delete it or wire it as the intended switched rail (see GPS-power note). |
| 4 | **High** | PCB | **Board-edge placement violations** — J4/J6/J9 at 0.23 mm, J11 at 0.48 mm from the edge; J1/J2 courtyards overhang the edge by 6.73 mm. Confirm intent or move inward. |
| 5 | **High (sourcing)** | BOM | **0 % MPN coverage** — pre-fab/assembly blocker and the reason this review is consistency-only. |

## Schematic findings

### 1. Operator-output section unwired — *blocker*
The tool flags GPIO26 (RGB), GPIO37 (wipe), GPIO39 (buzzer) and GPIO40 (vibration) as single-pin nets (`NT-001`, warning), and `transistor_pin_analysis` shows **Q1 (PN2222A)**: emitter→GND, **base→`__unnamed_12` (floating)**, **collector→`__unnamed_13` (floating)**. So the buzzer's drive path (GPIO39 → 1 kΩ → Q1 base; collector → buzzer) is not drawn, and the same is true for vibration, RGB and the wipe button. The *sensor/input* side (tilt, arming, photo, proximity, IR, GPS UART, I²C, the Flipper link) **is** fully wired — only the output side is missing. Because these are absent nets rather than mis-wired ones, **ERC/DRC pass and "routing complete" reads True** — this is exactly the class of bug that reaches fab silently.

**Fix:** draw the four output nets and complete Q1 (and the vibration driver): GPIO39→1 kΩ→Q1.B, Q1.C→J10 buzzer(−); GPIO40→driver→vibration; GPIO26→SK6812 DIN; GPIO37→wipe tact switch→GND.

### 2. +3V3 has no declared source — *blocker* (`RS-001`)
`+3V3` is consumed by J10 (buzzer +), J11 (coin) and D1, but the Heltec's 3V3 output pins (U1.2/U1.3) are floating single-pin nets — the rail is never actually fed. Tie the Heltec 3V3 output to `+3V3` (and add a `PWR_FLAG`). Combined with finding #1, the buzzer is unconnected on **both** its signal and its supply.

### 3. Q3 IRLZ44N is vestigial / incomplete — *high* (`RS-001` + `NT-001`)
`transistor_pin_analysis` for Q3: **G→floating, D→floating, S→GND.** It switches nothing. The sensor rail `3v3ext` is instead driven directly by the Heltec's `Ve` (Vext) pins 37/38 — the software-gated rail (GPIO36). So either:
- **Delete Q3** if the intent is to gate all sensors via the Heltec's built-in Vext (works, but it's all-or-nothing — see note), **or**
- **Wire Q3 properly** as an independent switched rail (e.g. GPS-only). Note a bare IRLZ44N (N-FET) can't high-side-switch a 3.3 V rail from a 3.3 V gate — that needs a P-FET high-side switch with a gate driver.

### 4. Sensor rail is on Vext — design note (not a bug)
Confirmed by net trace: **every sensor VCC** (GPS J7, BME280 J5, MAX17048 J6, ultrasonic J3, IR J4, arming J9, tilt J8, photo pull-up R2) sits on `3v3ext` = Heltec `Ve`/Vext (GPIO36-gated). Good news: software **can** cut the GPS (LED + ~40 mA) by dropping Vext. Caveat: it's **all-or-nothing** — Vext also carries every other sensor and the onboard OLED, so you can't kill the GPS alone (that's what a correctly-wired Q3 would buy). Firmware must **own GPIO36** so a screen-blank doesn't brown out the sensors.

> **Docs mismatch:** `docs/wiring.md` / `docs/hardware.md` say the GPS and sensors are on the always-on `3V3` rail — that describes the breadboard rig. This PCB puts them on Vext (`3v3ext`). Update the docs once the board is the build target.

### 5. `3v3ext` no declared source (`RS-001`, warning)
The Ve pins are power outputs but aren't flagged as such — add a `PWR_FLAG` on `3v3ext` to silence ERC and document the source.

## PCB findings

### Placement / board edge (`PM-002`) — *high*
- **Errors:** J4, J6, J9 at **0.23 mm**; J11 at **0.48 mm**; R1 courtyard over edge by 0.1 mm.
- **J1 & J2 (Flipper headers): courtyards overhang the edge by 6.73 mm.** If these are the Flipper-mating headers meant to extend past the board, that's expected — **confirm**. Otherwise it's a placement error.
- **Warnings:** J8 0.73 mm, J10 0.98 mm, R2 0.5 mm from edge.

Tighten edge clearance (fab minimum is typically ≥0.5 mm to copper/courtyard) or confirm the overhangs are intentional edge-mate connectors.

### Ground / return path (`GP-001`, `VS-002`, `BE-002`) — *low risk for this board*
EMC flags 12 major + 6 partial reference-plane gaps, no ground-stitching vias, and incomplete edge pour. On a 2-layer board these are real, **but** every signal here is low-speed — UART @115200, I²C ≤400 kHz, GPIO, DC power; the SX1262 RF is internal to the Heltec module and never crosses this PCB. So functional and emissions risk is low. **Recommended, not blocking:** add ground-stitching vias and extend the ground pour to the edges — cheap insurance and better practice.

### DFM / manufacturing — scope-dependent
- **No test points** (`TE-001`, 0/58 nets) — acceptable for a hand-built lab board; add a few (3V3, GND, Flipper TX/RX) if you want bring-up probing.
- **No ESD on headers** (`EP-AUD`, info) — acceptable for this scope; the external-facing lines (Flipper link, GPS, IR) are the ones to protect if you ever want field ruggedness.

## Cross-domain / EMC / Thermal / SPICE
- **Cross-domain:** 1 finding — `VS-002` no ground stitching vias (same as above).
- **EMC:** trust level *low* (all findings are the low-speed plane-gap items above; no high-speed content on the board).
- **Thermal:** **skipped — 0 dissipating parts.** Correct: no regulators/power devices on this shield; the Heltec module and drivers dissipate negligibly.
- **SPICE:** not run — no simulator on PATH, and there are no analog subcircuits to simulate.

## Triaged as false positives (no action)
- **`DC-002` "no decoupling near U1"** — U1 is the Heltec **module**, not a bare IC; it carries its own onboard decoupling. Not applicable.
- **`PU-001` "RST missing pull-up"** — U1's reset is handled by the module's onboard reset circuitry. Not applicable.
- **`CP-002` "no opposite-layer copper"** (15×, info) — expected 2-layer artifact.

## Not performed / limits
- **Datasheet verification** — no `datasheets/`, 0 MPNs → consistency-only (see banner). To upgrade to a correctness review: populate MPNs, then `sync_datasheets_*` (digikey/lcsc/element14) and re-run.
- **Lifecycle/obsolescence** — skipped (no MPNs / no network keys).
- **Gerber checks** — no fabrication outputs present.
- **Prior-review delta** — none; this is the first run.

## Fix list for the EE (ordered)
1. **Wire the output section:** GPIO39→1 kΩ→Q1.B, Q1.C→buzzer; complete the vibration driver; GPIO26→SK6812 DIN; GPIO37→wipe switch→GND. (Blocker #1)
2. **Feed +3V3:** tie the Heltec 3V3 output (U1.2/U1.3) to the `+3V3` rail; add a `PWR_FLAG`. (Blocker #2)
3. **Decide Q3:** delete it, or wire it as a real (P-FET high-side) switch if you want GPS-independent power control. (High #3)
4. **Fix edge clearances:** move J4/J6/J9/J11 (and R1/R2) off the edge, or confirm J1/J2 are intentional edge-mate headers. (High #4)
5. **Add `PWR_FLAG` on `3v3ext`** to document the Vext source and clear ERC. (Warning)
6. *(Recommended)* ground-stitching vias + edge pour; a few test points. (Low risk)
7. **Before ordering:** populate MPNs on all 19 parts so the board is sourceable and a real datasheet-backed review can run.

---
*Regenerate:* `python3 .claude/skills/kicad/scripts/analyze_schematic.py kicad/FlipperZeroModule/FlipperZeroModule.kicad_sch --analysis-dir analysis/` (+ `analyze_pcb.py --full`, `cross_analysis.py`, `emc/scripts/analyze_emc.py`). Analyzer JSON lands in `analysis/` (gitignored; not committed).
