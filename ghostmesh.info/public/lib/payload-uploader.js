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

  // ── reply plumbing: the transfer awaits specific "PUT <fid> ..." replies ──
  let fidHex = '', pending = null; // pending = { match(verb,rest)->value|undefined, resolve, timer }
  function waitReply(match, timeoutMs) {
    return new Promise((resolve) => {
      const timer = setTimeout(() => { if (pending && pending.resolve === resolve) { pending = null; resolve(null); } }, timeoutMs);
      pending = { match, resolve, timer };
    });
  }
  function onReply(text) {
    const m = text.trim().match(/^PUT\s+([0-9a-fA-F]+)\s+([a-zA-Z]+)(?:\s+(.*))?$/);
    if (!m || m[1].toLowerCase() !== fidHex.toLowerCase()) return false;
    const verb = m[2].toLowerCase(), rest = m[3] || '';
    if (pending) {
      const v = pending.match(verb, rest);
      if (v !== undefined) { clearTimeout(pending.timer); const r = pending.resolve; pending = null; r(v); }
    }
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
    fidHex = Math.floor(Math.random() * 0xffff).toString(16);
    const name = file.name.replace(/[\s\\/]+/g, '_');

    log(`PUT begin ${fidHex} ${name} (${bytes.length}B / ${nchunks} chunks)`, 'gm-tx');
    status(`Opening on node…`);
    await sendCmd(`/put @${nodeIdHex} begin ${fidHex} ${nchunks} ${bytes.length} ${crc.toString(16)} ${name}`);
    const ready = await waitReply((verb) => verb === 'ready' ? true : undefined, 4000);
    if (!ready) return fail('node did not acknowledge begin');

    // Wait for an ack of at least `target`, DRAINING stale/duplicate acks. Meshtastic delivers each
    // self-addressed packet to the module twice, so the node acks every chunk twice; the second ack
    // is for an index we've already passed. Ignoring those (instead of treating them as failures) is
    // what keeps stop-and-wait in sync. Returns the ack index, or null on timeout.
    async function waitAckAtLeast(target, timeoutMs) {
      const deadline = performance.now() + timeoutMs;
      for (;;) {
        const remaining = deadline - performance.now();
        if (remaining <= 0) return null;
        const a = await waitReply((verb, rest) => verb === 'a' ? parseInt(rest, 10) : undefined, remaining);
        if (a === null) return null;            // real timeout — the chunk (or its ack) was lost
        if (!isNaN(a) && a >= target) return a; // fresh ack
        // else a < target: a stale duplicate ack — ignore and keep waiting
      }
    }

    const t0 = performance.now();
    let nextIdx = 0, retries = 0;
    while (nextIdx < nchunks) {
      const s = nextIdx * CHUNK, e = Math.min(s + CHUNK, bytes.length);
      await sendCmd(`/put @${nodeIdHex} d ${fidHex} ${nextIdx} ${b64(bytes, s, e)}`);
      const acked = await waitAckAtLeast(nextIdx, ACK_TIMEOUT);
      if (acked === null) {
        if (++retries > MAX_RETRIES) return fail(`no ack for chunk ${nextIdx}`);
        continue; // resend the same chunk
      }
      nextIdx = acked + 1;
      retries = 0;
      const kbps = ((nextIdx * CHUNK) / 1024) / (Math.max(1, performance.now() - t0) / 1000);
      progress(nextIdx / nchunks);
      status(`Sending… ${Math.round((nextIdx / nchunks) * 100)}%  (${kbps.toFixed(1)} KB/s)`);
    }

    // Close out. `end` can be dropped (or double-delivered) like any packet, so retry until the node
    // confirms. A repeated end after success returns 'noxfer' (harmless) — we only accept a verdict.
    status('Verifying on node…');
    let res = null;
    for (let endTry = 0; endTry < 5 && !res; endTry++) {
      await sendCmd(`/put @${nodeIdHex} end ${fidHex}`);
      res = await waitReply((verb, rest) =>
        (verb === 'ok' || verb === 'crcfail' || verb === 'sizefail' || verb === 'need') ? { verb, rest } : undefined, 3000);
    }
    if (!res) return fail('no confirmation from node');
    if (res.verb === 'ok') {
      sending = false; sendBtn.disabled = false;
      progress(1);
      status(`Uploaded ${res.rest} bytes → /ghostmesh/${file.name}`, 'ok');
      log(`PUT ok ${res.rest} bytes`, 'gm-rx');
    } else {
      return fail(res.verb + (res.rest ? ' ' + res.rest : ''));
    }
  }

  return { element: el, onReply };
}
