#include "TiltModule.h"
#include "GhostMeshArming.h"
#include "GhostMeshConfig.h"
#include "MeshService.h"
#include "configuration.h"
#include "main.h"
#include <Arduino.h>
#include <string.h>

TiltModule *tiltModule;

// SW-520D tilt switch on GPIO2, external pull-down per the board schematic: common -> 3.3V,
// junction -> GPIO2, 10k -> GND. So idle (open) reads LOW, disturbed (closed) reads HIGH.
// Any level change = movement. Plain INPUT (the external 10k does the pull-down).
#define TILT_PIN            2
#define TILT_POLL_MS        100   // fast poll to catch a movement edge
#define TILT_MIN_BROADCAST_MS 30000 // anti-spam: at most one TAMPER per 30 s
#define TILT_DISABLED_MS    3000  // when in_tilt is off: idle poll so a re-enable is still noticed

TiltModule::TiltModule()
    : SinglePortModule("tilt", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("Tilt")
{
}

int32_t TiltModule::runOnce()
{
    if (firstTime) {
        firstTime = false;
        pinMode(TILT_PIN, INPUT);
        lastState = digitalRead(TILT_PIN);
        ghostmesh_config_ensure_loaded();
        LOG_INFO("Tilt: init on GPIO%d", TILT_PIN);
        return TILT_POLL_MS;
    }

    if (!ghostmesh_config.inTilt) return TILT_DISABLED_MS; // sensor disabled — skip the read (battery)

    bool state = digitalRead(TILT_PIN);
    if (state != lastState) { // movement (either edge)
        lastState = state;
        if (ghostmesh_armed) {
            uint32_t now = millis();
            if (lastSent == 0 || (now - lastSent) >= TILT_MIN_BROADCAST_MS) {
                broadcastTamper();
                lastSent = now;
            }
        }
    }
    return TILT_POLL_MS;
}

void TiltModule::broadcastTamper()
{
    if (!ghostmesh_config.bcTilt) return; // TAMPER announce gated by bc_tilt (default on)
    meshtastic_MeshPacket *p = allocDataPacket(); // portnum = TEXT_MESSAGE_APP
    p->want_ack = false;
    const char *msg = "TAMPER";
    p->decoded.payload.size = strlen(msg);
    memcpy(p->decoded.payload.bytes, msg, p->decoded.payload.size);
    LOG_INFO("Tilt: movement -> broadcast TAMPER");
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}
