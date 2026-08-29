/**
 * GhostMesh firmware flasher — Web Serial + esptool-js.
 *
 * Standalone: it manages its OWN serial port (esptool resets the ESP32-S3 into its ROM bootloader,
 * which takes over the port), so it does NOT share the configurator's live PROTO connection and
 * works even when the node isn't talking yet — which is exactly when you need to reflash.
 *
 * esptool-js is a real ES module (there is no `ESPLoader` global from a <script src> — that was the
 * first bug). We import it from a locally-bundled copy: esm.sh mis-handled esptool-js's JSON stub
 * imports (`await import('./stub.json')` → the `.text` base64 came back undefined → atob threw
 * "not correctly encoded"). esbuild inlines JSON with proper named exports, so the self-hosted
 * bundle loads the flasher stub correctly. Rebuild it with:
 *   echo "export { ESPLoader, Transport } from 'esptool-js';" > e.mjs
 *   node_modules/.bin/esbuild e.mjs --bundle --format=esm --platform=browser \
 *     --loader:.json=json --minify --outfile=public/lib/esptool-bundle.js
 *
 *   flasherInit({ hostedUrl, hostedLabel, log }) -> DOM element
 */
import { ESPLoader, Transport } from '/lib/esptool-bundle.js';

export function flasherInit({ hostedUrl, hostedLabel, log }) {
  log = log || function () {};

  const el = document.createElement('div');
  el.innerHTML = `
    <div class="fl-row">
      <select class="fl-src">
        <option value="hosted">${hostedLabel || 'GhostMesh firmware (latest)'}</option>
        <option value="custom">Custom .bin…</option>
      </select>
      <label class="fl-filewrap" style="display:none">
        <input type="file" class="fl-file" accept=".bin" style="display:none" />
        <span class="fl-choose">[ CHOOSE .bin ]</span>
        <span class="fl-fname"></span>
      </label>
    </div>
    <div class="fl-bar" style="display:none"><div class="fl-fill"></div></div>
    <div class="fl-status">Ready.</div>
    <button class="fl-flash">[ FLASH FIRMWARE ]</button>
  `;
  const style = document.createElement('style');
  style.textContent = `
    .fl-row { display:flex; align-items:center; gap:0.8rem; flex-wrap:wrap; margin:0.4rem 0; }
    .fl-src { background:#0d1224; color:var(--text); border:1px solid var(--text-dim); font-family:inherit;
      font-size:0.72rem; letter-spacing:0.08em; padding:0.5rem 2rem 0.5rem 0.7rem; cursor:pointer;
      appearance:none; -webkit-appearance:none;
      background-image:linear-gradient(45deg,transparent 50%,var(--text-dim) 50%),linear-gradient(135deg,var(--text-dim) 50%,transparent 50%);
      background-position:calc(100% - 14px) center, calc(100% - 9px) center; background-size:5px 5px,5px 5px; background-repeat:no-repeat;
      transition:border-color .15s; }
    .fl-src:hover { border-color:var(--blue); }
    .fl-choose { display:inline-block; border:1px solid var(--text-dim); color:var(--text);
      font-family:inherit; letter-spacing:0.15em; text-transform:uppercase; font-size:0.64rem;
      padding:0.45rem 0.95rem; cursor:pointer; transition:border-color .15s, color .15s; }
    .fl-filewrap:hover .fl-choose { border-color:var(--blue); color:var(--blue); }
    .fl-fname { color:var(--text-dim); font-size:0.78rem; margin-left:0.4rem; letter-spacing:0.03em; }
    .fl-bar { height:6px; background:rgba(120,160,140,0.18); margin:0.7rem 0; overflow:hidden;
      border:1px solid var(--text-ghost); }
    .fl-fill { height:100%; width:0%; background:linear-gradient(90deg,var(--blue),var(--terminal)); transition:width .1s; }
    .fl-status { font-size:0.78rem; color:var(--text-dim); letter-spacing:0.5px; min-height:1.1em; margin:0.4rem 0; }
    .fl-status.ok { color:var(--terminal); }
    .fl-status.err { color:#e88; }
    .fl-flash { background:transparent; border:1px solid var(--red); color:var(--red); font-family:inherit;
      font-size:0.7rem; letter-spacing:0.2em; text-transform:uppercase; padding:0.55rem 1.2rem; cursor:pointer;
      transition:background .15s, color .15s, box-shadow .15s; }
    .fl-flash:hover:not(:disabled) { background:var(--red); color:#150406; box-shadow:0 0 14px rgba(255,34,68,0.35); }
    .fl-flash:disabled { border-color:var(--text-dim); color:var(--text-dim); cursor:not-allowed; opacity:0.6; }
  `;
  el.appendChild(style);

  const srcSel = el.querySelector('.fl-src');
  const fileWrap = el.querySelector('.fl-filewrap');
  const fileInput = el.querySelector('.fl-file');
  const fnameEl = el.querySelector('.fl-fname');
  const barEl = el.querySelector('.fl-bar');
  const fillEl = el.querySelector('.fl-fill');
  const statusEl = el.querySelector('.fl-status');
  const flashBtn = el.querySelector('.fl-flash');

  function status(msg, cls) { statusEl.textContent = msg; statusEl.className = 'fl-status' + (cls ? ' ' + cls : ''); }
  function progress(frac) { barEl.style.display = 'block'; fillEl.style.width = Math.round(frac * 100) + '%'; }

  let customFile = null;
  srcSel.addEventListener('change', () => {
    fileWrap.style.display = srcSel.value === 'custom' ? 'inline-flex' : 'none';
  });
  fileInput.addEventListener('change', (e) => { customFile = e.target.files[0]; fnameEl.textContent = customFile ? customFile.name : ''; });

  // Resolve the firmware bytes from either the hosted binary or the user's file.
  async function firmwareBytes() {
    if (srcSel.value === 'custom') {
      if (!customFile) throw new Error('choose a .bin file first');
      return new Uint8Array(await customFile.arrayBuffer());
    }
    const r = await fetch(hostedUrl, { cache: 'no-store' });
    if (!r.ok) throw new Error('hosted firmware fetch failed (' + r.status + ')');
    return new Uint8Array(await r.arrayBuffer());
  }

  flashBtn.addEventListener('click', async () => {
    if (!('serial' in navigator)) { status('Web Serial unavailable — use Chrome/Edge.', 'err'); return; }
    flashBtn.disabled = true;
    let transport = null;
    try {
      status('Reading firmware…');
      const data = await firmwareBytes();
      log(`flash: ${data.length} bytes from ${srcSel.value}`, 'gm-tx');

      status('Select the node in the browser dialog…');
      const port = await navigator.serial.requestPort();

      // esptool owns the port: Transport wraps it, ESPLoader.main() resets to the ROM bootloader,
      // syncs, and returns the chip name. terminal.* routes esptool's own logging to our trace.
      transport = new Transport(port, false);
      const esploader = new ESPLoader({
        transport,
        baudrate: 921600,     // flash-time speed; negotiated up from the ROM baud
        romBaudrate: 115200,
        terminal: { clean() {}, writeLine(d) { log(d, 'gm-dim'); }, write() {} },
      });

      status('Connecting to bootloader…');
      const chip = await esploader.main();
      status(`Detected ${chip}. Writing…`);
      log(`flash: detected ${chip}`, 'gm-rx');

      await esploader.writeFlash({
        fileArray: [{ data, address: 0 }], // factory image = bootloader+partitions+app, flashes at 0x0
        flashMode: 'keep',
        flashFreq: 'keep',
        flashSize: 'keep',
        eraseAll: false,     // keep NVS + LittleFS (channel keys, config) — no re-pair needed
        compress: true,
        reportProgress: (idx, written, total) => {
          progress(written / total);
          status(`Writing… ${Math.round((written / total) * 100)}%`);
        },
      });

      status('Resetting node…');
      await esploader.after(); // hard reset into the new firmware
      progress(1);
      status('✓ Flash complete — node rebooting into new firmware.', 'ok');
      log('flash: complete', 'gm-rx');
    } catch (e) {
      status('Flash failed: ' + (e && e.message ? e.message : e), 'err');
      log('flash fail: ' + (e && e.message ? e.message : e), 'gm-err');
    } finally {
      try { if (transport) await transport.disconnect(); } catch (_) {}
      flashBtn.disabled = false;
    }
  });

  return el;
}
