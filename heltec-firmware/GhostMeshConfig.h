#pragma once
#include <stdint.h>
#include <stdbool.h>

// Shared, persisted GhostMesh settings.
//
// One struct the modules read at runtime, backed by NVS (ESP32 Preferences) so changes survive
// reboot — until a wipe, which erases NVS along with everything else. Tunable live over the mesh
// CLI (`/set <key> <val>`, `/cfg`), from the web configurator, and from the FAP Settings screen —
// all send the same self-addressed /set + /cfg over the local link, no reflash needed.
//
// Defaults below match the modules' original compile-time constants, so a fresh node behaves
// exactly as before until something is changed.
struct GhostMeshConfig {
    uint16_t proxThresholdCm; // ProximityModule: trip distance (cm)
    uint16_t lightThreshold;  // LightTamperModule: ADC counts below which = "light"

    // ── Physical output enables (the "covert toggle"): does the hardware fire? ──
    // notifyLed/Buzz/Vib are the /cfg `out` bits 0/1/2 (name kept — 7 call sites read them).
    bool notifyLed;   // out bit 0 — RGB status LED (GPIO26)
    bool notifyBuzz;  // out bit 1 — buzzer
    bool notifyVib;   // out bit 2 — vibration motor
    bool outScreen;   // out bit 3 — OLED display on
    bool outHbled;    // out bit 4 — onboard heartbeat LED (GPIO35)
    bool outGpsled;   // out bit 5 — GPS PPS/fix LED (best-effort, UBX timepulse)

    // ── Mesh reply / broadcast enables: does a mesh message go out? (orthogonal to outputs) ──
    // rep_* gate command confirmations; bc_* gate autonomous sensor broadcasts. /cfg `rep` bits 0..7.
    bool repArm;   // rep bit 0 — /arm//disarm confirmations + Arming/IR ARMED/DISARMED broadcasts
    bool repBuzz;  // rep bit 1 — /buzz confirmation
    bool repVib;   // rep bit 2 — /vibrate confirmation
    bool repLed;   // rep bit 3 — /led + /fx confirmations
    bool repWipe;  // rep bit 4 — /wipe reply TEXT only (wipe safety is unaffected)
    bool bcTilt;   // rep bit 5 — TAMPER broadcast
    bool bcLight;  // rep bit 6 — TAMPER_LIGHT broadcast
    bool bcProx;   // rep bit 7 — PERSON_DETECTED broadcast
    // rep bits 8..11 — request-response replies, each individually gateable. These never broadcast
    // (they're routed only to whoever sent the command — off-mesh for the local Flipper/configurator,
    // directed unicast for a remote node), so gating just lets a node stay tight-lipped even to a
    // direct query. /cfg + /set success echo are deliberately NOT gated — they're the control channel
    // the configurator/FAP read to populate their UI.
    bool repHelp;    // rep bit 8  — /help listing
    bool repStatus;  // rep bit 9  — /status reply
    bool repErr;     // rep bit 10 — /set error messages (bad key/val, needs args, bad mode)
    bool repUnknown; // rep bit 11 — unknown-command reply

    // ── Sensor input enables (battery): does the module poll its hardware? /cfg `in` bits 0..3 ──
    bool inTilt;   // in bit 0
    bool inLight;  // in bit 1
    bool inProx;   // in bit 2
    bool inIr;     // in bit 3

    // ── Meshtastic-native (applied to config.position / moduleConfig.telemetry via saveToDisk) ──
    bool     gpsOn;         // GPS enabled (gps_mode ENABLED/DISABLED)
    uint16_t gpsUpdateSecs; // 0 = leave Meshtastic default; else config.position.gps_update_interval
    uint16_t telUpdateSecs; // 0 = leave default; else moduleConfig.telemetry.environment_update_interval
};

extern GhostMeshConfig ghostmesh_config;

// Load from NVS (applying defaults for anything unset). Idempotent — safe to call from every
// module's first runOnce; only the first call touches NVS.
void ghostmesh_config_ensure_loaded();

// Persist the current struct to NVS. Call after any /set change.
void ghostmesh_config_save();

// Apply the Meshtastic-native settings (GPS on/off + update intervals) held in this struct to
// Meshtastic's own config and persist them. Applied live (no reboot). Call at boot (after
// ensure_loaded) and after any /set of gps/gpsint/telint. Defined in GhostMeshConfig.cpp.
void ghostmesh_apply_native_config();
