#include "LightTamperModule.h"
#include "GhostMeshArming.h"
#include "GhostMeshConfig.h"
#include "MeshService.h"
#include "configuration.h"
#include "main.h"
#include <Arduino.h>
#include <string.h>

LightTamperModule *lightTamperModule;

// ── GhostMesh light-tamper config ─────────────────────────────────────────────
// GPIO5 = ADC1_CH4 on the Heltec V3. Photoresistor divider per the board schematic:
// 3.3V - 10k - GPIO5 - photoresistor - GND, so bright light LOWERS the ADC reading.
// "Light" therefore = reading BELOW the threshold.
#define LIGHT_TAMPER_PIN        5
// ADC reading (raw counts) BELOW which we consider it "light". CALIBRATE: watch the
// "LightTamper: raw=..." debug lines dark vs. exposed and set this partway between.
#define LIGHT_TAMPER_THRESHOLD  2000
// The reading must fall this far below the threshold to re-arm — debounces a photoresistor
// hovering near the edge so it doesn't spam on/off.
#define LIGHT_TAMPER_HYSTERESIS 200
#define LIGHT_POLL_INTERVAL_MS  500   // how often to sample the ADC
#define LIGHT_MIN_BROADCAST_MS  60000 // minimum interval between alerts (anti-spam)

LightTamperModule::LightTamperModule()
    : SinglePortModule("lighttamper", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("LightTamper")
{
}

int32_t LightTamperModule::runOnce()
{
    if (firstTime) {
        firstTime = false;
        ghostmesh_config_ensure_loaded();
        pinMode(LIGHT_TAMPER_PIN, INPUT);
        LOG_INFO("LightTamper: init on GPIO%d, threshold=%u", LIGHT_TAMPER_PIN,
                 ghostmesh_config.lightThreshold);
        return LIGHT_POLL_INTERVAL_MS;
    }

    uint16_t raw = analogRead(LIGHT_TAMPER_PIN);
    LOG_DEBUG("LightTamper: raw=%d", raw); // watch this to calibrate the threshold (/set light N)

    // Divider has the 10k on the 3.3V side and the photoresistor to GND, so bright = LOW reading.
    // Hysteresis: once "light", stay light until the reading rises well above the threshold.
    uint16_t thr = ghostmesh_config.lightThreshold; // live-tunable via the CLI
    bool isLight = wasLight ? (raw < thr + LIGHT_TAMPER_HYSTERESIS) : (raw < thr);

    // Fire only on the dark -> light transition, rate-limited, and only when armed.
    if (isLight && !wasLight && ghostmesh_armed) {
        uint32_t now = millis();
        if (lastSent == 0 || (now - lastSent) >= LIGHT_MIN_BROADCAST_MS) {
            broadcastTamperLight(raw);
            lastSent = now;
        }
    }
    wasLight = isLight;

    return LIGHT_POLL_INTERVAL_MS;
}

void LightTamperModule::broadcastTamperLight(uint16_t raw)
{
    meshtastic_MeshPacket *p = allocDataPacket(); // portnum = TEXT_MESSAGE_APP
    p->want_ack = false;
    const char *msg = "TAMPER_LIGHT";
    p->decoded.payload.size = strlen(msg);
    memcpy(p->decoded.payload.bytes, msg, p->decoded.payload.size);
    LOG_INFO("LightTamper: light detected (raw=%d) -> broadcast TAMPER_LIGHT", raw);
    service->sendToMesh(p, RX_SRC_LOCAL, true); // ccToPhone=true so a locally-attached Flipper/app also sees it
}
