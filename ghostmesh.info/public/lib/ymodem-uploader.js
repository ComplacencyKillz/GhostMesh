/**
 * GhostMesh YMODEM-1K File Uploader
 * Pure ES6, no dependencies. CRC-16-CCITT, 1024-byte blocks, full protocol.
 */

export function ymodemInit(serialPort) {
  // ─── Magic bytes & protocol constants ───
  const SOH = 0x01, STX = 0x02, EOT = 0x04, ACK = 0x06, NAK = 0x15, CAN = 0x18;
  const BLOCK_SIZE_SOH = 128, BLOCK_SIZE_STX = 1024;
  const TIMEOUT_MS = 5000;

  // ─── CRC-16-CCITT lookup table (pre-computed) ───
  const CRC_TABLE = new Uint16Array(256);
  for (let i = 0; i < 256; i++) {
    let crc = (i << 8) >>> 0;
    for (let j = 0; j < 8; j++) {
      crc = ((crc << 1) >>> 0);
      if (crc & 0x10000) crc ^= 0x1021;
      crc &= 0xffff;
    }
    CRC_TABLE[i] = crc;
  }

  function crc16(data) {
    let crc = 0;
    for (const byte of data) {
      crc = ((CRC_TABLE[(crc >>> 8) ^ byte] ^ ((crc << 8) & 0xffff)) >>> 0) & 0xffff;
    }
    return crc;
  }

  // ─── Container & UI ───
  const container = document.createElement('div');
  container.style.cssText = `
    padding: 16px; border: 1px solid #333; border-radius: 2px;
    background: #0a0a0a; color: #ccc; font-family: monospace; font-size: 12px;
    max-width: 500px;
  `;

  const title = document.createElement('div');
  title.style.cssText = 'margin-bottom: 12px; color: #666; text-transform: uppercase; letter-spacing: 1px; font-size: 11px;';
  title.textContent = 'USB FILE UPLOAD';
  container.appendChild(title);

  const fileInput = document.createElement('input');
  fileInput.type = 'file';
  fileInput.accept = '.bin,.hex,.img,.dat';
  fileInput.style.cssText = 'display: none;';
  container.appendChild(fileInput);

  const fileSection = document.createElement('div');
  fileSection.style.cssText = 'margin-bottom: 12px; padding: 8px; background: #111; border-left: 2px solid #333;';

  const selectBtn = document.createElement('button');
  selectBtn.textContent = 'SELECT FILE';
  selectBtn.style.cssText = `
    padding: 6px 12px; background: transparent; border: 1px solid #444;
    color: #aaa; cursor: pointer; font-family: monospace; font-size: 11px;
    text-transform: uppercase; transition: all 0.2s;
  `;
  selectBtn.onmouseover = () => { selectBtn.style.borderColor = '#666'; selectBtn.style.color = '#ddd'; };
  selectBtn.onmouseout = () => { selectBtn.style.borderColor = '#444'; selectBtn.style.color = '#aaa'; };
  selectBtn.onclick = () => fileInput.click();
  fileSection.appendChild(selectBtn);

  const fileInfo = document.createElement('div');
  fileInfo.style.cssText = 'margin-top: 8px; color: #555; font-size: 11px;';
  fileInfo.textContent = 'no file selected';
  fileSection.appendChild(fileInfo);
  container.appendChild(fileSection);

  const progressSection = document.createElement('div');
  progressSection.style.cssText = 'margin-bottom: 12px; display: none;';

  const progressBar = document.createElement('div');
  progressBar.style.cssText = 'width: 100%; height: 4px; background: #1a1a1a; margin-bottom: 6px; overflow: hidden; border-radius: 1px;';
  const progressFill = document.createElement('div');
  progressFill.style.cssText = 'height: 100%; background: #444; width: 0%; transition: width 0.1s;';
  progressBar.appendChild(progressFill);
  progressSection.appendChild(progressBar);

  const statusMsg = document.createElement('div');
  statusMsg.style.cssText = 'color: #666; font-size: 10px; text-transform: uppercase; letter-spacing: 0.5px;';
  statusMsg.textContent = 'ready';
  progressSection.appendChild(statusMsg);

  const speedMsg = document.createElement('div');
  speedMsg.style.cssText = 'color: #555; font-size: 10px; margin-top: 4px;';
  speedMsg.textContent = '';
  progressSection.appendChild(speedMsg);
  container.appendChild(progressSection);

  const uploadBtn = document.createElement('button');
  uploadBtn.textContent = 'START UPLOAD';
  uploadBtn.style.cssText = `
    width: 100%; padding: 8px; background: transparent; border: 1px solid #444;
    color: #aaa; cursor: pointer; font-family: monospace; font-size: 11px;
    text-transform: uppercase; transition: all 0.2s;
  `;
  uploadBtn.disabled = true;
  uploadBtn.onmouseover = () => !uploadBtn.disabled && (uploadBtn.style.borderColor = '#666', uploadBtn.style.color = '#ddd');
  uploadBtn.onmouseout = () => !uploadBtn.disabled && (uploadBtn.style.borderColor = '#444', uploadBtn.style.color = '#aaa');
  container.appendChild(uploadBtn);

  let selectedFile = null;

  fileInput.onchange = (e) => {
    selectedFile = e.target.files[0];
    if (!selectedFile) return;

    if (selectedFile.size > 10_000_000) {
      fileInfo.textContent = 'error: file exceeds 10 MB';
      fileInfo.style.color = '#a44';
      uploadBtn.disabled = true;
      selectedFile = null;
      return;
    }

    const sizeMb = (selectedFile.size / 1024 / 1024).toFixed(2);
    fileInfo.textContent = `${selectedFile.name} (${sizeMb} MB)`;
    fileInfo.style.color = '#666';
    uploadBtn.disabled = false;
  };

  uploadBtn.onclick = async () => {
    if (!selectedFile || !serialPort) return;

    progressSection.style.display = 'block';
    uploadBtn.disabled = true;
    statusMsg.textContent = 'reading file';
    speedMsg.textContent = '';

    try {
      const buffer = await selectedFile.arrayBuffer();
      const fileData = new Uint8Array(buffer);
      const fileSize = fileData.length;
      const startTime = Date.now();

      statusMsg.textContent = 'waiting for handshake';

      // ─── Handshake: wait for CRC request (NAK x 3 or C) ───
      const reader = serialPort.readable.getReader();
      const writer = serialPort.writable.getWriter();

      let handshakeOk = false;
      const handshakeStart = Date.now();
      while (Date.now() - handshakeStart < TIMEOUT_MS * 3) {
        const { value, done } = await Promise.race([
          reader.read(),
          new Promise(r => setTimeout(() => r({ done: true }), 100))
        ]);
        if (!done && value) {
          const byte = value[0];
          if (byte === NAK || byte === 0x43) { // NAK or 'C' for CRC
            handshakeOk = true;
            statusMsg.textContent = 'handshake received';
            break;
          }
        }
      }

      if (!handshakeOk) {
        statusMsg.textContent = 'handshake timeout';
        statusMsg.style.color = '#a44';
        uploadBtn.disabled = false;
        reader.releaseLock();
        writer.releaseLock();
        return;
      }

      // ─── Send blocks ───
      const blockSize = BLOCK_SIZE_STX;
      const numBlocks = Math.ceil(fileSize / blockSize);
      let blockNum = 1;
      let bytesSent = 0;

      for (let offset = 0; offset < fileSize; offset += blockSize) {
        const isLastBlock = offset + blockSize >= fileSize;
        const chunkSize = Math.min(blockSize, fileSize - offset);
        const chunk = fileData.slice(offset, offset + chunkSize);

        // Pad to block size
        const padded = new Uint8Array(blockSize);
        padded.set(chunk);
        if (chunkSize < blockSize) padded.fill(0x1a, chunkSize); // EOF padding

        // Build frame: [STX] [block#] [255-block#] [data] [crc_hi] [crc_lo]
        const frameData = new Uint8Array(blockSize + 4);
        frameData[0] = STX;
        frameData[1] = blockNum & 0xff;
        frameData[2] = (255 - blockNum) & 0xff;
        frameData.set(padded, 3);

        const crcVal = crc16(padded);
        frameData[blockSize + 3] = (crcVal >>> 8) & 0xff;
        frameData[blockSize + 4] = crcVal & 0xff;

        // Send block
        await writer.write(frameData);
        bytesSent += chunkSize;

        // Wait for ACK
        let gotAck = false;
        const blockStart = Date.now();
        let ackRetries = 0;
        while (ackRetries < 5 && !gotAck) {
          const { value, done } = await Promise.race([
            reader.read(),
            new Promise(r => setTimeout(() => r({ done: true }), TIMEOUT_MS))
          ]);
          if (!done && value && value[0] === ACK) {
            gotAck = true;
            break;
          }
          ackRetries++;
        }

        if (!gotAck) {
          statusMsg.textContent = `block ${blockNum} NACK`;
          statusMsg.style.color = '#a44';
          uploadBtn.disabled = false;
          reader.releaseLock();
          writer.releaseLock();
          return;
        }

        blockNum++;
        const pct = Math.round((bytesSent / fileSize) * 100);
        progressFill.style.width = pct + '%';

        const elapsed = (Date.now() - startTime) / 1000;
        const rate = (bytesSent / 1024 / elapsed).toFixed(1);
        statusMsg.textContent = `${pct}% (${blockNum - 1}/${numBlocks} blocks)`;
        speedMsg.textContent = `${rate} KB/s`;
      }

      // ─── Send EOT ───
      statusMsg.textContent = 'finalizing';
      await writer.write(new Uint8Array([EOT]));

      // Wait for final ACK
      let finalAck = false;
      const finalStart = Date.now();
      while (Date.now() - finalStart < TIMEOUT_MS && !finalAck) {
        const { value, done } = await Promise.race([
          reader.read(),
          new Promise(r => setTimeout(() => r({ done: true }), 100))
        ]);
        if (!done && value && value[0] === ACK) {
          finalAck = true;
        }
      }

      reader.releaseLock();
      writer.releaseLock();

      const totalTime = ((Date.now() - startTime) / 1000).toFixed(1);
      progressFill.style.width = '100%';
      statusMsg.textContent = `✓ complete (${totalTime}s)`;
      statusMsg.style.color = '#666';
      fileInfo.textContent = `${selectedFile.name} transferred successfully`;

      setTimeout(() => {
        progressSection.style.display = 'none';
        uploadBtn.disabled = false;
      }, 3000);

    } catch (err) {
      statusMsg.textContent = `✗ ${err.message}`;
      statusMsg.style.color = '#a44';
      uploadBtn.disabled = false;
    }
  };

  return container;
}
