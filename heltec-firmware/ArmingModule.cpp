#include "ArmingModule.h"
#include "GhostMeshArming.h"
#include "MeshService.h"
#include "configuration.h"
#include "main.h"
#include <Arduino.h>
#include <string.h>

ArmingModule *armingModule;

// Shared arming flag. Default DISARMED at boot (safe — no spurious alerts until the switch
// is read). ArmingModule reads the physical switch at init and sets the real state.
volatile bool ghostmesh_armed = false;

// Slide switch (SPDT) on GPIO4 per the board schematic: common -> GPIO4, one throw -> 3.3V
// (armed = HIGH), the other -> GND. It's SPDT so it always drives the pin — no pull needed.
// If ARMED/DISARMED come out backwards, just swap the two throw wires (3.3V <-> GND).
#define ARM_PIN         4
#define ARM_ACTIVE_HIGH true
#define ARM_POLL_MS     250

ArmingModule::ArmingModule()
    : SinglePortModule("arming", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("Arming")
{
}

int32_t ArmingModule::runOnce()
{
    if (firstTime) {
        firstTime = false;
        pinMode(ARM_PIN, INPUT);
        bool level = digitalRead(ARM_PIN);
        lastArmed = ARM_ACTIVE_HIGH ? level : !level;
        ghostmesh_armed = lastArmed;
        LOG_INFO("Arming: init on GPIO%d -> %s", ARM_PIN, lastArmed ? "ARMED" : "DISARMED");
        return ARM_POLL_MS;
    }

    bool level = digitalRead(ARM_PIN);
    bool armed = ARM_ACTIVE_HIGH ? level : !level;
    if (armed != lastArmed) {
        lastArmed = armed;
        ghostmesh_armed = armed;
        broadcastArmState(armed);
    }
    return ARM_POLL_MS;
}

void ArmingModule::broadcastArmState(bool armed)
{
    meshtastic_MeshPacket *p = allocDataPacket(); // portnum = TEXT_MESSAGE_APP
    p->want_ack = false;
    const char *msg = armed ? "ARMED" : "DISARMED";
    p->decoded.payload.size = strlen(msg);
    memcpy(p->decoded.payload.bytes, msg, p->decoded.payload.size);
    LOG_INFO("Arming: %s -> broadcast", msg);
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}
