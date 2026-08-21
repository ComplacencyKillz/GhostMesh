/**
 * GhostMesh payload uploader — the browser half of the `/put` protocol.
 *
 * A Meshtastic node exposes only its PROTO text channel over serial (the same one this page does
 * want_config over), so a file rides that channel: base64-chunked TEXT_MESSAGE_APP commands the
 * firmware's CommandModule reassembles to LittleFS and verifies with a CRC32. USB is just the fast,
 * reliable case; the identical protocol works over the mesh.
 *
 *   putUploaderInit({ sendCmd, nodeIdHex, log }) -> { element, onReply(text) }
 *     sendCmd(text)  — send one addressed text command to the node (caller PROTO-frames it)
 *     nodeIdHex      — this node's last-4 hex id (targeting)
 *     log(msg, cls)  — optional: echo status into the page trace
 *     onReply(text)  — feed every "PUT ..." reply line here so the transfer can verify/resume
 */
export function putUploaderInit({ sendCmd, nodeIdHex, log }) {
  const CHUNK = 132;      // must match firmware PUT_CHUNK_BYTES (132 -> 176 base64 chars, no padding)
  const PACE_MS = 25;     // gap between data chunks; the node's serial RX + flash write is the limit
  const MAX_ROUNDS = 6;   // resend attempts before giving up
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
  function progress(pct) { barEl.style.display = 'block'; fillEl.style.width = pct + '%'; }

  // ── CRC32 (zlib/PNG polynomial) — must match the firmware's crc32_update ──
  const CRC_TABLE = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1); t[n] = c >>> 0; }
    return t;
  })();
  function crc32(bytes) {
    let c = 0xFFFFFFFF;
    for (let i = 0; i < bytes.length; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xFF] ^ (c >>> 8);
    return (c ^ 0xFFFFFFFF) >>> 0;
  }
  function b64(u8, start, end) {
    let s = '';
    for (let i = start; i < end; i++) s += String.fromCharCode(u8[i]);
    return btoa(s);
  }
  function sleep(ms) { return new Promise((r) => setTimeout(r, ms)); }

  // ── transfer state ──
  let file = null, bytes = null, fidHex = '', nchunks = 0, round = 0, waiting = null, sending = false;

  fileInput.addEventListener('change', (e) => {
    file = e.target.files[0];
    if (!file) return;
    nameEl.textContent = `${file.name} — ${(file.size / 1024).toFixed(1)} KB`;
    sendBtn.disabled = false;
    status('Ready.');
  });

  sendBtn.addEventListener('click', () => { if (!sending) start(); });

  function sendChunk(i) {
    const s = i * CHUNK, e = Math.min(s + CHUNK, bytes.length);
    sendCmd(`/put @${nodeIdHex} d ${fidHex} ${i} ${b64(bytes, s, e)}`);
  }

  async function start() {
    sending = true; sendBtn.disabled = true; round = 0;
    bytes = new Uint8Array(await file.arrayBuffer());
    nchunks = Math.ceil(bytes.length / CHUNK);
    const crc = crc32(bytes);
    fidHex = Math.floor(Math.random() * 0xffff).toString(16);
    // The node tokenizes the begin line on spaces and the name is its last field, so collapse any
    // whitespace (and strip path separators) to a single flat token.
    const name = file.name.replace(/[\s\\/]+/g, '_');

    log(`PUT begin ${fidHex} ${name} (${bytes.length}B / ${nchunks} chunks)`, 'gm-tx');
    status(`Sending ${nchunks} chunks…`);
    sendCmd(`/put @${nodeIdHex} begin ${fidHex} ${nchunks} ${bytes.length} ${crc.toString(16)} ${name}`);
    await sleep(200); // let the node open the file before the flood

    const t0 = performance.now();
    for (let i = 0; i < nchunks; i++) {
      sendChunk(i);
      if ((i & 7) === 0 || i === nchunks - 1) {
        const pct = Math.round(((i + 1) / nchunks) * 100);
        const kbps = (((i + 1) * CHUNK) / 1024) / ((performance.now() - t0) / 1000);
        progress(pct);
        status(`Sending… ${pct}%  (${kbps.toFixed(1)} KB/s)`);
      }
      await sleep(PACE_MS);
    }
    finishRound();
  }

  function finishRound() {
    status('Verifying on node…');
    sendCmd(`/put @${nodeIdHex} end ${fidHex}`);
    waiting = true;
  }

  // Feed every "PUT ..." reply here. Returns true if it consumed the line.
  function onReply(text) {
    if (!waiting) return false;
    const m = text.trim().match(/^PUT\s+([0-9a-fA-F]+)\s+(\w+)(?:\s+(.*))?$/);
    if (!m || m[1].toLowerCase() !== fidHex.toLowerCase()) return false;
    const verb = m[2].toLowerCase(), rest = m[3] || '';

    if (verb === 'ready') return true; // begin ack — data already streaming
    if (verb === 'ok') {
      waiting = false; sending = false; sendBtn.disabled = false;
      progress(100);
      status(`Uploaded ${rest} bytes → /ghostmesh/${file.name}`, 'ok');
      log(`PUT ok ${rest} bytes`, 'gm-rx');
      return true;
    }
    if (verb === 'need') {
      if (++round > MAX_ROUNDS) { fail('too many missing chunks — link too lossy'); return true; }
      const idxs = rest.split(',').map((x) => parseInt(x, 10)).filter((x) => !isNaN(x));
      status(`Resending ${idxs.length} chunk(s) (round ${round})…`);
      log(`PUT need ${idxs.length} — resending`, 'gm-tx');
      resend(idxs);
      return true;
    }
    // toobig / nospace / crcfail / sizefail / timeout / noxfer / *-fail
    fail(verb + (rest ? ' ' + rest : ''));
    return true;
  }

  async function resend(idxs) {
    waiting = false;
    for (const i of idxs) { if (i >= 0 && i < nchunks) { sendChunk(i); await sleep(PACE_MS); } }
    finishRound();
  }

  function fail(reason) {
    waiting = false; sending = false; sendBtn.disabled = false;
    status('Upload failed: ' + reason, 'err');
    log('PUT fail: ' + reason, 'gm-err');
  }

  return { element: el, onReply };
}
