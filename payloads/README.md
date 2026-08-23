# payloads/ — sample payloads

Sample payloads for the Remote Payload Execution use case
(`docs/red-team-lab-use-cases.md` §5, Phase 13). They exist to prove the delivery *and launch* path
end-to-end and to give the `/put`/`/get`/`/run` commands something real to move and fire.

They are also the easiest way to **test the web configurator's PAYLOAD UPLOAD**: a small text file
uploads in a second and is CRC-verified on arrival, so you see the whole `/put` round-trip
(`ready → ok`) without waiting on a large transfer.

## Files

| File | What it is | Notes |
|------|-----------|-------|
| `lab_hello.txt` | Flipper BadUSB DuckyScript | Opens Notepad, types a banner, saves/installs nothing. |

## Delivering and running one

Three steps — delivery, staging, and launch are kept deliberately separate:

1. **Upload to the backpack** (`/put`) — ghostmesh.info/config → **PAYLOAD UPLOAD** → pick the file →
   it lands in `/ghostmesh/` on the node's flash, CRC-verified. Works over USB (fast) or the mesh
   (slower, same protocol).
2. **Stage it on the Flipper's SD card** — either `/get` it down over the wired link (see
   `docs/command-cli.md`), or copy it directly with qFlipper/an SD reader to `/ext/badusb/`. Only a
   file that's physically on the Flipper's SD card can ever be launched — the Heltec's flash is not
   enough on its own.
3. **Launch it** (`/run @id <name>`) — from anywhere on the mesh, or locally from the Flipper's
   **Payloads** screen. The Heltec only checks armed and acks/denies; it never executes anything
   itself. The FAP wired to `@id` is what offers to hand off to Bad USB — requiring armed *and* an OK
   press there, and then Bad USB's own OK press before any keystroke actually fires. Three
   independent gates between a mesh command and a keystroke.

`/run` never carries bytes, only a name — matching the design constraint below (selected by name, no
arbitrary code injection). See `docs/command-cli.md` for the full `/ls`/`/get`/`/run` reference.
