---
---
# Flipper Zero Setup and Build Guide

## Prerequisites

- Python 3.8+ installed
- <code>pip</code> available
- Flipper Zero connected via USB (for deploying the FAP)
- Flipper with stock firmware or a compatible fork (OFW recommended for compatibility)

---

## Install ufbt

ufbt is the official micro build tool for Flipper Zero external apps. It downloads the correct SDK automatically — you do not need to clone the full Flipper firmware repository.

<pre><code>
python -m pip install --upgrade ufbt
<pre><code>

Verify:
</code></pre>
ufbt --version
</code></pre>

---

## Build GhostMesh

<pre><code>
cd flipper-app
ufbt
<pre><code>

On first run, ufbt will download the Flipper SDK (this may take a minute). Subsequent builds are fast.

Successful output looks like:
</code></pre>
...
LINK    ghostmesh.elf
...
FAP     ghostmesh.fap
<pre><code>

The built FAP will be in <code>flipper-app/dist/</code>.

---

## Deploy the FAP

### Option 1: Copy via qFlipper or SD card

1. Copy <code>flipper-app/dist/ghostmesh.fap</code> to your SD card at <code>apps/Tools/ghostmesh.fap</code>
2. On the Flipper: **Apps → Tools → GhostMesh**

### Option 2: Deploy via USB with ufbt

With the Flipper connected via USB and qFlipper **closed**:

</code></pre>
cd flipper-app
ufbt launch
</code></pre>

This builds, deploys, and launches the app on the Flipper automatically.

---

## Development Workflow

### Build only
<pre><code>
cd flipper-app
ufbt
<pre><code>

### Build and deploy
</code></pre>
cd flipper-app
ufbt launch
</code></pre>

### Clean build artifacts
<pre><code>
cd flipper-app
ufbt clean
<pre><code>

### Check SDK version in use
</code></pre>
ufbt status
</code></pre>

### Update SDK
<pre><code>
ufbt update
~~~

---

## Troubleshooting

| Problem | Cause | Fix |
|---------|-------|-----|
| <code>ufbt: command not found</code> | pip install path not in PATH | Use <code>python -m ufbt</code> or add pip scripts dir to PATH |
| Compile error: unknown type | SDK version mismatch | Run <code>ufbt update</code> to refresh the SDK |
| <code>UART acquire failed</code> at runtime | Another app holds USART1 | Exit any other UART app on the Flipper before running GhostMesh |
| FAP not visible in Apps menu | Wrong SD card path | Confirm the FAP is at <code>SD:/apps/Tools/ghostmesh.fap</code> |
| Flipper reboots on FAP launch | Stack overflow | Increase <code>stack_size</code> in <code>application.fam</code> (currently <code>2 * 1024</code>) |

---

## SDK Compatibility Notes

GhostMesh targets the **official Flipper Zero SDK** via ufbt. The app uses:

- <code>furi_hal_serial</code> for UART (USART1 / FuriHalSerialIdUsart)
- <code>gui</code> / <code>ViewPort</code> for display
- <code>furi</code> core (mutex, logging, delays)

These APIs are stable across recent official firmware releases. If you are using a fork (Unleashed, Momentum, etc.), the SDK should be compatible, but test on your specific firmware version.
