#include "GhostMeshConfig.h"
#include "configuration.h"
#include <Preferences.h>

// Defaults = the modules' original compile-time constants (prox 200cm, light 2000, all indicators on).
GhostMeshConfig ghostmesh_config = {200, 2000, true, true, true};

static bool loaded = false;

// NVS namespace. Isolated from Meshtastic's own storage (it keeps config in LittleFS /prefs).
#define CFG_NS "ghostmesh"

void ghostmesh_config_ensure_loaded()
{
    if (loaded)
        return;
    loaded = true;
    Preferences p;
    if (!p.begin(CFG_NS, true)) // read-only; fails cleanly if the namespace doesn't exist yet
        return;
    ghostmesh_config.proxThresholdCm = p.getUShort("prox", ghostmesh_config.proxThresholdCm);
    ghostmesh_config.lightThreshold = p.getUShort("light", ghostmesh_config.lightThreshold);
    ghostmesh_config.notifyLed = p.getBool("led", ghostmesh_config.notifyLed);
    ghostmesh_config.notifyBuzz = p.getBool("buzz", ghostmesh_config.notifyBuzz);
    ghostmesh_config.notifyVib = p.getBool("vib", ghostmesh_config.notifyVib);
    p.end();
    LOG_INFO("Config: loaded prox=%u light=%u led=%d buzz=%d vib=%d", ghostmesh_config.proxThresholdCm,
             ghostmesh_config.lightThreshold, ghostmesh_config.notifyLed, ghostmesh_config.notifyBuzz,
             ghostmesh_config.notifyVib);
}

void ghostmesh_config_save()
{
    Preferences p;
    if (!p.begin(CFG_NS, false))
        return;
    p.putUShort("prox", ghostmesh_config.proxThresholdCm);
    p.putUShort("light", ghostmesh_config.lightThreshold);
    p.putBool("led", ghostmesh_config.notifyLed);
    p.putBool("buzz", ghostmesh_config.notifyBuzz);
    p.putBool("vib", ghostmesh_config.notifyVib);
    p.end();
}
