#pragma once
#include <stdint.h>
#include <stdbool.h>

// Shared, persisted GhostMesh settings.
//
// One struct the modules read at runtime, backed by NVS (ESP32 Preferences) so changes survive
// reboot — until a wipe, which erases NVS along with everything else. Tunable live over the mesh
// CLI (`/set <key> <val>`, `/cfg`) so a deployed node can be re-tuned without a reflash, and
// (later) from the FAP settings screen.
//
// Defaults below match the modules' original compile-time constants, so a fresh node behaves
// exactly as before until something is changed.
struct GhostMeshConfig {
    uint16_t proxThresholdCm; // ProximityModule: trip distance (cm)
    uint16_t lightThreshold;  // LightTamperModule: ADC counts below which = "light"
    bool     notifyLed;       // CommandModule indicators: LED enabled
    bool     notifyBuzz;      // buzzer enabled
    bool     notifyVib;       // vibration enabled
};

extern GhostMeshConfig ghostmesh_config;

// Load from NVS (applying defaults for anything unset). Idempotent — safe to call from every
// module's first runOnce; only the first call touches NVS.
void ghostmesh_config_ensure_loaded();

// Persist the current struct to NVS. Call after any /set change.
void ghostmesh_config_save();
