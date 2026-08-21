# payloads/ — lab-only sample payloads

Sample, **benign** payloads for the Remote Payload Execution use case
(`docs/red-team-lab-use-cases.md` §5, Phase 13). They exist to prove the delivery path
end-to-end and to give the `/put` uploader something real to move — not to do anything to a
machine.

They are also the easiest way to **test the web configurator's PAYLOAD UPLOAD**: a small text file
uploads in a second and is CRC-verified on arrival, so you see the whole `/put` round-trip
(`ready → ok`) without waiting on a large transfer.

## Files

| File | What it is | Notes |
|------|-----------|-------|
| `lab_hello.txt` | Flipper BadUSB DuckyScript | Opens Notepad, types a banner, saves/installs nothing. Fully reversible — close without saving. |

## The non-negotiables (from the use-case doc)

Every payload here obeys the design constraints, and any you add must too:

- **Benign + reversible only** — print to a terminal/editor, create a text file, blink an LED.
  Nothing destructive, nothing persistent, no exfiltration, no network, no credentials.
- **Selected by name** — the trigger names a stored file. There is no arbitrary code injection.
- **ARMED + private channel** — the node must be armed and the trigger must arrive on the private
  channel. These gates live in the firmware/FAP, not in the payload file.
- **Lab / owned systems only.** See `docs/red-team-lab-use-cases.md` and the Scope & Authorization
  section of `CLAUDE.md`.

## Delivering one

- **Over USB (fast):** ghostmesh.info/config → **PAYLOAD UPLOAD** → pick the file → it lands in
  `/ghostmesh/` on the node, CRC-verified.
- **Over the mesh (slower):** the same `/put` protocol — see `docs/command-cli.md`.

The staged file is then invoked **by name** when an authorized trigger arrives — armed, on the
private channel. Delivery and execution are deliberately separate steps.
