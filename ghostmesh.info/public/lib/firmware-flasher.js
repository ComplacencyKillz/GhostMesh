// GhostMesh Firmware Flasher Component
// Requires: esptool.js loaded globally
// CDN: <script src="https://cdn.jsdelivr.net/npm/esptool-js@0.4.0/dist/bundle.js"></script>

export async function flasherInit(serialPort) {
  const container = document.createElement('div');
  container.id = 'firmware-flasher';
  container.style.cssText = `
    background: #0a0e27; color: #e0e0e0; padding: 16px; border-radius: 4px;
    border: 1px solid #1a2a4a; font-family: monospace; font-size: 13px;
  `;

  // Chip info panel
  const chipInfo = document.createElement('div');
  chipInfo.style.cssText = `
    margin-bottom: 12px; padding: 8px; background: #0f1424;
    border-left: 2px solid #888; font-size: 12px;
  `;
  chipInfo.textContent = '⏳ Detecting chip...';
  container.appendChild(chipInfo);

  // File selector
  const fileLabel = document.createElement('label');
  fileLabel.style.cssText = 'display: flex; align-items: center; margin: 12px 0; cursor: pointer;';
  const fileInput = document.createElement('input');
  fileInput.type = 'file';
  fileInput.accept = '.bin';
  fileInput.style.display = 'none';
  const fileBtn = document.createElement('span');
  fileBtn.textContent = 'Choose .bin file';
  fileBtn.style.cssText = 'color: #00d9ff; text-decoration: underline;';
  const fileName = document.createElement('span');
  fileName.style.cssText = 'margin-left: 8px; color: #666; font-size: 12px;';
  fileLabel.appendChild(fileInput);
  fileLabel.appendChild(fileBtn);
  fileLabel.appendChild(fileName);
  container.appendChild(fileLabel);

  let selectedFile = null;
  fileInput.addEventListener('change', (e) => {
    selectedFile = e.target.files[0];
    fileName.textContent = selectedFile ? `✓ ${selectedFile.name}` : '';
  });

  // Progress bar (hidden until flash)
  const progressContainer = document.createElement('div');
  progressContainer.style.cssText = 'margin: 12px 0; display: none;';
  const progressLabel = document.createElement('div');
  progressLabel.style.cssText = 'margin-bottom: 4px; font-size: 11px; color: #00d9ff;';
  progressLabel.textContent = 'Progress: 0%';
  const progressBar = document.createElement('div');
  progressBar.style.cssText = 'height: 6px; background: #1a2a4a; border-radius: 2px; overflow: hidden;';
  const progressFill = document.createElement('div');
  progressFill.style.cssText = `
    height: 100%; width: 0%;
    background: linear-gradient(90deg, #00d9ff, #00ff88);
    transition: width 0.15s ease-out;
  `;
  progressBar.appendChild(progressFill);
  progressContainer.appendChild(progressLabel);
  progressContainer.appendChild(progressBar);
  container.appendChild(progressContainer);

  // Status output
  const status = document.createElement('div');
  status.style.cssText = `
    margin-top: 12px; padding: 8px; background: #0f1424;
    border-left: 2px solid #666; min-height: 18px; font-size: 11px;
  `;
  status.textContent = 'Ready';
  container.appendChild(status);

  // Flash button
  const flashBtn = document.createElement('button');
  flashBtn.textContent = 'Flash Firmware';
  flashBtn.disabled = true;
  flashBtn.style.cssText = `
    margin-top: 12px; padding: 8px 16px; background: #00d9ff; color: #0a0e27;
    border: none; border-radius: 2px; cursor: pointer; font-weight: bold;
    font-family: monospace; font-size: 12px;
  `;
  container.appendChild(flashBtn);

  // Auto-detect chip
  async function detectChip() {
    try {
      const espTool = await ESPLoader.connect({ port: serialPort });
      const mac = await espTool.readMac();
      const chip = espTool.chip.CHIP_NAME || 'Unknown';
      chipInfo.textContent = `${chip} • MAC: ${mac}`;
      chipInfo.style.borderLeftColor = '#00ff88';
      flashBtn.disabled = false;
      await espTool.disconnect();
    } catch (err) {
      chipInfo.textContent = `✗ ${err.message}`;
      chipInfo.style.borderLeftColor = '#ff6644';
    }
  }

  // Flash workflow
  flashBtn.addEventListener('click', async () => {
    if (!selectedFile) return;
    progressContainer.style.display = 'block';
    flashBtn.disabled = true;
    status.style.borderLeftColor = '#00d9ff';
    status.textContent = 'Connecting...';

    try {
      const espTool = await ESPLoader.connect({ port: serialPort });
      const buffer = await selectedFile.arrayBuffer();
      const data = new Uint8Array(buffer);

      status.textContent = 'Writing firmware...';
      await espTool.writeFlash({
        fileArray: [{ data, address: 0 }],
        flashMode: 'dio',
        flashFreq: '80m',
        progress: (p) => {
          const pct = Math.round(p * 100);
          progressLabel.textContent = `Progress: ${pct}%`;
          progressFill.style.width = `${pct}%`;
        }
      });

      status.textContent = 'Verifying...';
      await new Promise(r => setTimeout(r, 500));
      await espTool.hardReset();
      await espTool.disconnect();

      status.textContent = '✓ Flash complete. Device rebooting...';
      status.style.borderLeftColor = '#00ff88';
      progressContainer.style.display = 'none';

      setTimeout(() => {
        status.textContent = '✓ Ready for use';
        flashBtn.disabled = false;
      }, 2000);
    } catch (err) {
      status.textContent = `✗ ${err.message}`;
      status.style.borderLeftColor = '#ff6644';
      flashBtn.disabled = false;
      progressContainer.style.display = 'none';
    }
  });

  await detectChip();
  return container;
}
