---
name: digikey
description: >-
  Search DigiKey for electronic components and download datasheets — primary
  source for prototype orders and the preferred API method for fetching
  datasheets. Find parts by keyword or MPN, check pricing/stock, download
  datasheets via API, analyze specifications. Sync and maintain a local
  datasheets directory — extract components from schematics, download missing
  datasheets, keep them up to date. Also supports batch MPN-list seeding
  (<code>--mpn-list</code>) for bulk workflows without a KiCad project. Use when the user asks about electronic
  components, part specs, datasheets, pricing, stock, footprints, or needs to
  download a datasheet — even without mentioning "DigiKey". Also for "sync
  datasheets", "download datasheets for my board/project", or mentions a
  datasheets directory. DigiKey is the default distributor for prototyping. For
  BOM workflows, see the bom skill.
---

# DigiKey Parts Search & Analysis

## Related Skills

| Skill | Purpose |
|-------|---------|
| <code>kicad</code> | Schematic analysis — extracts MPNs for datasheet sync |
| <code>bom</code> | BOM management — orchestrates sourcing across distributors |
| <code>spice</code> | Uses DigiKey parametric data for behavioral SPICE models |

DigiKey is the **primary source for prototype orders** (Mouser is secondary). Its API returns direct PDF datasheet links, making it the preferred datasheet source. For production orders, see <code>lcsc</code>/<code>jlcpcb</code>. For BOM management and export workflows, see <code>bom</code>.

## API Credential Setup

The DigiKey API requires OAuth 2.0 credentials. Here's how to set them up:

1. **Create a DigiKey account** at [digikey.com](https://www.digikey.com) if you don't have one
2. **Register an API app** at [developer.digikey.com](https://developer.digikey.com):
   - Sign in with your DigiKey account
   - Go to "My Apps" → "Create App"
   - App name: anything (e.g., "kicad-happy")
   - Select **"Product Information v4"** API
   - OAuth type: **Client Credentials** (2-legged, no user login needed)
   - Callback URL: <code>https://localhost</code> (not used for client credentials, but required)
   - After creation, note the **Client ID** and **Client Secret**
3. **Set the environment variables** before running the scripts:
   ```bash
   export DIGIKEY_CLIENT_ID=your_client_id_here
   export DIGIKEY_CLIENT_SECRET=your_client_secret_here
<pre><code>
   If credentials are stored in a central secrets file (e.g., <code>~/.config/secrets.env</code>), load them first:
   ```bash
   export $(grep -v '^#' ~/.config/secrets.env | grep -v '^$' | xargs)
</code></pre>

The client credentials flow has no user interaction — once configured, API calls work automatically.

## DigiKey Product Information API v4

The API is the preferred way to search DigiKey. It returns structured JSON with full product details, pricing, stock, datasheets, and parametric data.

**Base URL:** <code>https://api.digikey.com</code>

### Authentication

All API requests require OAuth 2.0. Use the **client credentials flow** (2-legged). Credentials must be loaded as environment variables (see "API Credential Setup" above).

<pre><code>
curl -s -X POST https://api.digikey.com/v1/oauth2/token \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "client_id=${DIGIKEY_CLIENT_ID}&client_secret=${DIGIKEY_CLIENT_SECRET}&grant_type=client_credentials"
</code></pre>

The response returns an <code>access_token</code> valid for **10 minutes**. Cache the token in a shell variable and reuse it for subsequent calls in the same session. If you get a 401 error mid-session, the token has expired — re-authenticate to get a fresh one.

### Required Headers

Every API call needs:
<pre><code>
X-DIGIKEY-Client-Id: ${DIGIKEY_CLIENT_ID}
Authorization: Bearer <access_token>
</code></pre>

Optional locale headers:
- <code>X-DIGIKEY-Locale-Language</code>: <code>en</code> (default), <code>ja</code>, <code>de</code>, <code>fr</code>, <code>ko</code>, <code>zhs</code>, <code>zht</code>, <code>it</code>, <code>es</code>
- <code>X-DIGIKEY-Locale-Currency</code>: <code>USD</code> (default), <code>CAD</code>, <code>EUR</code>, <code>GBP</code>, <code>JPY</code>, etc.
- <code>X-DIGIKEY-Locale-Site</code>: <code>US</code> (default), <code>CA</code>, <code>UK</code>, <code>DE</code>, etc.

### KeywordSearch — Find Parts

<pre><code>
POST /products/v4/search/keyword
</code></pre>

This is the primary search endpoint. Search by MPN, DigiKey part number, description, or keywords.

Request body:
<pre><code>
{
  "Keywords": "GRM155R71C104KA88D",
  "Limit": 25,
  "Offset": 0,
  "FilterOptionsRequest": {
    "MinimumQuantityAvailable": 1,
    "SearchOptions": ["InStock", "HasDatasheet", "RoHSCompliant"],
    "ManufacturerFilter": [{"Id": "..."}],
    "CategoryFilter": [{"Id": "..."}],
    "StatusFilter": [{"Id": "..."}],
    "MarketPlaceFilter": "ExcludeMarketPlace"
  },
  "SortOptions": {
    "Field": "Price",
    "SortOrder": "Ascending"
  }
}
</code></pre>

Key request fields:
- <code>Keywords</code> (string, max 250 chars) — search term (MPN, DK PN, description)
- <code>Limit</code> (int, 1-50) — results per page
- <code>Offset</code> (int) — pagination offset
- <code>SearchOptions</code> — array of: <code>InStock</code>, <code>HasDatasheet</code>, <code>RoHSCompliant</code>, <code>NormallyStocking</code>, <code>Has3DModel</code>, <code>HasCadModel</code>, <code>HasProductPhoto</code>, <code>NewProduct</code>
- <code>SortOptions.Field</code> — <code>Price</code>, <code>QuantityAvailable</code>, <code>Manufacturer</code>, <code>ManufacturerProductNumber</code>, <code>DigiKeyProductNumber</code>, <code>MinimumQuantity</code>
- <code>MarketPlaceFilter</code> — <code>NoFilter</code>, <code>ExcludeMarketPlace</code>, <code>MarketPlaceOnly</code>

Response — key fields in each <code>Products[]</code> item:
<pre><code>
{
  "ManufacturerProductNumber": "GRM155R71C104KA88D",
  "Manufacturer": {"Id": 563, "Name": "Murata Electronics"},
  "Description": {
    "ProductDescription": "CAP CER 100NF 16V X7R 0402",
    "DetailedDescription": "..."
  },
  "UnitPrice": 0.01,
  "QuantityAvailable": 248000,
  "ProductUrl": "https://www.digikey.com/...",
  "DatasheetUrl": "https://...",
  "PhotoUrl": "https://...",
  "ProductVariations": [
    {
      "DigiKeyProductNumber": "490-10698-1-ND",
      "PackageType": {"Name": "Cut Tape"},
      "StandardPricing": [
        {"BreakQuantity": 1, "UnitPrice": 0.01, "TotalPrice": 0.01},
        {"BreakQuantity": 10, "UnitPrice": 0.008, "TotalPrice": 0.08}
      ],
      "QuantityAvailableforPackageType": 248000,
      "MinimumOrderQuantity": 1,
      "StandardPackage": 10000
    }
  ],
  "Parameters": [
    {"ParameterText": "Capacitance", "ValueText": "100nF"},
    {"ParameterText": "Voltage Rated", "ValueText": "16V"},
    {"ParameterText": "Temperature Coefficient", "ValueText": "X7R"},
    {"ParameterText": "Package / Case", "ValueText": "0402 (1005 Metric)"}
  ],
  "ProductStatus": {"Status": "Active"},
  "Category": {"Name": "Ceramic Capacitors"},
  "Classifications": {"RohsStatus": "ROHS3 Compliant"},
  "Discontinued": false,
  "EndOfLife": false,
  "NormallyStocking": true
}
</code></pre>

### ProductDetails — Full Details for One Part

<pre><code>
GET /products/v4/search/{productNumber}/productdetails
</code></pre>

Use this for expanded information on a specific part. <code>{productNumber}</code> can be a DigiKey part number or manufacturer part number.

Query parameters:
- <code>manufacturerId</code> (optional) — disambiguate MPNs that match multiple manufacturers (e.g., "CR2032")

Returns the full <code>Product</code> object with all parameters, pricing (including MyPricing if authenticated with account), media links, and related products.

### Other Useful Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| <code>/products/v4/search/{pn}/productdetails</code> | GET | Full product info for one part |
| <code>/products/v4/search/productpricing/{pn}</code> | GET | Pricing with MyPricing for a part |
| <code>/products/v4/search/{pn}/media</code> | GET | All media (images, datasheets) for a part |
| <code>/products/v4/search/manufacturers</code> | GET | All manufacturers (use IDs in KeywordSearch filters) |
| <code>/products/v4/search/categories</code> | GET | All categories (use IDs in KeywordSearch filters) |
| <code>/products/v4/search/{pn}/alternatepackaging</code> | GET | Alternate packaging options |
| <code>/products/v4/search/{pn}/substitutions</code> | GET | Substitute parts |
| <code>/products/v4/search/{pn}/recommendedproducts</code> | GET | Recommended/associated parts |

### Rate Limits

Per-minute and daily quotas apply. HTTP 429 with <code>Retry-After</code> header on exceed.

### Error Responses

All errors return <code>DKProblemDetails</code>:
<pre><code>
{"type": "...", "title": "...", "status": 401, "detail": "Invalid token", "correlationId": "..."}
</code></pre>

## Fallback: Fetch DigiKey Website

If API credentials are not available or authentication fails, search DigiKey by fetching product pages directly:

<pre><code>
https://www.digikey.com/en/products/result?keywords=<url-encoded-query>
</code></pre>

Examples:
- <code>https://www.digikey.com/en/products/result?keywords=GRM155R71C104KA88D</code> (by MPN)
- <code>https://www.digikey.com/en/products/result?keywords=100nF+0402+X7R+16V</code> (by specs)

Results from DigiKey can be noisy (JS-heavy pages). Look for the product table rows containing: DigiKey part number, MPN, description, unit price, stock quantity, and datasheet links. If results are truncated or empty, try searching by exact MPN rather than keywords.

## Datasheet Download & Analysis

DigiKey's API provides **direct PDF URLs** for datasheets — this is the preferred method for downloading datasheets because it avoids web scraping and returns reliable, stable links. Other skills (kicad, bom) should use DigiKey API as the first-choice datasheet source.

### Datasheet Directory Sync (Primary Workflow)

Use <code>sync_datasheets_digikey.py</code> to maintain a <code>datasheets/</code> directory alongside a KiCad project. It extracts components from the schematic, searches DigiKey for datasheet URLs, downloads missing PDFs, and writes an <code>manifest.json</code> manifest. Subsequent runs are incremental — only new or changed parts are fetched.

<pre><code>
# Sync datasheets for a KiCad project (creates datasheets/ next to the schematic)
python3 <skill-path>/scripts/sync_datasheets_digikey.py <file.kicad_sch>

# Preview what would be downloaded
python3 <skill-path>/scripts/sync_datasheets_digikey.py <file.kicad_sch> --dry-run

# Retry previously failed downloads
python3 <skill-path>/scripts/sync_datasheets_digikey.py <file.kicad_sch> --force

# Custom output directory
python3 <skill-path>/scripts/sync_datasheets_digikey.py <file.kicad_sch> -o ./my-datasheets

# Use pre-computed analyzer JSON instead of running the analyzer
python3 <skill-path>/scripts/sync_datasheets_digikey.py analyzer_output.json

# Parallel downloads (3 workers)
python3 <skill-path>/scripts/sync_datasheets_digikey.py <file.kicad_sch> --parallel 3

# Batch mode — sync from a plain MPN list (no KiCad project required)
python3 <skill-path>/scripts/sync_datasheets_digikey.py --mpn-list mpns.txt --output ./datasheets
</code></pre>

**MPN-list batch mode** (KH-312) — when you have a list of MPNs but no KiCad
project to point at (harness datasheet seeding, bulk seeding a new part
library). The file format is one MPN per line. Blank lines and <code>#</code>
comments (full-line and inline) are skipped. Non-MPN strings (generic
values like <code>100nF</code> or <code>DNP</code>) are filtered via <code>is_real_mpn()</code> and
de-duplicated. Output defaults to <code>./datasheets/</code> in the current working
directory when <code>--output</code> is omitted.

The script:
- **Runs the kicad schematic analyzer** automatically to extract components and MPNs
- **Filters generic passives** — skips entries without real MPNs (e.g., "100nF", "10K")
- **Tries schematic URLs first** — uses the datasheet URL embedded in the KiCad symbol before hitting the DigiKey API, saving API calls
- **Writes <code>manifest.json</code> manifest** — maps each MPN to its PDF file, manufacturer, description, download status, and URL. The kicad skill reads this during design review to cross-reference datasheets with the schematic.
- **Tracks failures** — failed downloads are recorded with error details and not retried on subsequent runs unless <code>--force</code> is used
- **Rate-limited** — 1 second between DigiKey API calls (configurable with <code>--delay</code>)
- **Saves progress incrementally** — if interrupted, already-downloaded files are preserved

The <code>manifest.json</code> manifest structure:
<pre><code>
{
  "schematic": "/path/to/file.kicad_sch",
  "last_sync": "2026-03-09T04:44:30+00:00",
  "parts": {
    "TPS61023DRLR": {
      "file": "TPS61023DRLR.pdf",
      "manufacturer": "Texas Instruments",
      "description": "Boost converter",
      "datasheet_url": "https://...",
      "status": "ok",
      "references": ["U3", "U2"],
      "size_bytes": 2392725
    }
  }
}
</code></pre>

### Single Datasheet Download

Use <code>fetch_datasheet_digikey.py</code> for one-off datasheet downloads. It handles manufacturer-specific quirks automatically.

<pre><code>
# Search by MPN (uses DigiKey API, requires credentials)
python3 <skill-path>/scripts/fetch_datasheet_digikey.py --search "TPS61023" -o datasheet.pdf

# Direct URL download
python3 <skill-path>/scripts/fetch_datasheet_digikey.py "https://www.ti.com/lit/gpn/tps61023" -o datasheet.pdf

# JSON output for script integration
python3 <skill-path>/scripts/fetch_datasheet_digikey.py --search "ADP1706" --json
</code></pre>

The script:
- **OS-agnostic** — uses Python <code>requests</code> library (no wget/curl dependency). Falls back to <code>urllib</code> if <code>requests</code> isn't installed.
- **Normalizes redirect URLs** — DigiKey's <code>DatasheetUrl</code> for TI parts points to a JS redirect page; the script extracts the direct PDF link. Also fixes protocol-relative <code>//mm.digikey.com/...</code> URLs.
- **Sets proper User-Agent** — many manufacturer sites (Nexperia, Lite-On, STMicro, Molex) block bare <code>urllib</code> or <code>curl</code> requests but serve PDFs fine with a browser User-Agent
- **Validates PDF headers** — rejects HTML error pages or Cloudflare challenge pages that masquerade as downloads
- **Falls back to alternative sources** — tries known URL patterns for Microchip when the primary URL fails
- **Headless browser fallback** — if <code>playwright</code> is installed, automatically uses a headless Chromium browser as a last resort for sites that serve PDFs via JavaScript (Broadcom doc viewer, Espressif download redirects). Intercepts download events and reads response bodies directly.
- **Exit codes**: 0 = success, 1 = download failed, 2 = search/API error
- **Dependencies**:
  - <code>pip install requests</code> (strongly recommended; urllib fallback can't handle HTTP/2 sites like analog.com)
  - <code>pip install playwright && playwright install chromium</code> (optional; enables headless browser fallback for JS-heavy sites)

### Manufacturer Compatibility

Tested against 240 components across 8 open-source KiCad projects (96% download success rate, 94% without Playwright):

| Manufacturer | Status | Notes |
|---|---|---|
| TI | Works | URL normalization strips JS redirect wrapper |
| ADI / Analog | Works | <code>requests</code> handles HTTP/2 transparently |
| STMicro | Works | Requires User-Agent header |
| Nexperia | Works | Requires User-Agent header |
| Lite-On | Works | Requires User-Agent header |
| Molex | Works | Requires User-Agent header |
| Renesas | Works | Direct download |
| ON Semi | Works | Direct download |
| NXP | Works | Direct download |
| Diodes Inc | Works | Direct download |
| Microchip | Works | Direct download via API URLs |
| YAGEO, Samsung, Murata | Works | DigiKey-hosted PDFs (<code>mm.digikey.com</code>) |
| Broadcom | Works* | Requires Playwright — <code>docs.broadcom.com</code> serves PDFs via JS download |
| Espressif | Works* | Requires Playwright — download redirect needs JS execution |
| Lattice | Mixed | Some URLs require cookies/auth |

\* Requires <code>playwright</code> package — falls back gracefully to user notification if not installed.

### When Download Fails

If the script or inline download fails (exit code 1), **tell the user and provide the URL** so they can open it in a real browser. Some manufacturer sites (Lattice, TDK InvenSense) require interactive login, cookies, or CAPTCHA that even a headless browser can't handle. With Playwright installed, Broadcom and Espressif now download automatically.

Example message to the user:
> I couldn't download the datasheet for ICE40UP5K-SG48I automatically — Lattice's site requires browser authentication. Here's the direct link:
> https://www.latticesemi.com/-/media/LatticeSemi/Documents/DataSheets/iCE/FPGA-DS-02008-1-9-iCE40-Ultra-Plus-Family-Data-Sheet.ashx
>
> You can open it in your browser and save it locally, then I can read and analyze it.

The <code>--json</code> output always includes the <code>datasheet_url</code> field even on failure, so you can extract the URL programmatically.

### Manual Download Workflow

If the script isn't available or you need to do it inline:

1. **Search for the part** using KeywordSearch or ProductDetails
2. **Extract <code>DatasheetUrl</code>** from the API response
3. **Normalize the URL** — if it starts with <code>//</code>, prepend <code>https:</code>. If it contains <code>ti.com/general/docs/suppproductinfo</code>, extract the <code>gotoUrl</code> query parameter.
4. **Download with <code>requests</code>** (Python) or <code>wget</code>/<code>curl</code> with a browser User-Agent
5. **Verify it's a PDF**: first 4 bytes should be <code>%PDF</code>

If the <code>DatasheetUrl</code> field is empty or all download methods fail:
- Provide the URL to the user for manual browser download
- Try the <code>/products/v4/search/{pn}/media</code> endpoint for alternative media links
- Web search as a last resort: <code>"<MPN> datasheet filetype:pdf"</code>

### What to Extract from Datasheets

When analyzing a datasheet for a KiCad design review (see <code>kicad</code> skill):
- **Absolute maximum ratings** — voltage, current, temperature limits
- **Recommended operating conditions** — typical operating ranges
- **Pinout and pin descriptions** — verify against KiCad symbol
- **Package dimensions** — verify against KiCad footprint
- **Typical application circuit** — compare against the user's schematic
- **Thermal characteristics** — θJA, θJC for power dissipation calculations
- **Electrical characteristics** — key parameters (Vout, Iq, PSRR, etc.)

## Tips

- DigiKey PN suffixes: <code>-ND</code> standard, <code>-1-ND</code> cut tape, <code>-2-ND</code> digi-reel, <code>-6-ND</code> full reel
- Use <code>ExcludeMarketPlace</code> filter to avoid third-party seller listings
- Price breaks in <code>ProductVariations[].StandardPricing[]</code> — check <code>BreakQuantity</code> thresholds
- Check <code>ProductStatus</code> and <code>Discontinued</code>/<code>EndOfLife</code> before selecting parts
