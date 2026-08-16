# Flipper Zero Setup and Build Guide

## Prerequisites

- Python 3.8+ installed
- `pip` available
- Flipper Zero connected via USB (for deploying the FAP)
- Flipper with stock firmware or a compatible fork (OFW recommended for compatibility)

---

## Install ufbt

ufbt is the official micro build tool for Flipper Zero external apps. It downloads the correct SDK automatically — you do not need to clone the full Flipper firmware repository.

```bash
python -m pip install --upgrade ufbt
```

Verify:
```bash
ufbt --version
```

---

## Build GhostMesh

```bash
cd flipper-app
ufbt
```

On first run, ufbt will download the Flipper SDK (this may take a minute). Subsequent builds are fast.

Successful output looks like:
```
...
LINK    ghostmesh.elf
...
FAP     ghostmesh.fap
```

The built FAP will be in `flipper-app/dist/`.

---

## Deploy the FAP

### Option 1: Copy via qFlipper or SD card

1. Copy `flipper-app/dist/ghostmesh.fap` to your SD card at `apps/Tools/ghostmesh.fap`
2. On the Flipper: **Apps → Tools → GhostMesh**

### Option 2: Deploy via USB with ufbt

With the Flipper connected via USB and qFlipper **closed**:

```bash
cd flipper-app
ufbt launch
```

This builds, deploys, and launches the app on the Flipper automatically.

---

## Development Workflow

### Build only
```bash
cd flipper-app
ufbt
```

### Build and deploy
```bash
cd flipper-app
ufbt launch
```

### Clean build artifacts
```bash
cd flipper-app
ufbt clean
```

### Check SDK version in use
```bash
ufbt status
```

### Update SDK
```bash
ufbt update
```

---

## Troubleshooting

| Problem | Cause | Fix |
|---------|-------|-----|
| `ufbt: command not found` | pip install path not in PATH | Use `python -m ufbt` or add pip scripts dir to PATH |
| Compile error: unknown type | SDK version mismatch | Run `ufbt update` to refresh the SDK |
| `UART acquire failed` at runtime | Another app holds USART1 | Exit any other UART app on the Flipper before running GhostMesh |
| FAP not visible in Apps menu | Wrong SD card path | Confirm the FAP is at `SD:/apps/Tools/ghostmesh.fap` |
| Flipper reboots on FAP launch | Stack overflow | Increase `stack_size` in `application.fam` (currently `2 * 1024`) |

---

## SDK Compatibility Notes

GhostMesh targets the **official Flipper Zero SDK** via ufbt. The app uses:

- `furi_hal_serial` for UART (USART1 / FuriHalSerialIdUsart)
- `gui` / `ViewPort` for display
- `furi` core (mutex, logging, delays)

These APIs are stable across recent official firmware releases. If you are using a fork (Unleashed, Momentum, etc.), the SDK should be compatible, but test on your specific firmware version.
