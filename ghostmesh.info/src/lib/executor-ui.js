/**
 * GhostMesh Command File Executor UI Component
 * Transmits payload/script files to mesh nodes with automatic chunking
 */

export function executorInit(serialConnection, nodeId) {
  const CHUNK_SIZE = 180; // Leaves room for framing in ~200 byte mesh limit
  const STATUS_DURATION = 5000;

  const container = document.createElement('div');
  container.className = 'ghostmesh-executor';
  container.innerHTML = `
    <div class="executor-panel">
      <div class="executor-header">
        <h3>⚡ Payload Executor</h3>
        <p>Transmit command files to the mesh</p>
      </div>

      <input type="file" id="file-picker" accept=".bin,.txt,.dat,.cmd" class="executor-input" />
      <div id="file-info" class="executor-info hidden">
        <span id="file-name"></span>
        <span id="file-size"></span>
      </div>

      <button id="send-btn" class="executor-btn" disabled>Send to Node</button>
      <div id="status" class="executor-status"></div>
    </div>
  `;

  // Inject ServerMonk-themed styles
  const style = document.createElement('style');
  style.textContent = `
    .ghostmesh-executor { font-family: 'Monaco', monospace; }
    .executor-panel {
      padding: 1.5rem;
      background: #0f1428;
      border: 1px solid #2a3f7f;
      border-radius: 6px;
      box-shadow: 0 4px 6px rgba(0, 0, 0, 0.3);
    }
    .executor-header { margin-bottom: 1rem; }
    .executor-header h3 {
      color: #e8eef5;
      font-size: 1rem;
      margin: 0 0 0.25rem 0;
      font-weight: 600;
    }
    .executor-header p {
      color: #8b96c4;
      font-size: 0.75rem;
      margin: 0;
      letter-spacing: 0.5px;
    }
    .executor-input {
      color: #8b96c4;
      background: transparent;
      border: 1px solid #2a3f7f;
      padding: 0.5rem;
      border-radius: 4px;
      width: 100%;
      box-sizing: border-box;
      margin: 1rem 0;
    }
    .executor-info {
      font-size: 0.875rem;
      color: #8b96c4;
      margin: 0.75rem 0;
      display: flex;
      gap: 1.5rem;
    }
    .executor-info span::before {
      content: '▪ ';
      color: #00ff9f;
      margin-right: 0.5rem;
    }
    .executor-btn {
      background: #1a2a5a;
      color: #00ff9f;
      border: 1px solid #00ff9f;
      padding: 0.5rem 1rem;
      border-radius: 4px;
      cursor: pointer;
      font-family: inherit;
      font-weight: 500;
      transition: all 0.2s ease;
    }
    .executor-btn:hover:not(:disabled) {
      background: #2a3a7a;
      box-shadow: 0 0 12px rgba(0, 255, 159, 0.3);
    }
    .executor-btn:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }
    .executor-status {
      font-size: 0.875rem;
      color: #8b96c4;
      margin-top: 1rem;
      min-height: 1.25rem;
      transition: color 0.3s ease;
    }
    .status-success { color: #00ff9f; }
    .status-error { color: #ff6b6b; }
    .hidden { display: none; }
  `;
  document.head.appendChild(style);

  const picker = container.querySelector('#file-picker');
  const fileInfo = container.querySelector('#file-info');
  const fileName = container.querySelector('#file-name');
  const fileSize = container.querySelector('#file-size');
  const sendBtn = container.querySelector('#send-btn');
  const status = container.querySelector('#status');

  let selectedFile = null;
  let isSending = false;

  picker.addEventListener('change', (e) => {
    selectedFile = e.target.files[0];
    if (selectedFile) {
      fileName.textContent = `${selectedFile.name}`;
      fileSize.textContent = `${(selectedFile.size / 1024).toFixed(2)} KB`;
      fileInfo.classList.remove('hidden');
      sendBtn.disabled = false;
      status.textContent = '';
    }
  });

  sendBtn.addEventListener('click', async () => {
    if (!selectedFile || isSending) return;

    isSending = true;
    sendBtn.disabled = true;

    try {
      const arrayBuf = await selectedFile.arrayBuffer();
      const bytes = new Uint8Array(arrayBuf);
      const totalChunks = Math.ceil(bytes.length / CHUNK_SIZE);

      status.classList.remove('status-success', 'status-error');
      status.textContent = `Sending ${totalChunks} chunk${totalChunks > 1 ? 's' : ''}...`;

      // Transmit chunks over mesh
      for (let i = 0; i < totalChunks; i++) {
        const start = i * CHUNK_SIZE;
        const end = Math.min(start + CHUNK_SIZE, bytes.length);
        const chunk = bytes.slice(start, end);

        // Encode chunk as base64 for mesh transport
        const b64 = btoa(String.fromCharCode(...chunk));

        // Format command: /exec @nodeId chunk:i/total base64payload
        const cmd = `/exec @${nodeId.toString(16)} chunk:${i + 1}/${totalChunks} ${b64}`;

        // Send over serial to mesh
        await serialConnection.write(cmd + '\n');

        // Update progress
        const progress = Math.round(((i + 1) / totalChunks) * 100);
        status.textContent = `Sending... ${progress}%`;
      }

      // Success
      status.classList.add('status-success');
      status.textContent = `✓ Transmitted ${bytes.length} bytes to @${nodeId.toString(16)}`;

      setTimeout(() => {
        status.classList.remove('status-success');
        status.textContent = '';
      }, STATUS_DURATION);

    } catch (err) {
      status.classList.add('status-error');
      status.textContent = `✗ ${err.message}`;
    } finally {
      isSending = false;
      sendBtn.disabled = false;
    }
  });

  return container;
}
