# GhostMesh — System Overview

> Utilizing hacked equipment and illegal technology, transmitted so that you might at least prepare.

GhostMesh is an open-source red-team hardware platform. A custom backpack snaps onto a Flipper Zero and turns it into an infrastructure-free mesh radio — then detaches, gets planted, and stays in the field as an autonomous node you still control. This document is the map: what the system is, how it's shaped, and where each piece lives.

Start here, then follow the pointers at the bottom.

---

## The idea

Physical and wireless engagements need eyes and comms where there's no network you're allowed to touch. GhostMesh is that: a deployable node that runs on its own battery, talks over long-range LoRa, and answers only to operators holding the channel key.

- **Attached**, the backpack rides on your Flipper — a handheld mesh terminal for the team.
- **Detached**, it's a dropbox: plant it, walk away. It watches its own perimeter (moved, opened, approached), holds its place on the mesh, and waits.
- **Controlled** from a distance — by line-of-sight IR from the operator who planted it, or over the encrypted mesh from any teammate.
- **Denied on capture** — an armed destruct wipes the device to bare silicon.

This repository is the **framework** — the sensing, signaling, and command backbone. Offensive capability (remote payload execution over USB, triggered by IR or mesh; SIGINT; more) is the layer built on top of it. See [roadmap.md](roadmap.md).

---

## Architecture

Two independent brains, one link you can break on purpose.

<pre><code>
      OPERATOR                                   PLANTED / UNATTENDED
   ┌──────────────┐                          ┌────────────────────────┐
   │ Flipper Zero │  GPIO header (TX/RX/GND) │  Heltec WiFi LoRa 32 V3 │  ))) 915 MHz
   │   the FAP    │ ◀══ backpack shield ════▶│  Meshtastic + GhostMesh │  ))) LoRa mesh
   │  terminal    │      (detachable)        │  sensors · IR · destruct│  ))) to teammates
   └──────────────┘                          └────────────────────────┘
</code></pre>

- **Flipper Zero** — the operator terminal. Screen, keys, logging, and the IR emitter that reaches a planted node by line of sight. Runs the GhostMesh FAP.
- **Heltec WiFi LoRa 32 V3** — the field asset. ESP32-S3 + an SX1262 LoRa radio, running stock Meshtastic plus the custom GhostMesh modules. This is what gets planted; it needs no Flipper to keep working.
- **The backpack** — a custom board (the shield) that carries the Heltec and its sensors and plugs onto the Flipper's GPIO header. Three signals cross that header — TX, RX, GND — and nothing else; each device runs from its own battery.

The Flipper talks to the Heltec over the **Meshtastic Serial module in PROTO mode** — the same protobuf stream the phone app uses. See [serial-modes.md](serial-modes.md).

---

## What you get from this repo

| Deliverable | Where | State |
|-------------|-------|-------|
| Operator app (Flipper FAP, C99) | [`flipper-app/`](../flipper-app/) | working |
| Backpack firmware (custom Heltec Meshtastic modules, C++) | [`heltec-firmware/`](../heltec-firmware/) | working |
| Board CAD (the GhostMesh PCB) | [`kicad/`](../kicad/) | in progress |
| Case CAD (3D-printable enclosure) | FreeCAD | planned |
| Hardware spec (every component, datasheet-trackable) | [hardware.md](hardware.md) | in progress |
| Wiring schematic (the full system) | [`kicad/`](../kicad/) · [wiring.md](wiring.md) | in progress |

All of it open — software, firmware, board, case, BoM.

---

## Core concepts

**The private channel.** GhostMesh runs on a Meshtastic private channel with a random key. That key is the security boundary — a node can't be commanded, read, or joined without it. The command syntax is public by design; the secret is the key, not the words. See [meshtastic-setup.md](meshtastic-setup.md) and [opsec.md](opsec.md).

**The arming gate.** A single shared flag, `ghostmesh_armed`, decides whether the node is live. Disarmed, it can be handled and staged silently. Armed, its tamper sensors report and its destruct is available. The gate is flipped three ways — a switch on the board, a mesh command, or IR — last action wins.

**Sensing.** Tilt (moved), photoresistor (case opened), ultrasonic proximity (someone approached). Each fires a plain-text mesh alert — only while armed. See [hardware.md](hardware.md).

**Command & control.** Two paths. Over the mesh: a text CLI, `/cmd @target` — status, arm/disarm, indicators, wipe (see [command-cli.md](command-cli.md)). Out of band: line-of-sight IR — arm, disarm, and the destruct sequence, for when you can see the device but the mesh can't reach it.

**The destruct.** An armed-gated **complete flash erase** — firmware, config, and channel keys wiped, the chip left in USB download mode. Layered: it fires only when armed, and only behind a confirm (a one-time mesh token, an IR `ARM → WIPE → CONFIRM` sequence, or a physical double-press). Recover by reflashing and restoring an encrypted config backup. See [opsec.md](opsec.md).

**Backup & restore.** The FAP captures the node's config — including the channel key — at connect, encrypts it with an operator passphrase (AES-256-GCM), and writes it to the Flipper SD. A wiped node comes back with a reflash + `tools/restore_backpack.py`. The passphrase is never stored; a captured Flipper yields only ciphertext.

---

## The platform is a skeleton

What exists today is the backbone: comms, situational awareness, and control. The roadmap hangs capability on it — remote payload execution (BadUSB / scripts over USB, triggered by IR or mesh), SIGINT and jammer detection, wardriving. The framework is deliberately general so those layers slot in without redesigning the interface. See [roadmap.md](roadmap.md).

---

## Where to go next

- **Build it:** [hardware.md](hardware.md) · [wiring.md](wiring.md) · [meshtastic-setup.md](meshtastic-setup.md) · [flipper-setup.md](flipper-setup.md) · [`heltec-firmware/README.md`](../heltec-firmware/README.md)
- **Operate it:** [user-guide.md](user-guide.md) · [command-cli.md](command-cli.md) · [opsec.md](opsec.md)
- **Understand the code:** [developer-guide.md](developer-guide.md) · [serial-modes.md](serial-modes.md)
- **Scope & plan:** [red-team-lab-use-cases.md](red-team-lab-use-cases.md) · [roadmap.md](roadmap.md)

> So light your candles, and may SERVER protect us all.
