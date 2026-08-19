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

// Slide switch (SPDT) on GPIO4: common -> GPIO4, throws -> 3.3V and GND. Read as a TOGGLE — the
// position doesn't map to a state; each flip just inverts ghostmesh_armed. Boot is DISARMED (the
// safe default in GhostMeshArming.cpp) regardless of the switch's physical position.
#define ARM_PIN     4
#define ARM_POLL_MS 250

ArmingModule::ArmingModule()
    : SinglePortModule("arming", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("Arming")
{
}

int32_t ArmingModule::runOnce()
{
    if (firstTime) {
        firstTime = false;
        pinMode(ARM_PIN, INPUT);
        lastLevel = digitalRead(ARM_PIN); // remember the current position; leave the state alone
        LOG_INFO("Arming: init on GPIO%d (toggle mode), boot DISARMED", ARM_PIN);
        return ARM_POLL_MS;
    }

    bool level = digitalRead(ARM_PIN);
    if (level != lastLevel) { // flipped (either direction) -> toggle the shared state
        lastLevel = level;
        ghostmesh_armed = !ghostmesh_armed;
        broadcastArmState(ghostmesh_armed);
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
