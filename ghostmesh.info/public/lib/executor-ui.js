/**
 * GhostMesh Mesh Command Executor UI Component
 * Minimal ServerMonk-styled file upload for mesh payload delivery.
 */

export function meshExecInit(serialConnection, nodeId) {
  const container = document.createElement('div');
  container.style.cssText = `
    padding: 16px; border: 1px solid #333; border-radius: 2px;
    background: #0a0a0a; color: #ccc; font-family: monospace; font-size: 12px;
    max-width: 500px;
  `;

  const title = document.createElement('div');
  title.style.cssText = 'margin-bottom: 12px; color: #666; text-transform: uppercase; letter-spacing: 1px; font-size: 11px;';
  title.textContent = 'MESH COMMANDS';
  container.appendChild(title);

  const fileInput = document.createElement('input');
  fileInput.type = 'file';
  fileInput.accept = '.txt,.cmd,.sh';
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
  container.appendChild(progressSection);

  const sendBtn = document.createElement('button');
  sendBtn.textContent = 'SEND TO MESH';
  sendBtn.style.cssText = `
    width: 100%; padding: 8px; background: transparent; border: 1px solid #444;
    color: #aaa; cursor: pointer; font-family: monospace; font-size: 11px;
    text-transform: uppercase; transition: all 0.2s; margin-bottom: 8px;
  `;
  sendBtn.disabled = true;
  sendBtn.onmouseover = () => !sendBtn.disabled && (sendBtn.style.borderColor = '#666', sendBtn.style.color = '#ddd');
  sendBtn.onmouseout = () => !sendBtn.disabled && (sendBtn.style.borderColor = '#444', sendBtn.style.color = '#aaa');
  container.appendChild(sendBtn);

  let selectedFile = null;

  fileInput.onchange = (e) => {
    selectedFile = e.target.files[0];
    if (!selectedFile) return;

    if (selectedFile.size > 50000) {
      fileInfo.textContent = 'error: file exceeds 50kb';
      fileInfo.style.color = '#a44';
      sendBtn.disabled = true;
      selectedFile = null;
      return;
    }

    const sizeKb = (selectedFile.size / 1024).toFixed(1);
    fileInfo.textContent = `${selectedFile.name} (${sizeKb} kb)`;
    fileInfo.style.color = '#666';
    sendBtn.disabled = false;
  };

  sendBtn.onclick = async () => {
    if (!selectedFile || !serialConnection) return;

    progressSection.style.display = 'block';
    sendBtn.disabled = true;
    statusMsg.textContent = 'reading file';

    try {
      const buffer = await selectedFile.arrayBuffer();
      const bytes = new Uint8Array(buffer);
      const b64 = btoa(String.fromCharCode(...bytes));

      const chunkSize = 180;
      const chunks = [];
      for (let i = 0; i < b64.length; i += chunkSize) {
        chunks.push(b64.slice(i, i + chunkSize));
      }

      statusMsg.textContent = `sending ${chunks.length} chunks`;

      for (let i = 0; i < chunks.length; i++) {
        const cmd = `/exec @${nodeId} ${chunks[i]}\n`;
        await serialConnection.write(cmd);

        const pct = Math.round(((i + 1) / chunks.length) * 100);
        progressFill.style.width = pct + '%';
        statusMsg.textContent = `${pct}% sent`;

        await new Promise(r => setTimeout(r, 50));
      }

      statusMsg.textContent = 'transmission complete';
      statusMsg.style.color = '#666';
      fileInfo.textContent = 'file sent successfully';
    } catch (err) {
      statusMsg.textContent = 'transmission failed';
      statusMsg.style.color = '#a44';
      console.error('Mesh exec error:', err);
    }

    sendBtn.disabled = false;
  };

  return container;
}

// For backward compatibility, export as executorInit
export function executorInit(serialConnection, nodeId) {
  return meshExecInit(serialConnection, nodeId);
}
