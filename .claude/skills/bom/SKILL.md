---
name: bom
description: BOM (Bill of Materials) management for electronics projects — the workflow skill that coordinates DigiKey, Mouser, LCSC, element14, JLCPCB, PCBWay, and KiCad skills around a unified BOM lifecycle. Create, update, and maintain BOMs with part numbers, costs, quantities stored as KiCad symbol properties. ALWAYS trigger this skill for any task involving component sourcing, pricing, ordering, distributor searches, BOM export, or fabrication preparation — even if the user names a specific distributor or fab house (e.g. "search DigiKey for...", "generate JLCPCB BOM", "order from Mouser"). This skill analyzes the schematic for sourcing gaps, recommends which distributor/fab skills to call for each gap, and writes results back as symbol properties — the agent (or user) performs the actual searches via the called skills. Also trigger on phrases like "what parts do I need", "order components", "how much will this cost", "export for JLCPCB", "find parts for this board", "compare pricing", or "check stock".
---

# BOM Management

BOM data lives in **KiCad schematic symbol properties** as the single source of truth. This skill orchestrates the full lifecycle: analyze the schematic, search distributors, validate parts, write properties back, export tracking CSVs, and generate order files.

## Related Skills

| Skill | Purpose |
|-------|---------|
| <code>kicad</code> | Read/analyze schematics, PCB, footprints |
| <code>digikey</code> | Search DigiKey, download datasheets (primary prototype source) |
| <code>mouser</code> | Search Mouser (secondary prototype source) |
| <code>lcsc</code> | Search LCSC (production/JLCPCB parts) |
| <code>element14</code> | Search Newark/Farnell/element14 (international) |
| <code>jlcpcb</code> | PCB fabrication & assembly ordering |
| <code>pcbway</code> | Alternative PCB fab & assembly |

## Scripts

Use <code><skill-path></code> to reference the BOM skill directory.

```bash
# Analyze schematic (JSON output, recursive sub-sheets)
python3 <skill-path>/scripts/bom_manager.py analyze path/to/schematic.kicad_sch --json --recursive

# Export BOM tracking CSV (creates new or merges with existing)
python3 <skill-path>/scripts/bom_manager.py export path/to/schematic.kicad_sch -o bom/bom.csv --recursive

# Generate per-distributor order files (5 boards + 2 spares/line)
python3 <skill-path>/scripts/bom_manager.py order bom/bom.csv --boards 5 --spares 2

# Quick single-distributor order (bypasses Chosen_Distributor column)
python3 <skill-path>/scripts/bom_manager.py order bom/bom.csv --distributor digikey

# Write properties to schematic (dry-run first, then apply)
echo '{"R1": {"MPN": "RC0805FR-0710KL", "Manufacturer": "Yageo"}}' \
  | python3 <skill-path>/scripts/edit_properties.py path/to/schematic.kicad_sch --dry-run

# Sync datasheet URLs from manifest.json back into schematic Datasheet properties
python3 <skill-path>/scripts/sync_datasheet_urls.py path/to/schematic.kicad_sch --recursive --dry-run

# Translate KiCad/Altium BOM and CPL files into JLCPCB upload format
# (<code>pnp --bom</code> filter drops orphan designators — see skills/jlcpcb/SKILL.md
# for the 3-step PCBA upload workflow)
python3 <skill-path>/scripts/translate_bom_pnp.py bom input_bom.csv -o jlc_bom.csv
python3 <skill-path>/scripts/translate_bom_pnp.py pnp input_cpl.csv -o jlc_cpl.csv --bom jlc_bom.csv
```

## Workflow

Skip steps that don't apply. Common shortcuts:
- **"Add Mouser PNs"** — search Mouser by MPN for each part → validate → write to schematic → update CSV
- **"Fill in the gaps"** — run analyzer with <code>--gaps-only</code>, address each missing field
- **"Update datasheet URLs"** — run <code>sync_datasheet_urls.py</code> to backfill empty Datasheet fields from the datasheets manifest
- **"Prepare for production"** — ensure every part has an LCSC number, check stock, set Chosen_Distributor to LCSC

### Step 1: Understand the Project

```bash
python3 <skill-path>/scripts/bom_manager.py analyze path/to/schematic.kicad_sch --json --recursive
```

The output tells you the project's field naming convention, which distributors are populated, what's missing, and the preferred distributor. Also look for an existing BOM tracking CSV in the project directory or <code>bom/</code> folder.

The script covers common patterns, but some projects use internal key systems or parametric fields. See <code>references/part-number-conventions.md</code> for the full catalog. Read the schematic if something seems off.

### Step 2: Sync Datasheets

**Do this immediately.** Datasheets are essential context for validation and part selection. Run the preferred distributor's sync first; if some fail, try others — they share the same <code>datasheets/</code> directory and skip already-downloaded parts.

```bash
python3 <digikey-skill-path>/scripts/sync_datasheets_digikey.py path/to/schematic.kicad_sch --recursive
python3 <lcsc-skill-path>/scripts/sync_datasheets_lcsc.py path/to/schematic.kicad_sch --recursive
python3 <element14-skill-path>/scripts/sync_datasheets_element14.py path/to/schematic.kicad_sch --recursive
```

DigiKey is best (direct PDF URLs). element14 is reliable (no bot protection). LCSC works for LCSC-only parts. Mouser is a last resort (often blocks downloads).

**Tell the user where datasheets are** (e.g., <code>hardware/<project>/datasheets/</code>). They'll reference them often.

**Cross-revision projects:** Use a single shared datasheets directory at the project level rather than per-revision. The same MPN's datasheet doesn't change between revisions.

Re-sync after writing new MPNs (Step 5) — the scripts are idempotent. Then backfill Datasheet URLs into the schematic:

```bash
python3 <skill-path>/scripts/sync_datasheet_urls.py path/to/schematic.kicad_sch --recursive
```

This reads <code>datasheets/manifest.json</code> (legacy name <code>index.json</code> still supported) and writes discovered datasheet URLs into empty schematic <code>Datasheet</code> properties. Opportunistic — only fills blanks. If a schematic already has a different URL, it warns about the mismatch without overwriting (use <code>--overwrite</code> to replace). Run with <code>--dry-run</code> first to preview.

### Step 3: Gather Part Information

**Watch for comma-separated MPNs.** Some symbols track multiple physical parts (e.g., battery holder + clip). Split on commas and search each MPN independently — searching the combined string matches the wrong product.

Search strategy based on what's available:
- Has MPN → search distributors by MPN to get their PNs and stock
- Has distributor PN but no MPN → search that distributor, get MPN, then search others
- Has only Value + Footprint → search by description (e.g., "100nF 0402 X7R 16V")

Use the project's preferred distributor first, then alternates. Prototype: DigiKey primary, Mouser secondary. Production: LCSC.

### Step 4: Validate Matches

**Don't assume existing PNs are correct** — distributor PNs go stale (discontinued, renumbered). Verify existing PNs resolve against the API. If a PN returns 404, flag it for replacement.

For every match, verify:
1. **Package** matches the schematic footprint (see cross-reference table below)
2. **Specs** match (capacitance, resistance, voltage, tolerance)
3. **Description** makes sense (a resistor ref should get a resistor)
4. **Lifecycle** — not obsolete or EOL
5. **Datasheet URL** is a direct PDF link (not a product page)

If ambiguous, ask the user. A wrong part is worse than a missing part.

### Step 5: Update the Schematic

**KiCad coexistence.** The script detects KiCad's lock file and warns but proceeds. KiCad doesn't auto-detect external changes — it keeps its in-memory copy. If KiCad is open, tell the user: *"Close and reopen the schematic (File → Open Recent) to see the changes. Don't save from KiCad first."*

If unsaved KiCad work exists, ask them to save first (Ctrl+S), then run the script, then reopen.

```bash
echo '{"R1": {"MPN": "RC0805FR-0710KL", "Manufacturer": "Yageo", "DigiKey": "311-10.0KCRCT-ND"}}' \
  | python3 <skill-path>/scripts/edit_properties.py path/to/schematic.kicad_sch
```

**Backups:** By default, no <code>.bak</code> file is created (git tracks changes). Pass <code>--backup</code> if the schematic is not in a git repo or has uncommitted changes the user wants to preserve.

**Respect the project's convention.** Write to <code>"Digi-Key_PN"</code> if that's what exists, not <code>"DigiKey"</code>. Use canonical names only for new projects.

**Always write Manufacturer alongside MPN** — every API returns it, it's free data.

### Step 6: Update the BOM Tracking CSV

```bash
python3 <skill-path>/scripts/bom_manager.py export path/to/schematic.kicad_sch -o bom/bom.csv --recursive
```

CSV columns are dynamic — only distributors the project uses get columns. Base columns: Reference, Qty, Value, Footprint, MPN, Manufacturer. Each active distributor gets a PN column + stock column. Tail columns: Chosen_Distributor, Datasheet, Validated, DNP, Notes.

The **Notes** column is seeded from schematic <code>BOM Comments</code> properties (or aliases like <code>Notes</code>, <code>Remarks</code>, <code>Ordering Notes</code>, etc.) on first export. On re-export, user edits in the CSV take priority — existing Notes values are preserved and schematic-sourced comments won't overwrite them.

**Merge behavior:** Re-exporting preserves user-managed columns (stock, Chosen_Distributor, Validated, Notes) while updating schematic-derived columns.

### Step 7: Check Stock

For each part with a distributor PN, query current stock via the corresponding distributor skill. Update stock columns in the CSV. Stock data goes stale — note the date and re-check before ordering.

If the chosen distributor is out of stock, flag it and suggest the alternate.

### Step 8: Set Chosen Distributor

Factors: stock availability, price at order qty, minimum order/multiples, lead time, shipping consolidation (fewer distributors = fewer shipments).

For prototypes, consolidate to 1-2 distributors (DigiKey + Mouser). For production, LCSC/JLCPCB is cheapest.

### Step 9: Re-Sync Datasheets & URLs

Re-run Step 2 (download + URL backfill) to pick up parts added in Steps 3-5. Fast — already-downloaded files are skipped.

### Step 10: Validate Datasheets Against Design

Read downloaded datasheets and verify parts are functionally correct for the circuit. This catches wrong-part-number errors that Step 4 might miss.

**What to check by type:**
- **Passives** — voltage rating vs rail voltage, temperature coefficient, power dissipation
- **Regulators** — Vin range, Vout, max current, quiescent current
- **MCUs/ICs** — supply voltage, I/O levels, peripherals, pinout
- **Connectors** — pin count, pitch, current/voltage rating
- **MOSFETs** — Vds, Rds(on), gate threshold, thermal dissipation
- **Diodes** — Vf, Vr, current rating, recovery time

For large BOMs (50+ parts), focus on power components, critical signal paths, and anything the user flagged. Commodity passives usually don't need deep review.

### Step 11: Generate Order Files

**Ask how many boards** if not already known — this sets the <code>--boards</code> multiplier.

**Pre-flight:** verify no gaps, CSV is current, Chosen_Distributor is set (or use <code>--distributor</code> flag), stock is fresh.

```bash
# Using Chosen_Distributor column, 5 boards + 2 spares
python3 <skill-path>/scripts/bom_manager.py order bom/bom.csv -o bom/orders/ --boards 5 --spares 2

# Or quick single-distributor order
python3 <skill-path>/scripts/bom_manager.py order bom/bom.csv --distributor digikey
```

<code>--boards</code> multiplies all quantities. <code>--spares</code> adds a flat extra per line after multiplication. <code>--distributor</code> bypasses Chosen_Distributor — generates an order for all parts with that distributor's PN.

Comma-separated PNs (accessories) are auto-split into separate order lines. DNP parts excluded. The script produces one file per distributor in the correct upload format (see <code>references/ordering-and-fabrication.md</code> for format details).

Present the order summary and let the user review/edit before ordering.

**Cost estimate:** After generating order files, query pricing from distributor APIs at the order quantity and present a total per distributor. See <code>references/ordering-and-fabrication.md</code> for the cost summary template.

## BOM Corner Cases & Per-Component Notes

Real projects have BOM quirks that don't fit neatly into standard fields. These are the things that get lost between design and ordering — a connector that's only for prototyping, a cable shared between two boards, a part that needs to be ordered from a specific vendor lot. **Actively look for these** during BOM analysis; don't wait for the user to mention them.

### BOM Comments Field

The <code>BOM Comments</code> symbol property (canonical name) captures per-component freeform notes. It flows into the <code>Notes</code> column in the exported CSV. The script recognizes many aliases: <code>BOM Notes</code>, <code>Ordering Notes</code>, <code>Assembly Notes</code>, <code>Notes</code>, <code>Remarks</code>, <code>Comment</code>, and underscore/space variants.

**When to suggest adding BOM Comments:**
- Component is prototype-only (DNP in production, or vice versa)
- Component has ordering constraints (minimum order qty, long lead time, specific vendor lot)
- Component is shared with another board (ribbon cables, mating connectors, shared harnesses)
- Component has assembly notes (orientation matters, hand-solder only, apply after reflow)
- Component has substitution rules (acceptable alternates, pin-compatible swaps)
- Component has conditional population (different value for different product variants/SKUs)

**Example values:**
```
"Proto only — DNP in production"
"Shares ribbon cable with power board — don't double-order"
"Must be Murata GRM series, no substitution (validated for EMI)"
"Hand-solder after reflow — temperature sensitive"
"Order 10% extra — fragile QFN rework difficult"
"Use 10K for rev A, 4.7K for rev B"
"Mating connector: Molex 39-01-2040 on cable side"
```

### Where Else to Look for BOM Quirks

The schematic symbol property is the best place for per-component notes, but projects scatter this information everywhere. Check all of these:

1. **Schematic text annotations** — free text placed on the schematic sheet. The <code>kicad</code> skill's analyzer extracts these as <code>text_annotations</code>. Look for notes near components about ordering, assembly, or variants.

2. **Title block comments** — the title block has numbered comment fields. Sometimes used for board-level BOM notes ("All passives 0402 unless marked", "Order from DigiKey for proto").

3. **Project README / docs** — look for <code>README.md</code>, <code>docs/</code>, <code>bom/README.md</code>, or any text file mentioning parts, ordering, or assembly. These often contain the highest-level BOM decisions.

4. **Existing BOM CSV Notes column** — if a <code>bom.csv</code> already exists, read the Notes column. The user may have added notes there that aren't in the schematic.

5. **Project-level config** (<code>.kicad-happy.json</code>) — <code>preferred_suppliers</code> sets sourcing priority, <code>bom</code> section sets field naming and grouping conventions. See <code>skills/kicad/references/config-reference.md</code> for the full schema.

6. **Schematic symbol Description field** — sometimes used for assembly notes rather than part description (e.g., "100nF bypass - place close to U3 pin 4").

7. **KiCad custom fields with non-standard names** — fields like <code>Assembly</code>, <code>Order</code>, <code>Variant</code>, <code>Config</code>, <code>SKU</code> may contain BOM-relevant info. The analyzer flags these as <code>unrecognized_fields</code>.

8. **DNP with context** — a DNP component may need a note about *why* it's DNP and *when* to populate it. KiCad's DNP flag is boolean — the reason belongs in BOM Comments.

### Multi-Board / System-Level BOM Concerns

When a project has multiple boards (e.g., main board + daughter board, or sender + receiver):

- **Shared cables/connectors** — document on both boards which connector mates with which, and note "don't double-order" on cables shared between boards
- **Shared power supplies** — if boards share a PSU, document which board's BOM includes it
- **Common parts across boards** — when ordering, consolidate quantities across boards. Note in each board's BOM which parts are shared
- **Board-specific variants** — if the same PCB is used with different stuffing options (e.g., different resistor values for different output voltages), use BOM Comments to document the variant rules

### Non-BOM Items

Some project-specific items aren't on the schematic but need ordering alongside the BOM. Commonly forgotten:

- **Mating connectors & cables** — if the schematic has a connector, the other half needs ordering too (board-to-board, ribbon cables, wire harnesses)
- **Stencil** — order a framed stencil with the PCBs (~$7 from JLCPCB/PCBWay)
- **Programming/debug adapter** — Tag-Connect cable, SWD ribbon, specific USB cable for the board's debug connector
- **Antenna cables** — U.FL to SMA pigtails if the board has an RF connector
- **Mounting hardware** — standoffs, screws, nuts specific to the enclosure
- **Thermal management** — heat sinks, thermal pads for specific components

Track these as rows in the BOM CSV with <code>Reference</code> = <code>--</code> and a Note, or in a separate <code>bom/non-bom-items.csv</code>. Mention them separately in cost estimates.

### Presenting BOM Comments

When generating reports or order summaries, **always surface BOM comments prominently** — they're the designer's voice about exceptions and gotchas. Don't bury them. In the order summary, list any component with a BOM comment separately after the main table so the user sees them before clicking "order."

## Package/Footprint Cross-Reference

| Imperial | Metric | KiCad Footprint |
|----------|--------|----------------|
| 0201 | 0603 | <code>R_0201_0603Metric</code> |
| 0402 | 1005 | <code>R_0402_1005Metric</code> |
| 0603 | 1608 | <code>R_0603_1608Metric</code> |
| 0805 | 2012 | <code>R_0805_2012Metric</code> |
| 1206 | 3216 | <code>R_1206_3216Metric</code> |

Replace <code>R_</code> with <code>C_</code> or <code>L_</code> as appropriate. Prefix with <code>Resistor_SMD:</code>, <code>Capacitor_SMD:</code>, etc.

## BOM Diffing

When the schematic changes between revisions, compare the old and new BOM to identify added, removed, and changed parts. Highlight which new parts need sourcing.

## Interactive BOM (ibom)

Generates an HTML page showing component locations on the PCB — essential for hand-assembly.

```bash
pip install InteractiveHtmlBom
generate_interactive_bom board.kicad_pcb \
  --dest-dir bom/ --name-format "%f_ibom_%r" \
  --extra-fields "MPN,Manufacturer,DigiKey,Mouser,LCSC" \
  --group-fields "Value,Footprint,MPN" \
  --checkboxes "Sourced,Placed" --dnp-field "DNP" --no-browser
```

## Reference Files

Read these when you need detailed lookup data:
- <code>references/kicad-fields.md</code> — field definitions, aliases, S-expression format, part number patterns
- <code>references/ordering-and-fabrication.md</code> — distributor paste formats, gerber export, CPL, cost templates
- <code>references/part-number-conventions.md</code> — detailed analysis of naming patterns across 56+ real projects

## Production Readiness Checklist

- [ ] All parts have MPN and LCSC numbers (for JLCPCB) or MPN (for PCBWay)
- [ ] No obsolete or EOL parts
- [ ] Stock verified, basic vs extended parts identified
- [ ] BOM and CPL exported in correct format
- [ ] Gerbers exported and verified
- [ ] Design rules meet manufacturer minimums (see <code>jlcpcb</code> or <code>pcbway</code> skill)
- [ ] Prototype fully tested

## Generated Files & Cleanup

The BOM and distributor skills create files in the project tree. Know what they are so you can clean up or <code>.gitignore</code> them.

### Files created in the project directory

| File/Dir | Created By | Purpose | Keep in git? |
|----------|-----------|---------|--------------|
| <code>datasheets/</code> | DigiKey, LCSC, element14, Mouser sync scripts | Downloaded PDF datasheets | No — large binaries, re-downloadable |
| <code>datasheets/manifest.json</code> | Datasheet sync scripts | Tracks download status per MPN (legacy name: <code>index.json</code>) | No — regenerated by sync |
| <code>bom/bom.csv</code> | <code>bom_manager.py export</code> | BOM tracking spreadsheet | Yes — user-curated data |
| <code>bom/orders/*.csv</code> | <code>bom_manager.py order</code> | Per-distributor order upload files | No — regenerated before each order |
| <code>*.YYYYMMDD_HHMMSS.bak</code> | <code>edit_properties.py --backup</code> | Schematic backup before edits | No — use git instead |

The <code>kicad</code> skill also creates analyzer JSON and design review markdown reports with user-chosen filenames — see its "Generated Files" section for tracking and cleanup guidance.

### Temporary files (outside project)

| File | Location | Purpose |
|------|----------|---------|
| <code>digikey_token_cache.json</code> | System temp dir | OAuth token cache (9-min TTL, mode 0600) |
| <code>manifest.tmp</code> | <code>datasheets/</code> | Atomic write staging — renamed to <code>manifest.json</code>, never persists |

### Cleanup commands

```bash
# Remove downloaded datasheets (re-downloadable)
rm -rf datasheets/

# Remove order files (regenerate before ordering)
rm -rf bom/orders/

# Remove schematic backups
rm -f *.bak

# Remove KiCad analyzer/report files (filenames vary — check project instructions file)
```

### Suggested .gitignore additions

```gitignore
# BOM skill working files
datasheets/
bom/orders/
*.bak
```

Keep <code>bom/bom.csv</code> tracked — it contains user-curated data (Chosen_Distributor, Validated, Notes) that can't be regenerated from the schematic alone.

## Tips

- **MPN is the universal key** — populate it first, enables cross-referencing everything
- **Schematic is source of truth** — all BOM data in symbol properties, exported as needed
- **DigiKey first, Mouser second** for prototyping; LCSC for production
- **CSV round-trip** — Edit Symbol Fields > Export/Import CSV for bulk updates
- **Field Name Templates** (KiCad 9+) — pre-define MPN, Manufacturer, LCSC, DigiKey, Mouser
- **DigiKey token reuse** — cached to temp file with 9-minute TTL; no need to re-auth per call
- **Second source** — use <code>AltMPN</code> field for critical parts
- **Price at target qty** — prototype pricing != production pricing
- **BOM Comments** — use the <code>BOM Comments</code> symbol property for ordering/assembly quirks that don't fit in standard fields. Flows into CSV Notes column. Check schematic text annotations, README, and existing CSV notes for scattered BOM info too.
