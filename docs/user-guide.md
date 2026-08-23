# GhostMesh User Guide

The FAP is the operator terminal. This walks through it screen by screen. (Building and installing it: [flipper-setup.md](flipper-setup.md).)

## Navigation

The app opens on the **menu hub** — the home screen. Pick a screen, open it, `BACK` returns to the hub. `BACK` from the hub exits.

<pre><code>
Menu hub ─[OK]→ Messages ─[OK]→ Profiles ─[OK]→ that profile's messages
   │                                                   (OK sends)
   ├─→ RX History        ├─→ Control (IR)
   ├─→ Sensors           ├─→ Status
   └─→ Backup            └─→ Settings

BACK always steps back one level.
</code></pre>

Every screen carries the same **title bar** (name + link/battery status) and a **bottom marquee** that scrolls the last message received — so incoming traffic is visible from anywhere in the app.

**Status indicator (top right):**
- `...` — handshake in progress (self-retries every ~2 s until the node answers)
- `RDY` — connected to the node, ready
- `77%` / `PWR` — the node's battery level once known (`PWR` = external/USB power)

---

## Menu (home)

The hub. Up/Down to choose, OK to open.

<pre><code>
┌────────────────────────────┐
│ GhostMesh             77%  │
├────────────────────────────┤
│ ► Messages                 │
│   RX History               │
│   Sensors                  │
│   Control                  │
│   Status · Backup          │
│   Settings                 │
├────────────────────────────┤
│ f69c: TAMPER               │ ← last received, scrolling
└────────────────────────────┘
</code></pre>

| Button | Action |
|--------|--------|
| UP / DOWN | Move through the menu |
| OK | Open the highlighted screen |
| BACK | Exit GhostMesh |

---

## Messages

`Menu → Messages` opens the **Profiles** picker; choose one and OK loads its canned messages.

<pre><code>
Profiles                         Grid Down
┌────────────────────────────┐   ┌────────────────────────────┐
│ ► Grid Down                │   │ ► CHECKIN OK               │
│   Hiking / SAR             │   │   NEED ASSISTANCE          │
│   Red Team                 │ → │   MOVING                   │
│   My Custom Profile        │   │   HOLD POSITION            │
├────────────────────────────┤   ├────────────────────────────┤
│ OK:Load  BACK:Menu         │   │ TX:12  OK:Send             │
└────────────────────────────┘   └────────────────────────────┘
</code></pre>

| Button | Action |
|--------|--------|
| UP / DOWN | Scroll (hold to repeat) |
| OK | Profiles: load · Messages: **send** over the mesh |
| BACK | Messages → Profiles → Menu |

The bottom line shows a `Sent: …` banner for ~2 s after you send, otherwise the last received message. Long text marquee-scrolls; a scrollbar appears when the list overflows.

---

## RX History

`Menu → RX History`. The last 16 received messages, newest first.

<pre><code>
┌────────────────────────────┐
│ RX History            RDY  │
├────────────────────────────┤
│ f69c -3dBm: CHECKIN OK     │
│ 2f74: MOVING               │
│ f69c -7dBm: TAMPER         │
├────────────────────────────┤
│ BACK:Menu                  │
└────────────────────────────┘
</code></pre>

Each entry shows the sender (last 4 hex of the node ID), RSSI in dBm when available, and the text. Up/Down scrolls; BACK returns to the hub.

---

## Sensors

`Menu → Sensors`. Latest environmental telemetry and GPS from the attached node.

<pre><code>
┌────────────────────────────┐
│ Sensors               77%  │
├────────────────────────────┤
│ T:23.4C  H:41%             │
│ Press: 1013.2 hPa          │
│ GPS 37.043,-76.326         │
│ Alt: 27 m                  │
└────────────────────────────┘
</code></pre>

Temp/humidity/pressure come from the BME280; the GPS line shows the last fix or `GPS: no fix`. Blank until the first packet of each type arrives.

---

## Control

`Menu → Control`. Drives a backpack over **IR** — point the Flipper's emitter at the node. The `Node:` line reflects the last `ARMED`/`DISARMED` the backpack broadcast — but note that arm/disarm broadcasts are gated by the `rep_arm` setting, which is **off by default** (covert), so the line only updates if you've turned `rep_arm` on (Settings screen or `/set`).

<pre><code>
┌────────────────────────────┐
│ Control               77%  │
├────────────────────────────┤
│ Node: ARMED                │
│ ► Arm                      │
│   Disarm                   │
│   Wipe                     │
├────────────────────────────┤
│ OK:Send IR                 │
└────────────────────────────┘
</code></pre>

| Action | Effect |
|--------|--------|
| **Arm** / **Disarm** | Transmits one IR command; the node flips its arm state (and broadcasts it back only if `rep_arm` is enabled) |
| **Wipe** | Opens an on-screen confirmation (defaults to **Cancel**). Confirm and the FAP sends the `ARM → WIPE → CONFIRM` IR sequence — the **complete-erase destruct** |

The wipe confirm is deliberate: Cancel is preselected, and the toggle ignores held keys so a stray press can't reach Confirm. The destruct erases the backpack (see [opsec.md](opsec.md)) — only use it on a node you mean to burn.

---

## Status

`Menu → Status`. A one-glance node state overview.

<pre><code>
┌────────────────────────────┐
│ Status                77%  │
├────────────────────────────┤
│ Link:  connected           │
│ Batt:  77%                 │
│ Armed: ARMED               │
│ GPS:   fix                 │
└────────────────────────────┘
</code></pre>

---

## Backup

`Menu → Backup`. Captures the attached node's configuration — device/module config **and the channel keys** — and writes it to the Flipper SD as an **encrypted** file. This is how you recover a wiped node.

1. Open **Backup**. The Flipper keyboard prompts for a passphrase.
2. Enter it and confirm. GhostMesh encrypts the config (AES-256-GCM) and writes `SD:/apps_data/ghostmesh/backup_<id>.gmb`.
3. The screen reports `Saved backup_<id>.gmb` (or `Cancelled` / an error).

The passphrase is never stored — a captured Flipper yields only ciphertext. To restore after reflashing a wiped node, copy the `.gmb` to a PC and run `tools/restore_backpack.py` (see [command-cli.md](command-cli.md) and the tool's header). Requires `pip install meshtastic cryptography`.

---

## Settings

`Menu → Settings`. Live node configuration, sent to the attached backpack over the local link
(self-addressed, off-air) — the same `/set`/`/cfg` you can drive from the web configurator or the
mesh CLI. On open it queries the node and populates from the reply.

A scrolling, sectioned list of ~23 settings:

<pre><code>
┌────────────────────────────┐
│ Settings              77%  │
├────────────────────────────┤
│ -SENSING-                  │
│ ► prox      200 cm         │
│   light     2000           │
│ -REPLIES-                  │
│   arm       off            │
├────────────────────────────┤
│ Up/Dn pick  Lt/Rt set      │
└────────────────────────────┘
</code></pre>

- **Up/Down** move between rows (section headers are skipped).
- **Left/Right** change the selected value — a slider steps its number, a toggle flips on/off — and
  the change is sent to the node immediately.
- **OK** re-queries the node; **BACK** returns to the menu.

Sections: **Sensing** (proximity / light thresholds), **Replies** (per-command mesh-reply gates —
`arm`, `buzz`, `vib`, `led`, `wipe`, and the tamper broadcasts; routine replies are off by default),
**Outputs** (physical indicators — LED, buzzer, vibration, screen, onboard LED, GPS LED — for silent
mode), **Inputs** (per-sensor enable, to save battery), and **GPS/Telem** (GPS on/off + update
intervals). See [command-cli.md](command-cli.md) for every key.

---

## SD Card Logging

Every received message is appended to a dated CSV on the Flipper SD:

<pre><code>
SD:/apps_data/ghostmesh/log_YYYYMMDD.csv
</code></pre>

Columns: `timestamp, node_id, message, lat, lon, rssi, snr`.

<pre><code>
2026-05-06T14:32:01,f69c,CHECKIN OK,37.0432650,-76.3262981,-85,7.5
2026-05-06T14:33:44,2f74,MOVING,,,-92,4.2
</code></pre>

`lat`/`lon` carry the local node's last GPS fix, or blank before a lock. Convert to KML for Google Earth / QGIS:

```bash
python tools/log_to_kml.py log_20260506.csv
<pre><code>

---

## Custom Profiles

Create `SD:/apps_data/ghostmesh/profiles.yaml`. Start each profile with `name:`, then list messages with `-`:

```yaml
name: My Field Profile
- CHECKIN OK
- IN POSITION
- MOVING
- ABORT
- EXFIL NOW

name: Comms Check
- RADIO CHECK
- LOUD AND CLEAR
- WEAK SIGNAL
</code></pre>

**Rules:** up to 5 custom profiles (8 total with the built-ins), ≤12 messages each, names ≤19 chars, messages ≤22 chars, printable ASCII only. Empty profiles are skipped. See `examples/profiles.yaml` for a commented template.

### Built-in profiles
- **Grid Down** — `CHECKIN OK` · `NEED ASSISTANCE` · `MOVING` · `HOLD POSITION` · `ALL CLEAR` · `BATTERY LOW` · `MEDICAL NEEDED` · `SHELTER IN PLACE`
- **Hiking / SAR** — `CHECKIN OK` · `ON TRAIL` · `OFF TRAIL` · `SUMMIT REACHED` · `TURNING BACK` · `NEED WATER` · `NEED MEDICAL` · `CAMP REACHED`
- **Red Team** — `CHECKIN OK` · `IN POSITION` · `MOVING` · `ABORT` · `PHASE START` · `PHASE COMPLETE` · `HOLD` · `ALL CLEAR`

---

## Reading RSSI

Shown in RX History next to the sender, in dBm:

| RSSI | Signal |
|------|--------|
| -30 to -70 | Strong (close) |
| -70 to -100 | Good (normal LoRa range) |
| -100 to -120 | Marginal |
| below -120 | Edge of range |

`0` means the packet didn't arrive over radio (local echo or a locally-generated broadcast, e.g. the node's own `ARMED`).

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| Title bar stuck on `...` | No UART link, or Serial module not in PROTO on 7/6 | Check TX/RX (Flipper 13→Heltec 7, 14→Heltec 6); verify the Serial module is PROTO |
| `...` only clears on USB power | Wired to GPIO43/44 — the CP2102 clamps them on battery | Move the Heltec-side wires to GPIO7/6 |
| OK sends but nothing heard | Not transmitting | Check the antenna; verify the Meshtastic region and the private channel/frequency slot |
| Control screen does nothing | Backpack on old firmware, or out of IR line-of-sight | Reflash the latest backpack firmware; aim the emitter at the receiver |
| Custom profiles don't load | Wrong SD path or YAML | File must be at `SD:/apps_data/ghostmesh/profiles.yaml` |
| Backup says "No config yet" | Handshake not complete | Wait for `RDY`, then retry |
