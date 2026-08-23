---
name: element14
description: Search Newark, Farnell, and element14 for electronic components — find parts by MPN or distributor part number, check pricing/stock, download datasheets, analyze specifications. One unified API covers all three storefronts (Newark for US, Farnell for UK/EU, element14 for APAC). Free API key, simple query-parameter auth, no OAuth. Datasheets download directly from farnell.com CDN with no bot protection. Sync and maintain a local datasheets directory for a KiCad project, or use batch MPN-list seeding (<code>--mpn-list</code>) for bulk workflows without a project. Use this skill when the user mentions Newark, Farnell, element14, needs parts from a non-US distributor, wants to compare pricing across regions, or needs datasheets from a source that doesn't require complex API auth. For package cross-reference tables and BOM workflow, see the <code>bom</code> skill.
---

# element14 / Newark / Farnell — Component Search, Datasheets & Ordering

## Related Skills

| Skill | Purpose |
|-------|---------|
| <code>kicad</code> | Schematic analysis — extracts MPNs for part lookup |
| <code>bom</code> | BOM management — orchestrates sourcing across distributors |
| <code>spice</code> | Uses element14 parametric data for behavioral SPICE models |

One API covers three regional storefronts — same catalog, same datasheets, only pricing/stock vary by region:

| Storefront | Region | Store ID |
|------------|--------|----------|
| **Newark** | North America | <code>www.newark.com</code> |
| **Farnell** | UK / Europe | <code>uk.farnell.com</code> |
| **element14** | Asia-Pacific | <code>au.element14.com</code> |

For BOM management and export workflows, see <code>bom</code>.

## Key Differences from DigiKey/Mouser

- **Simple auth** — API key as a query parameter, no OAuth flow
- **Free API key** — register at partner.element14.com, courtesy usage allowance
- **Global coverage** — same API covers US (Newark), EU (Farnell), APAC (element14)
- **Unprotected PDFs** — datasheets hosted on farnell.com CDN, download freely with no bot protection
- **Datasheet URL in API response** — <code>responseGroup=medium</code> includes <code>datasheets[].url</code>

## API Credential Setup

1. **Register** at [partner.element14.com/member/register](https://partner.element14.com/member/register)
   - Free account — just username, email, password. No credit card needed.
   - Provides a "courtesy usage allowance" (2 calls/sec, 1,000 calls/day — sufficient for normal use)
2. **Register an application** — after logging in, go to [My API Keys](https://partner.element14.com/apps/mykeys) and click "Get API Keys"
   - App name: anything (e.g., "kicad-happy")
   - Type: "Desktop application"
   - Users: "1-10"
   - Commercial: No
   - Advertising: No
   - Check "Issue a new key for Product Search API" → select "Basic" tier
   - Agree to Terms of Service and click "Register Application"
3. **Copy your API key** — a 24-character alphanumeric string shown on the My API Keys page
4. **Set the environment variable** <code>ELEMENT14_API_KEY</code> before running the scripts:
   ```bash
   export ELEMENT14_API_KEY=your_api_key_here
<pre><code>
   If credentials are stored in a central secrets file (e.g., <code>~/.config/secrets.env</code>), load them first:
   ```bash
   export $(grep -v '^#' ~/.config/secrets.env | grep -v '^$' | xargs)
</code></pre>

## Product Search API

**Base URL:** <code>https://api.element14.com/catalog/products</code>

All requests use GET with query parameters. Authentication is via <code>callInfo.apiKey</code>.

### Search Modes

The <code>term</code> parameter supports three search types:

| Mode | Format | Example |
|------|--------|---------|
| **Keyword** | <code>any:<keywords></code> | <code>term=any:100nF 0402 X7R</code> |
| **MPN** | <code>manuPartNum:<mpn></code> | <code>term=manuPartNum:GRM155R71C104KA88D</code> |
| **Distributor PN** | <code>id:<sku></code> | <code>term=id:94AK6874</code> |

### Full Example

<pre><code>
GET https://api.element14.com/catalog/products
  ?term=manuPartNum:GRM155R71C104KA88D
  &storeInfo.id=www.newark.com
  &resultsSettings.offset=0
  &resultsSettings.numberOfResults=10
  &resultsSettings.responseGroup=medium
  &callInfo.responseDataFormat=JSON
  &callInfo.apiKey=YOUR_KEY
</code></pre>

### Response Groups

| Group | Fields |
|-------|--------|
| <code>small</code> | SKU, displayName, brandName, MPN, attributes |
| <code>medium</code> | + datasheets[], prices[], stock |
| <code>large</code> | + images, related products, country of origin |
| <code>prices</code> | Tiered pricing only |
| <code>inventory</code> | Stock levels by warehouse/region |

### Response Format

With <code>responseGroup=medium</code>, the response looks like:

<pre><code>
{
  "manufacturerPartNumberSearchReturn": {
    "numberOfResults": 5,
    "products": [
      {
        "sku": "94AK6874",
        "displayName": "Murata GRM155R71C104KA88D",
        "translatedManufacturerPartNumber": "GRM155R71C104KA88D",
        "brandName": "Murata Electronics",
        "datasheets": [
          {
            "type": "TechnicalDataSheet",
            "description": "Datasheet",
            "url": "https://www.farnell.com/datasheets/74273.pdf"
          }
        ],
        "prices": [
          {
            "from": 1,
            "to": 9,
            "cost": 0.156
          }
        ],
        "stock": {
          "level": 45000,
          "leastLeadTime": 0,
          "status": 4,
          "statusMessage": "In Stock"
        },
        "attributes": [
          {"attributeLabel": "Capacitance", "attributeUnit": "", "attributeValue": "100nF"},
          {"attributeLabel": "Voltage Rating", "attributeUnit": "V", "attributeValue": "16"}
        ],
        "rohsStatusCode": "YES"
      }
    ]
  }
}
</code></pre>

Key fields:
- <code>sku</code> — Newark/Farnell/element14 part number
- <code>translatedManufacturerPartNumber</code> — MPN
- <code>brandName</code> — manufacturer
- <code>datasheets[].url</code> — **direct PDF URL** (farnell.com CDN, no bot protection)
- <code>datasheets[].type</code> — usually <code>TechnicalDataSheet</code>
- <code>prices[]</code> — tiered pricing with <code>from</code>, <code>to</code>, <code>cost</code>
- <code>stock.level</code> — quantity in stock
- <code>stock.statusMessage</code> — human-readable availability
- <code>attributes[]</code> — parametric specs (label, unit, value)
- <code>rohsStatusCode</code> — RoHS compliance (<code>YES</code>/<code>NO</code>)

### Store IDs

Common store IDs for the <code>storeInfo.id</code> parameter:

| Store ID | Region |
|----------|--------|
| <code>www.newark.com</code> | US (default) |
| <code>uk.farnell.com</code> | UK |
| <code>www.farnell.com</code> | EU |
| <code>au.element14.com</code> | Australia |
| <code>sg.element14.com</code> | Singapore |
| <code>in.element14.com</code> | India |

### Rate Limits

No documented rate limits beyond the courtesy usage allowance. Be respectful — use 0.5s delays between calls.

### Filters

Add to query parameters:
- <code>resultsSettings.refinements.filter=rohsCompliant</code> — RoHS parts only
- <code>resultsSettings.refinements.filter=inStock</code> — in-stock only

### Pagination

- <code>resultsSettings.offset</code> — starting index (0-based)
- <code>resultsSettings.numberOfResults</code> — max 50 per page
- Only the first 100 results are reliably pageable

## Datasheet Download & Sync

element14's farnell.com CDN serves datasheet PDFs directly — no bot protection, no special headers needed. Datasheet URLs come from the API response (<code>datasheets[].url</code>).

### Datasheet Directory Sync

Use <code>sync_datasheets_element14.py</code> to maintain a <code>datasheets/</code> directory alongside a KiCad project. Same workflow and <code>manifest.json</code> format as the DigiKey, Mouser, and LCSC skills.

<pre><code>
# Sync datasheets for a KiCad project
python3 <skill-path>/scripts/sync_datasheets_element14.py <file.kicad_sch>

# Preview what would be downloaded
python3 <skill-path>/scripts/sync_datasheets_element14.py <file.kicad_sch> --dry-run

# Retry previously failed downloads
python3 <skill-path>/scripts/sync_datasheets_element14.py <file.kicad_sch> --force

# Use a specific store (default: www.newark.com)
python3 <skill-path>/scripts/sync_datasheets_element14.py <file.kicad_sch> --store uk.farnell.com

# Custom output directory
python3 <skill-path>/scripts/sync_datasheets_element14.py <file.kicad_sch> -o ./my-datasheets

# Parallel downloads (3 workers)
python3 <skill-path>/scripts/sync_datasheets_element14.py <file.kicad_sch> --parallel 3

# Batch mode — sync from a plain MPN list (no KiCad project required)
python3 <skill-path>/scripts/sync_datasheets_element14.py --mpn-list mpns.txt --output ./datasheets
</code></pre>

**MPN-list batch mode** (KH-312) — when you have a list of MPNs but no
KiCad project to point at. One MPN per line; blank lines and <code>#</code>
comments (full-line and inline) are skipped; generic values are filtered
via <code>is_real_mpn()</code> and de-duplicated. Output defaults to <code>./datasheets/</code>
in the current working directory when <code>--output</code> is omitted. Note:
<code>ELEMENT14_API_KEY</code> is checked unconditionally at startup, so
<code>--dry-run</code> still requires the env var to be set even though no
network calls are made.

The script:
- **Runs the kicad schematic analyzer** to extract components, MPNs, and distributor PNs
- **Accepts any identifier** — MPN, Newark/Farnell PN, or other distributor PNs from KiCad symbol properties
- **Prefers MPN search** (<code>manuPartNum:</code>) for exact match — falls back to keyword search
- **Downloads from farnell.com CDN** — direct PDF URLs, no bot protection
- **Writes <code>manifest.json</code> manifest** — same format as DigiKey/Mouser/LCSC skills
- **Verifies PDF content** — checks MPN, manufacturer, and description keywords
- **Rate-limited** — 0.5s between API calls (configurable with <code>--delay</code>)
- **Saves progress incrementally** — safe to interrupt

### Single Datasheet Download

Use <code>fetch_datasheet_element14.py</code> for one-off downloads.

<pre><code>
# Search by MPN
python3 <skill-path>/scripts/fetch_datasheet_element14.py --search "GRM155R71C104KA88D" -o datasheet.pdf

# Search by Newark/Farnell part number
python3 <skill-path>/scripts/fetch_datasheet_element14.py --search "94AK6874" -o datasheet.pdf

# Direct URL download
python3 <skill-path>/scripts/fetch_datasheet_element14.py "https://www.farnell.com/datasheets/74273.pdf" -o datasheet.pdf

# JSON output
python3 <skill-path>/scripts/fetch_datasheet_element14.py --search "GRM155R71C104KA88D" --json
</code></pre>

The script:
- **OS-agnostic** — uses <code>requests</code> → <code>urllib</code> → <code>playwright</code> fallback chain
- **Validates PDF headers** — rejects HTML error pages
- **Falls back to alternative manufacturer sources** when element14 URL fails
- **Exit codes**: 0 = success, 1 = download failed, 2 = search/API error
- **Dependencies**:
  - <code>pip install requests</code> (recommended; urllib fallback works fine for element14)
  - <code>pip install playwright && playwright install chromium</code> (optional; rarely needed)

## Web Search Fallback

If the API is unavailable, search by fetching product pages directly:

<pre><code>
https://www.newark.com/search?st=<query>
https://uk.farnell.com/search?st=<query>
</code></pre>

## Tips

- Use <code>responseGroup=medium</code> — includes datasheets and pricing without the overhead of <code>large</code>
- Use <code>manuPartNum:</code> prefix for exact MPN matches; <code>any:</code> for keyword search
- Cross-reference using <code>translatedManufacturerPartNumber</code> (MPN) across DigiKey/Mouser/LCSC
- Useful for international users where DigiKey/Mouser shipping is expensive
