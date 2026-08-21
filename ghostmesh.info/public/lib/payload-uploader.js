/**
 * GhostMesh payload uploader — the browser half of the `/put` protocol (stop-and-wait).
 *
 * A Meshtastic node exposes only its PROTO text channel over serial (the same one this page does
 * want_config over), so a file rides that channel: base64-chunked TEXT_MESSAGE_APP commands the
 * firmware's CommandModule reassembles to LittleFS and verifies with a CRC32.
 *
 * WHY stop-and-wait: a rapid burst of self-addressed packets overruns the node's serial ingest and
 * every packet after the first is dropped. So we send ONE chunk and wait for the node's ACK
 * (`PUT <fid> a <idx>`) before sending the next. Each chunk is individually confirmed; nothing
 * bursts. The node's ack is self-addressed, so a USB transfer stays entirely off the air.
 *
 *   putUploaderInit({ sendCmd, nodeIdHex, log }) -> { element, onReply(text) }
 *     sendCmd(text)  — send one addressed text command; returns a promise that resolves on write
 *     nodeIdHex      — this node's last-4 hex id (targeting)
 *     log(msg, cls)  — optional: echo status into the page trace
 *     onReply(text)  — feed every "PUT ..." reply line here so the transfer can advance/verify
 */
export function putUploaderInit({ sendCmd, nodeIdHex, log }) {
  const CHUNK = 132;        // raw bytes per chunk (base64 = 176 chars, no padding)
  const ACK_TIMEOUT = 2500; // ms to wait for a chunk ack before resending it
  const MAX_RETRIES = 8;    // per-chunk resend attempts before giving up
  log = log || function () {};

  // ── UI ──
  const el = document.createElement('div');
  el.innerHTML = `
    <label class="pu-pick">
      <input type="file" class="pu-file" style="display:none" />
      <span class="pu-choose">[ CHOOSE FILE ]</span>
      <span class="pu-name">no file selected</span>
    </label>
    <div class="pu-bar" style="display:none"><div class="pu-fill"></div></div>
    <div class="pu-status">Ready.</div>
    <button class="pu-send" disabled>[ UPLOAD ]</button>
  `;
  const style = document.createElement('style');
  style.textContent = `
    .pu-pick { display:flex; align-items:center; gap:0.8rem; cursor:pointer; margin:0.4rem 0; flex-wrap:wrap; }
    .pu-choose { color:var(--blue); letter-spacing:1px; font-size:0.85rem; }
    .pu-name { color:var(--text-dim); font-size:0.8rem; }
    .pu-bar { height:6px; background:rgba(120,160,140,0.18); margin:0.7rem 0; overflow:hidden; }
    .pu-fill { height:100%; width:0%; background:linear-gradient(90deg,var(--blue),var(--terminal)); transition:width .1s; }
    .pu-status { font-size:0.78rem; color:var(--text-dim); letter-spacing:0.5px; min-height:1.1em; margin:0.4rem 0; }
    .pu-status.ok { color:var(--terminal); }
    .pu-status.err { color:#e88; }
    .pu-send { background:transparent; border:1px solid var(--terminal); color:var(--terminal);
      font-family:inherit; font-size:0.85rem; padding:0.45rem 1rem; cursor:pointer; letter-spacing:1px; }
    .pu-send:disabled { border-color:var(--text-dim); color:var(--text-dim); cursor:not-allowed; opacity:0.6; }
  `;
  el.appendChild(style);

  const fileInput = el.querySelector('.pu-file');
  const nameEl = el.querySelector('.pu-name');
  const barEl = el.querySelector('.pu-bar');
  const fillEl = el.querySelector('.pu-fill');
  const statusEl = el.querySelector('.pu-status');
  const sendBtn = el.querySelector('.pu-send');

  function status(msg, cls) { statusEl.textContent = msg; statusEl.className = 'pu-status' + (cls ? ' ' + cls : ''); }
  function progress(frac) { barEl.style.display = 'block'; fillEl.style.width = Math.round(frac * 100) + '%'; }

  // ── CRC32 (zlib/PNG polynomial) — must match the firmware's crc32_update ──
  const CRC_TABLE = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1); t[n] = c >>> 0; }
    return t;
  })();
  function crc32(u8) {
    let c = 0xFFFFFFFF;
    for (let i = 0; i < u8.length; i++) c = CRC_TABLE[(c ^ u8[i]) & 0xFF] ^ (c >>> 8);
    return (c ^ 0xFFFFFFFF) >>> 0;
  }
  function b64(u8, start, end) {
    let s = '';
    for (let i = start; i < end; i++) s += String.fromCharCode(u8[i]);
    return btoa(s);
  }

  // ── reply plumbing: replies are BUFFERED as state, not caught by a one-shot waiter ──
  // Meshtastic double-delivers self-addressed packets, so acks stream in — often in the gap between
  // one chunk's send and the next wait being armed. A one-shot waiter drops anything landing in that
  // gap, forcing a full ACK_TIMEOUT + resend per chunk (the "0.0 KB/s" crawl). Recording the latest
  // ack / verdict as plain state means nothing is ever missed; the transfer just polls the state.
  let fidHex = '', lastAck = -1, gotReady = false, endVerdict = null;
  function onReply(text) {
    const m = text.trim().match(/^PUT\s+([0-9a-fA-F]+)\s+([a-zA-Z]+)(?:\s+(.*))?$/);
    if (!m || m[1].toLowerCase() !== fidHex.toLowerCase()) return false;
    const verb = m[2].toLowerCase(), rest = m[3] || '';
    if (verb === 'a') { const i = parseInt(rest, 10); if (!isNaN(i) && i > lastAck) lastAck = i; }
    else if (verb === 'ready') gotReady = true;
    else if (verb === 'ok' || verb === 'crcfail' || verb === 'sizefail' || verb === 'need') endVerdict = { verb, rest };
    return true;
  }
  function sleep(ms) { return new Promise((r) => setTimeout(r, ms)); }
  async function waitFor(pred, timeoutMs) {
    const deadline = performance.now() + timeoutMs;
    while (!pred()) { if (performance.now() > deadline) return false; await sleep(15); }
    return true;
  }

  // ── file selection ──
  let file = null, bytes = null, sending = false;
  fileInput.addEventListener('change', (e) => {
    file = e.target.files[0];
    if (!file) return;
    nameEl.textContent = `${file.name} — ${(file.size / 1024).toFixed(1)} KB`;
    sendBtn.disabled = false;
    status('Ready.');
  });
  sendBtn.addEventListener('click', () => { if (!sending) start().catch((e) => fail(e.message || String(e))); });

  function fail(reason) {
    sending = false; sendBtn.disabled = false;
    status('Upload failed: ' + reason, 'err');
    log('PUT fail: ' + reason, 'gm-err');
  }

  async function start() {
    sending = true; sendBtn.disabled = true;
    bytes = new Uint8Array(await file.arrayBuffer());
    const nchunks = Math.ceil(bytes.length / CHUNK);
    const crc = crc32(bytes);
    lastAck = -1; gotReady = false; endVerdict = null;  // reset buffered reply state for this transfer
    fidHex = Math.floor(Math.random() * 0xffff).toString(16);
    const name = file.name.replace(/[\s\\/]+/g, '_');

    log(`PUT begin ${fidHex} ${name} (${bytes.length}B / ${nchunks} chunks)`, 'gm-tx');
    status(`Opening on node…`);
    await sendCmd(`/put @${nodeIdHex} begin ${fidHex} ${nchunks} ${bytes.length} ${crc.toString(16)} ${name}`);
    if (!await waitFor(() => gotReady, 4000)) return fail('node did not acknowledge begin');

    // Stop-and-wait, but the node acks the HIGHEST CONTIGUOUS index it holds, so we just poll
    // lastAck. Duplicate/stale acks (Meshtastic double-delivers) only ever move lastAck forward or
    // leave it — never a problem. A chunk is resent only if lastAck fails to reach it in time.
    const t0 = performance.now();
    let nextIdx = 0, retries = 0;
    while (nextIdx < nchunks) {
      const s = nextIdx * CHUNK, e = Math.min(s + CHUNK, bytes.length);
      await sendCmd(`/put @${nodeIdHex} d ${fidHex} ${nextIdx} ${b64(bytes, s, e)}`);
      if (!await waitFor(() => lastAck >= nextIdx, ACK_TIMEOUT)) {
        if (++retries > MAX_RETRIES) return fail(`no ack for chunk ${nextIdx}`);
        continue; // resend the same chunk
      }
      nextIdx = lastAck + 1;
      retries = 0;
      const kbps = ((nextIdx * CHUNK) / 1024) / (Math.max(1, performance.now() - t0) / 1000);
      progress(nextIdx / nchunks);
      status(`Sending… ${Math.round((nextIdx / nchunks) * 100)}%  (${kbps.toFixed(1)} KB/s)`);
    }

    // Close out. `end` can be dropped like any packet, so retry until the node returns a verdict.
    status('Verifying on node…');
    for (let endTry = 0; endTry < 5 && !endVerdict; endTry++) {
      await sendCmd(`/put @${nodeIdHex} end ${fidHex}`);
      await waitFor(() => endVerdict, 3000);
    }
    if (!endVerdict) return fail('no confirmation from node');
    if (endVerdict.verb === 'ok') {
      sending = false; sendBtn.disabled = false;
      progress(1);
      status(`Uploaded ${endVerdict.rest} bytes → /ghostmesh/${file.name}`, 'ok');
      log(`PUT ok ${endVerdict.rest} bytes`, 'gm-rx');
    } else {
      return fail(endVerdict.verb + (endVerdict.rest ? ' ' + endVerdict.rest : ''));
    }
  }

  return { element: el, onReply };
}
