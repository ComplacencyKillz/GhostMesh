# KiCad Symbol Properties Reference

## Standard Fields

| Field | Description | Example |
|-------|-------------|---------|
| <code>Reference</code> | Designator (auto) | <code>C1</code>, <code>U3</code>, <code>R5</code> |
| <code>Value</code> | Component value | <code>100nF</code>, <code>ESP32-S3-WROOM-1</code> |
| <code>Footprint</code> | Library:footprint | <code>Capacitor_SMD:C_0402_1005Metric</code> |
| <code>Datasheet</code> | URL to datasheet | <code>https://...</code> |
| <code>Description</code> | Part description | <code>100nF 16V X7R 0402 MLCC</code> |

## Custom BOM Fields

| Field | Purpose | When | Example |
|-------|---------|------|---------|
| <code>MPN</code> | Manufacturer Part Number | Always | <code>GRM155R71C104KA88D</code> |
| <code>Manufacturer</code> | Part manufacturer | Always | <code>Murata</code> |
| <code>DigiKey</code> | DigiKey PN — primary prototype source | Prototype | <code>490-10698-1-ND</code> |
| <code>Mouser</code> | Mouser PN — secondary prototype source | Prototype | <code>81-GRM155R71C104KA8D</code> |
| <code>LCSC</code> | LCSC PN — production assembly source | Production | <code>C14663</code> |
| <code>AltMPN</code> | Alternate/second-source MPN | Optional | <code>CL05B104KO5NNNC</code> |
| <code>BOM Comments</code> | Freeform per-component ordering/assembly notes (flows into CSV Notes column) | Optional | <code>Proto only — DNP in production</code> |

## Field Name Aliases

Projects use inconsistent field names. The analyzer recognizes all common variants:

| Canonical | Also Recognized As |
|---|---|
| <code>MPN</code> | <code>Manufacturer Part Number</code>, <code>Manufacturer_Part_Number</code>, <code>Manufacturer Part #</code>, <code>PartNumber</code>, <code>Part Number</code>, <code>Mfr_No</code>, <code>ManufacturerPartNumber</code> |
| <code>Manufacturer</code> | <code>Manufacturer_Name</code>, <code>Mfr</code>, <code>MFR</code> |
| <code>DigiKey</code> | <code>Digi-Key Part Number</code>, <code>Digi-Key_PN</code>, <code>DigiKey Part</code>, <code>DigiKey_Part_Number</code>, <code>DK</code> |
| <code>Mouser</code> | <code>Mouser Part Number</code>, <code>Mouser Part</code>, <code>Mouser_PN</code>, <code>Mouser PN</code> |
| <code>LCSC</code> | <code>LCSC Part #</code>, <code>LCSC Part Number</code>, <code>LCSCStockCode</code>, <code>JLCPCB</code>, <code>JLCPCB Part</code>, <code>JLC</code> |
| <code>element14</code> | <code>Newark</code>, <code>Newark Part Number</code>, <code>Newark_PN</code>, <code>Farnell</code>, <code>Farnell_PN</code>, <code>element14_PN</code> |
| <code>BOM Comments</code> | <code>BOM_Comments</code>, <code>BOM Comment</code>, <code>BOM_Comment</code>, <code>BOM Notes</code>, <code>BOM_Notes</code>, <code>BOM Note</code>, <code>Ordering Notes</code>, <code>Assembly Notes</code>, <code>Notes</code>, <code>Remarks</code>, <code>Comment</code> |

When writing new fields, use the canonical names for consistency. When a project already has a convention (e.g., <code>Digi-Key_PN</code>), respect it.

## S-expression Format

Custom fields in <code>.kicad_sch</code> files:

```
(property "MPN" "GRM155R71C104KA88D"
    (at 0 0 0)
    (effects (font (size 1.27 1.27)) (hide yes))
)
```

## Adding/Editing Properties in KiCad

- **Single symbol**: Double-click or E > "+" to add field
- **Bulk editing**: Tools > Edit Symbol Fields (spreadsheet view, supports CSV export/import)
- **Field Name Templates** (KiCad 9+): Schematic Setup > Field Name Templates — pre-define MPN, Manufacturer, etc.

## Part Number Patterns

- **MPN is the universal key** — cross-references across all distributors
- **DigiKey PNs** end in <code>-ND</code> (e.g., <code>311-10.0KCRCT-ND</code>)
- **Mouser PNs** have numeric prefixes (e.g., <code>81-GRM155R71C104KA8D</code>)
- **LCSC PNs** are <code>Cxxxxx</code> (e.g., <code>C14663</code>)
- **Newark/Farnell PNs** are alphanumeric SKUs (e.g., <code>94AK6874</code>)
- Any identifier is useful — even a single distributor PN can be used to find the MPN and other PNs
- Parts with no identifiers need manual enrichment before datasheet sync or ordering

For detailed analysis of part number conventions across 56+ real-world projects, see <code>part-number-conventions.md</code>.
