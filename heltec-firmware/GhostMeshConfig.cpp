#include "GhostMeshConfig.h"
#include "configuration.h"
#include <Preferences.h>

// Defaults. Physical outputs all ON (unchanged behaviour); sensor inputs all ON; GPS ON.
// The mesh-reply defaults are the fix for channel spam: routine command confirmations
// (rep_arm/buzz/vib/led) default OFF — the operator gets physical feedback, not a mesh message —
// while the wipe confirmation and the tamper broadcasts (the security feature) default ON.
// Field order MUST match the struct in GhostMeshConfig.h (positional initializer).
GhostMeshConfig ghostmesh_config = {
    200,   // proxThresholdCm
    2000,  // lightThreshold
    true, true, true,          // notifyLed, notifyBuzz, notifyVib   (out 0,1,2)
    true, true, true,          // outScreen, outHbled, outGpsled     (out 3,4,5)
    false, false, false, false,// repArm, repBuzz, repVib, repLed    (rep 0..3) — silent by default
    true,                      // repWipe                            (rep 4)
    true, true, true,          // bcTilt, bcLight, bcProx            (rep 5,6,7) — tamper stays on
    true, true, true, true,    // repHelp, repStatus, repErr, repUnknown (rep 8..11) — reply by default
    true,                      // repRun                              (rep 12) — ack/deny by default
    true, true, true, true,    // inTilt, inLight, inProx, inIr      (in 0..3)
    true,                      // gpsOn
    true,                      // telOn (environment telemetry enabled)
    0,                         // gpsUpdateSecs (0 = Meshtastic default)
    0,                         // telUpdateSecs (0 = Meshtastic default)
};

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
    GhostMeshConfig &c = ghostmesh_config;
    c.proxThresholdCm = p.getUShort("prox", c.proxThresholdCm);
    c.lightThreshold = p.getUShort("light", c.lightThreshold);
    c.notifyLed = p.getBool("led", c.notifyLed);
    c.notifyBuzz = p.getBool("buzz", c.notifyBuzz);
    c.notifyVib = p.getBool("vib", c.notifyVib);
    c.outScreen = p.getBool("scr", c.outScreen);
    c.outHbled = p.getBool("hbl", c.outHbled);
    c.outGpsled = p.getBool("gpl", c.outGpsled);
    c.repArm = p.getBool("rarm", c.repArm);
    c.repBuzz = p.getBool("rbuz", c.repBuzz);
    c.repVib = p.getBool("rvib", c.repVib);
    c.repLed = p.getBool("rled", c.repLed);
    c.repWipe = p.getBool("rwip", c.repWipe);
    c.bcTilt = p.getBool("btlt", c.bcTilt);
    c.bcLight = p.getBool("blit", c.bcLight);
    c.bcProx = p.getBool("bprx", c.bcProx);
    c.repHelp = p.getBool("rhlp", c.repHelp);
    c.repStatus = p.getBool("rsta", c.repStatus);
    c.repErr = p.getBool("rerr", c.repErr);
    c.repUnknown = p.getBool("runk", c.repUnknown);
    c.repRun = p.getBool("rrun", c.repRun);
    c.inTilt = p.getBool("itlt", c.inTilt);
    c.inLight = p.getBool("ilit", c.inLight);
    c.inProx = p.getBool("iprx", c.inProx);
    c.inIr = p.getBool("iir", c.inIr);
    c.gpsOn = p.getBool("gpso", c.gpsOn);
    c.telOn = p.getBool("telo", c.telOn);
    c.gpsUpdateSecs = p.getUShort("gpsint", c.gpsUpdateSecs);
    c.telUpdateSecs = p.getUShort("telint", c.telUpdateSecs);
    p.end();
    LOG_INFO("Config: prox=%u light=%u out(led%d buzz%d vib%d scr%d hb%d gps%d) rep=%d%d%d%d%d bc=%d%d%d "
             "in=%d%d%d%d gps=%d gpsint=%u telint=%u",
             c.proxThresholdCm, c.lightThreshold, c.notifyLed, c.notifyBuzz, c.notifyVib, c.outScreen,
             c.outHbled, c.outGpsled, c.repArm, c.repBuzz, c.repVib, c.repLed, c.repWipe, c.bcTilt,
             c.bcLight, c.bcProx, c.inTilt, c.inLight, c.inProx, c.inIr, c.gpsOn, c.gpsUpdateSecs,
             c.telUpdateSecs);
}

void ghostmesh_config_save()
{
    Preferences p;
    if (!p.begin(CFG_NS, false))
        return;
    GhostMeshConfig &c = ghostmesh_config;
    p.putUShort("prox", c.proxThresholdCm);
    p.putUShort("light", c.lightThreshold);
    p.putBool("led", c.notifyLed);
    p.putBool("buzz", c.notifyBuzz);
    p.putBool("vib", c.notifyVib);
    p.putBool("scr", c.outScreen);
    p.putBool("hbl", c.outHbled);
    p.putBool("gpl", c.outGpsled);
    p.putBool("rarm", c.repArm);
    p.putBool("rbuz", c.repBuzz);
    p.putBool("rvib", c.repVib);
    p.putBool("rled", c.repLed);
    p.putBool("rwip", c.repWipe);
    p.putBool("btlt", c.bcTilt);
    p.putBool("blit", c.bcLight);
    p.putBool("bprx", c.bcProx);
    p.putBool("rhlp", c.repHelp);
    p.putBool("rsta", c.repStatus);
    p.putBool("rerr", c.repErr);
    p.putBool("runk", c.repUnknown);
    p.putBool("rrun", c.repRun);
    p.putBool("itlt", c.inTilt);
    p.putBool("ilit", c.inLight);
    p.putBool("iprx", c.inProx);
    p.putBool("iir", c.inIr);
    p.putBool("gpso", c.gpsOn);
    p.putBool("telo", c.telOn);
    p.putUShort("gpsint", c.gpsUpdateSecs);
    p.putUShort("telint", c.telUpdateSecs);
    p.end();
}
