---
name: lcsc
description: Search LCSC Electronics for electronic components — find parts by LCSC number (Cxxxxx) or MPN, check stock/pricing, download datasheets, analyze specifications. Sister company to JLCPCB, same parts library. Sync and maintain a local datasheets directory for a KiCad project, or use batch MPN-list seeding (<code>--mpn-list</code>) for bulk workflows without a project. No API key needed — uses the free jlcsearch community API. Use this skill when the user mentions LCSC, JLCPCB parts library, JLCPCB assembly parts, production sourcing, Cxxxxx part numbers, needs to find LCSC equivalents for parts, is preparing a BOM for JLCPCB assembly, or wants to download datasheets and LCSC is available. For package cross-reference tables and BOM workflow, see the <code>bom</code> skill.
---

# LCSC Electronics — Component Search, Datasheets & Ordering

## Related Skills

| Skill | Purpose |
|-------|---------|
| <code>kicad</code> | Schematic analysis — extracts MPNs for part lookup |
| <code>bom</code> | BOM management — orchestrates sourcing across distributors |
| <code>jlcpcb</code> | PCB assembly — shares the same parts library |
| <code>spice</code> | Uses LCSC parametric data for behavioral SPICE models (no auth needed) |

LCSC is JLCPCB's sister company — they share the same parts library and <code>Cxxxxx</code> part numbers. Use LCSC for **production sourcing** (assembled boards from JLCPCB/PCBWay). DigiKey/Mouser are for prototyping. For BOM management and export workflows, see <code>bom</code>.

## Key Differences from DigiKey/Mouser

- **No API key needed** — jlcsearch community API is free and open
- **Lower prices** — especially for passives and Chinese-manufactured ICs
- **JLCPCB integration** — same LCSC part numbers used in JLCPCB assembly BOMs
- **Direct PDF downloads** — LCSC's CDN (wmsc.lcsc.com) serves datasheets without bot protection
- **Low MOQ** — many parts available in quantities as low as 1
- **Warehouses** — Shenzhen (JS), Zhuhai (ZH), Hong Kong (HK)
- **Website**: <code>https://www.lcsc.com</code>

## LCSC Part Numbers

Format: <code>Cxxxxx</code> (e.g., <code>C14663</code>). This is the universal identifier across both LCSC and JLCPCB. Use it for:
- Direct ordering on LCSC
- BOM matching in JLCPCB assembly (see <code>jlcpcb</code> skill)
- Cross-referencing between platforms

## jlcsearch API Reference

The jlcsearch community API is the recommended way to search LCSC. **No authentication required.**

**Base URL:** <code>https://jlcsearch.tscircuit.com</code>

### General Search

<pre><code>
GET /api/search?q=<query>&limit=20&full=true
</code></pre>

Parameters:
- <code>q</code> — search query (matches MPN, LCSC code, or description keywords)
- <code>package</code> — optional footprint filter (e.g., <code>0402</code>)
- <code>limit</code> — max results (default 100)
- <code>full</code> — set to <code>true</code> to include all fields (datasheet URL, specs, stock per warehouse)

### Category-Specific Search

<pre><code>
GET /resistors/list.json?search=10k+0402
GET /capacitors/list.json?search=100nF+0402
GET /microcontrollers/list.json?search=STM32
GET /voltage_regulators/list.json?search=3.3V
</code></pre>

### Response Format

Results are returned as <code>{"components": [...]}</code>. With <code>full=true</code>, each component has:

<pre><code>
{
  "lcsc": 14663,
  "mfr": "GRM155R71C104KA88D",
  "package": "0402",
  "description": "",
  "datasheet": "https://www.lcsc.com/datasheet/...",
  "stock": 2751535,
  "price": [{"qFrom": 1, "qTo": 9, "price": 0.0069}, ...],
  "basic": 0,
  "extra": {
    "number": "C71629",
    "mpn": "GRM155R71C104KA88D",
    "manufacturer": {"id": 4, "name": "Murata Electronics"},
    "package": "0402",
    "description": "16V 100nF X7R ±10% 0402 ...",
    "quantity": 2751535,
    "whs-js": 1234567,
    "whs-zh": 567890,
    "whs-hk": 0,
    "moq": 100,
    "order_multiple": 100,
    "packaging": "Tape & Reel (TR)",
    "packaging_num": 10000,
    "datasheet": {"pdf": "https://wmsc.lcsc.com/wmsc/upload/file/pdf/v2/lcsc/...pdf"},
    "images": [{"96x96": "...", "224x224": "...", "900x900": "..."}],
    "rohs": true,
    "url": "https://www.lcsc.com/product-detail/...",
    "attributes": {
      "Capacitance": "100nF",
      "Voltage Rated": "16V",
      "Temperature Coefficient": "X7R",
      "Tolerance": "±10%"
    },
    "prices": [{"min_qty": 100, "max_qty": 499, "currency": "USD", "price": 0.0048}, ...]
  }
}
</code></pre>

Key fields:
- <code>lcsc</code> — numeric LCSC ID (without "C" prefix)
- <code>extra.number</code> — full LCSC code with prefix (e.g., <code>C71629</code>)
- <code>extra.mpn</code> — manufacturer part number
- <code>extra.manufacturer.name</code> — manufacturer
- <code>extra.datasheet.pdf</code> — **direct PDF URL** (wmsc.lcsc.com CDN, downloads without auth)
- <code>extra.attributes</code> — parametric specs (capacitance, voltage, etc.)
- <code>extra.quantity</code> — total stock across all warehouses
- <code>extra.whs-js</code>, <code>extra.whs-zh</code>, <code>extra.whs-hk</code> — stock per warehouse
- <code>basic</code> — <code>1</code> if JLCPCB basic part (no setup fee), <code>0</code> if extended
- <code>extra.moq</code> — minimum order quantity
- <code>extra.order_multiple</code> — must order in multiples of this
- <code>extra.rohs</code> — RoHS compliance (boolean)

### Rate Limits

The jlcsearch API is community-run with no documented rate limits, but be respectful — use delays of 0.5s between calls.

## Datasheet Download & Sync

LCSC's CDN serves datasheet PDFs directly — no bot protection, no special headers needed. This makes LCSC a reliable datasheet source alongside DigiKey.

### Datasheet Directory Sync

Use <code>sync_datasheets_lcsc.py</code> to maintain a <code>datasheets/</code> directory alongside a KiCad project. Same workflow and <code>manifest.json</code> format as the DigiKey and Mouser skills. **No API key required.**

<pre><code>
# Sync datasheets for a KiCad project
python3 <skill-path>/scripts/sync_datasheets_lcsc.py <file.kicad_sch>

# Preview what would be downloaded
python3 <skill-path>/scripts/sync_datasheets_lcsc.py <file.kicad_sch> --dry-run

# Retry previously failed downloads
python3 <skill-path>/scripts/sync_datasheets_lcsc.py <file.kicad_sch> --force

# Custom output directory
python3 <skill-path>/scripts/sync_datasheets_lcsc.py <file.kicad_sch> -o ./my-datasheets

# Parallel downloads (3 workers)
python3 <skill-path>/scripts/sync_datasheets_lcsc.py <file.kicad_sch> --parallel 3

# Batch mode — sync from a plain MPN list (no KiCad project required)
python3 <skill-path>/scripts/sync_datasheets_lcsc.py --mpn-list mpns.txt --output ./datasheets
</code></pre>

**MPN-list batch mode** (KH-312) — when you have a list of MPNs but no
KiCad project to point at. One MPN per line; blank lines and <code>#</code>
comments (full-line and inline) are skipped; generic values are filtered
via <code>is_real_mpn()</code> and de-duplicated. Output defaults to <code>./datasheets/</code>
in the current working directory when <code>--output</code> is omitted.

The script:
- **Runs the kicad schematic analyzer** to extract components, MPNs, and LCSC codes
- **Accepts any identifier** — MPN, LCSC code, or other distributor PNs from KiCad symbol properties
- **Prefers LCSC code** for search (exact match) — falls back to MPN keyword search
- **Falls back to wmsc.lcsc.com API** when jlcsearch has no results for an LCSC code (Cxxxxx)
- **Downloads from LCSC CDN** — direct PDF URLs, no bot protection
- **Writes <code>manifest.json</code> manifest** — same format as DigiKey/Mouser skills
- **Verifies PDF content** — checks MPN, manufacturer, and description keywords
- **Rate-limited** — 0.5s between API calls (configurable with <code>--delay</code>)
- **Saves progress incrementally** — safe to interrupt

### Single Datasheet Download

Use <code>fetch_datasheet_lcsc.py</code> for one-off downloads.

<pre><code>
# Search by MPN
python3 <skill-path>/scripts/fetch_datasheet_lcsc.py --search "GRM155R71C104KA88D" -o datasheet.pdf

# Search by LCSC code
python3 <skill-path>/scripts/fetch_datasheet_lcsc.py --search "C14663" -o datasheet.pdf

# Direct URL download
python3 <skill-path>/scripts/fetch_datasheet_lcsc.py "https://wmsc.lcsc.com/..." -o datasheet.pdf

# JSON output
python3 <skill-path>/scripts/fetch_datasheet_lcsc.py --search "C14663" --json
</code></pre>

The script:
- **OS-agnostic** — uses <code>requests</code> → <code>urllib</code> → <code>playwright</code> fallback chain (no wget/curl)
- **Validates PDF headers** — rejects HTML error pages
- **Falls back to alternative manufacturer sources** when LCSC URL fails
- **Exit codes**: 0 = success, 1 = download failed, 2 = search/API error
- **Dependencies**:
  - <code>pip install requests</code> (recommended; urllib fallback works fine for LCSC)
  - <code>pip install playwright && playwright install chromium</code> (optional; rarely needed for LCSC)

## LCSC Official API (Requires Approval)

**Base URL:** <code>https://ips.lcsc.com</code>. Requires API key + signature authentication. Contact <code>support@lcsc.com</code> for access. Rarely needed — jlcsearch covers most use cases.

## Web Search Fallback

If the jlcsearch API is unavailable, search LCSC by fetching the website directly:

<pre><code>
https://www.lcsc.com/search?q=<query>
</code></pre>

## Cross-Referencing & Missing Equivalents

LCSC part numbers are specific to the LCSC/JLCPCB ecosystem. Use the <code>extra.mpn</code> field to cross-reference on DigiKey/Mouser.

When an MPN has no exact LCSC match:
1. Search by key parameters (e.g., "100nF 0402 X7R 16V")
2. Look for pin-compatible alternatives from Chinese manufacturers
3. Verify specs and footprint match — pad dimensions can vary even within the same package size
4. As a last resort, mark as "consigned" and source separately

## Tips

- <code>basic</code> field matters — JLCPCB basic parts have no setup fee; extended parts cost $3 each
- Check stock per warehouse (<code>whs-js</code>, <code>whs-zh</code>, <code>whs-hk</code>) — availability varies
- <code>moq</code> and <code>order_multiple</code> — many parts require minimum quantities or specific multiples
- Datasheet quality varies for Chinese manufacturers — cross-reference MPN on DigiKey for better docs
