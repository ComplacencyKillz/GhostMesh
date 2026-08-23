---
name: pcbway
description: PCBWay PCB fabrication and assembly — turnkey/consigned assembly, design rules, ordering workflow. Alternative to JLCPCB for manufacturing. Use with KiCad. Use this skill when the user mentions PCBWay, needs turnkey assembly (PCBWay sources parts by MPN), has parts not available on LCSC, needs assembled boards with non-LCSC components, wants to compare PCBWay vs JLCPCB, or needs assembly with parts sourced globally rather than from LCSC only. For gerber/CPL export, stencil ordering, and BOM management, see the <code>bom</code> skill.
---

# PCBWay — PCB Fabrication & Assembly

PCBWay is a PCB fabrication and assembly service based in Shenzhen, China. It is an alternative to JLCPCB with similar capabilities and pricing.

**Typical usage**: Order bare prototype PCBs + framed stencil from PCBWay during prototyping (parts sourced separately from DigiKey/Mouser, hand-assembled in lab). For production runs (100s qty), order fully assembled boards from PCBWay using turnkey component sourcing (PCBWay sources parts by MPN). JLCPCB is the primary alternative. For BOM management, gerber/CPL export, and stencil ordering, see the <code>bom</code> skill.

## Related Skills

| Skill | Purpose |
|-------|---------|
| <code>kicad</code> | Read/analyze KiCad project files, DFM scoring |
| <code>bom</code> | BOM management, gerber/CPL export, stencil ordering |
| <code>digikey</code> | Search DigiKey (prototype sourcing, primary — also preferred for datasheet downloads via API) |
| <code>mouser</code> | Search Mouser (prototype sourcing, secondary) |
| <code>lcsc</code> | Search LCSC (production sourcing, JLCPCB parts) |
| <code>jlcpcb</code> | JLCPCB PCB fabrication & assembly (primary alternative) |
| <code>emc</code> | EMC pre-compliance risk analysis — run before fab to catch EMC issues |
| <code>spice</code> | SPICE simulation — verify analog subcircuits before committing to fab |

## Key Differences from JLCPCB

| Feature | PCBWay | JLCPCB |
|---------|--------|--------|
| Component sourcing | Turnkey (PCBWay sources by MPN) | LCSC parts library (you provide LCSC PNs) |
| Parts library | No fixed library — sources globally | LCSC library (basic/extended parts) |
| Assembly fee model | Quote-based per project | Per-part fees (basic free, extended $3 each) |
| Pricing turnaround | **Manual quote, 1–2 business days** | Instant online pricing |
| BOM format | MPN-based (manufacturer part numbers) | LCSC PN-based |

> **Plan around manual quoting.** PCBWay assembly requires a human-reviewed quote before you can place the order — typically 1-2 business days after uploading files. JLCPCB shows assembly pricing instantly at checkout. If you're prototyping and need to iterate quickly, the JLCPCB feedback loop is much tighter. PCBWay's value is in sourcing parts JLCPCB can't, not in turnaround speed.

**When to use PCBWay over JLCPCB:**
- Parts not available on LCSC — PCBWay sources globally by MPN
- Need turnkey sourcing — provide MPNs, PCBWay handles procurement
- Prefer quote-based pricing over per-part fee model
- Need consigned/kitted assembly (you ship parts to PCBWay)
- Timeline allows for the manual-quote turnaround

## Assembly Options

| Option | Description |
|--------|-------------|
| **Turnkey** | PCBWay sources all parts by MPN — you just provide the BOM |
| **Partial turnkey** | PCBWay sources some parts, you supply others |
| **Consigned/Kitted** | You supply all parts, PCBWay assembles |

## BOM Format for Assembly

PCBWay accepts XLS, XLSX, or CSV BOMs.

### Turnkey / Partial Turnkey BOM

<pre><code>
Line#,Qty,Designator,MPN,Manufacturer,Description,Package,Type
1,3,"C1,C2,C5",GRM155R71C104KA88D,Murata,100nF 16V X7R MLCC,0402,SMD
2,1,U1,ESP32-S3-WROOM-1-N16R8,Espressif,ESP32-S3 WiFi/BT module,ESP32-S3-WROOM-1,SMD
</code></pre>

Required columns:
| Column | Description |
|--------|-------------|
| <code>Line#</code> | Row number |
| <code>Qty</code> | Quantity per board |
| <code>Designator</code> | Reference designators (comma-separated) |
| <code>MPN</code> | Manufacturer Part Number — PCBWay sources by this |
| <code>Manufacturer</code> | Manufacturer name |
| <code>Description</code> | Part description |
| <code>Package</code> | Footprint/package name |
| <code>Type</code> | <code>SMD</code>, <code>THT</code> (through-hole), or <code>Hybrid</code> |

For turnkey, the **MPN** is the critical field — PCBWay uses it to source parts from their global supply chain.

### KiCad BOM Export for PCBWay

1. In KiCad, ensure MPN and Manufacturer fields are populated in symbol properties
2. Export via Edit Symbol Fields > Export CSV
3. Reformat columns to match PCBWay's expected format (add Line#, Qty, Type columns)

For gerber export settings and CPL format, see the <code>bom</code> skill — both JLCPCB and PCBWay use the same formats.

## PCB Design Rules (PCBWay Capabilities)

### Standard PCB (1-2 layers)

| Parameter | Minimum |
|-----------|---------|
| Trace width | 0.1mm (4mil) |
| Trace spacing | 0.1mm (4mil) |
| Via diameter | 0.3mm |
| Via drill | 0.15mm |
| Annular ring | 0.15mm (6mil) |
| Min hole size | 0.15mm |
| Board thickness | 0.2-3.2mm (default 1.6mm) |
| Min board size | 3x3mm |
| Max board size | 600x1200mm (1-2 layer) |

### Multi-layer (4+ layers)

| Parameter | Minimum |
|-----------|---------|
| Trace width | 0.09mm (3.5mil) |
| Trace spacing | 0.09mm (3.5mil) |
| Via drill | 0.15mm |
| Layer count | Up to 14 layers (standard), 24+ (advanced) |
| Max board size | 560x1150mm |

### Additional Capabilities

| Feature | Specification |
|---------|---------------|
| Copper weight (outer) | 1oz-8oz |
| Copper weight (inner) | 1oz-4oz |
| Solder mask colors | Green, Red, Yellow, Blue, White, Black, Matt Green, Matte Black, Purple |
| Silkscreen colors | White, Black, Yellow |
| Surface finishes | HASL (leaded/lead-free), ENIG, OSP, Hard Gold, Immersion Silver/Tin, ENEPIG |
| Impedance control | +/-10% tolerance |
| Castellated holes | >=0.4mm diameter |

### Importing DRU into KiCad

If you have a PCBWay <code>.kicad_dru</code> design rules file, import it in KiCad Board Editor > Board Setup > Design Rules > Import Settings.

## Assembly Constraints

- **Minimum order**: 5 PCBs
- **Sides**: Top, bottom, or both
- **Component types**: SMD, through-hole, or mixed
- **Fine-pitch**: BGA and QFP supported
- **Assembly drawings**: recommended (photos/diagrams of special placement)
- **Special instructions**: add notes for polarity, orientation, or non-standard placement

## PCBWay Partner API (Approval Required)

Contact PCBWay for partner API access. Partner API at <code>https://api-partner.pcbway.com</code>.

Available endpoints:
- **PCB Quotation** — <code>POST api/Pcb/PcbQuotation</code> — get PCB price
- **Place Order** — <code>POST api/Pcb/PlaceOrder</code> — add to cart
- **Confirm Order** — <code>POST api/Pcb/ConfirmOrder</code> — finalize and pay
- **Query Order** — <code>POST api/Pcb/QueryOrderProcess</code> — track order status

Documentation: <code>https://api-partner.pcbway.com/Help</code>

## Ordering Workflow

### Prototype Order (Bare PCB + Stencil)

1. **Export gerbers** from KiCad (see <code>bom</code> skill for export settings)
2. Upload gerbers to <code>https://www.pcbway.com/orderonline.aspx</code> — configure layers, thickness, color, qty
3. Order a **framed stencil** separately at <code>https://www.pcbway.com/stencil.aspx</code>
4. Order — PCBs and stencil typically arrive in ~1 week

### Production Order (Assembled Boards)

1. **Export gerbers** from KiCad (see <code>bom</code> skill for export settings)
2. **Export BOM** as CSV with MPN, Manufacturer, Description, Package, Type columns
3. **Export CPL** (centroid/placement file) as CSV (see <code>bom</code> skill for format)
4. Upload gerbers to PCBWay — configure PCB specs
5. Select "PCB Assembly" — choose Turnkey, Partial Turnkey, or Consigned
6. Upload BOM and centroid files
7. Add assembly drawings/photos if needed (recommended for complex boards)
8. PCBWay reviews files and provides quote
9. Confirm and order

## Tips

- **MPN is what matters** — unlike JLCPCB (LCSC PNs), PCBWay sources globally by Manufacturer Part Number
- **Turnkey is easiest** — just provide MPNs in BOM, PCBWay handles sourcing
- **Assembly requires manual quoting** — unlike JLCPCB's instant pricing, PCBWay reviews your files and provides a custom quote (typically 1-2 business days). Factor this into your timeline.
- **Assembly drawings help** — submit photos or annotated PDFs for complex placement
- **Lead time varies** — turnkey assembly depends on component sourcing time; check with PCBWay
- **Rotation offsets** — PCBWay may have different rotation conventions than KiCad; verify with assembly drawings
- **Solder mask colors** — more options than JLCPCB (includes purple, matt variants)
- **Edge clearance** — keep copper >=0.3mm from board edge
