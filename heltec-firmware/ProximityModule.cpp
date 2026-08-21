#include "ProximityModule.h"
#include "GhostMeshArming.h"
#include "GhostMeshConfig.h"
#include "MeshService.h"
#include "configuration.h"
#include "main.h"
#include <Arduino.h>
#include <string.h>

ProximityModule *proximityModule;

// ── GhostMesh proximity (RCWL-1601) config ────────────────────────────────────
// Deploy sensor: RCWL-1601 — Trig=GPIO38, Echo=GPIO47, VCC=3.3V, GND=GND. It's 3.3V-native, so
// Echo is already 3.3V logic — no divider needed. (A plain HC-SR04 works on the same pins but
// needs 5V + a divider on Echo; the battery backpack has no 5V, hence the RCWL-1601.)
#define PROX_TRIG_PIN         38
#define PROX_ECHO_PIN         47
// Distance (cm) inside which we call it "person detected". CALIBRATE: watch the
// "Proximity: N cm" debug lines and set this to the trip range you want.
#define PROX_THRESHOLD_CM     200
// Must move this far back beyond the threshold to re-arm — debounces a target near the edge.
#define PROX_HYSTERESIS_CM    20
#define PROX_ECHO_TIMEOUT_US  30000 // ~5 m; pulseIn gives up after this (no echo -> out of range)
#define PROX_POLL_MS          1000  // ping once per second
#define PROX_MIN_BROADCAST_MS 60000 // minimum interval between alerts (anti-spam)

ProximityModule::ProximityModule()
    : SinglePortModule("proximity", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("Proximity")
{
}

// One HC-SR04 ping. Returns distance in cm, or -1 if nothing echoed back (out of range).
long ProximityModule::measureCm()
{
    digitalWrite(PROX_TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(PROX_TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(PROX_TRIG_PIN, LOW);
    unsigned long us = pulseIn(PROX_ECHO_PIN, HIGH, PROX_ECHO_TIMEOUT_US);
    if (us == 0)
        return -1;
    return (long)(us / 58); // speed of sound: ~58 us per cm round-trip
}

int32_t ProximityModule::runOnce()
{
    if (firstTime) {
        firstTime = false;
        ghostmesh_config_ensure_loaded();
        pinMode(PROX_TRIG_PIN, OUTPUT);
        pinMode(PROX_ECHO_PIN, INPUT);
        digitalWrite(PROX_TRIG_PIN, LOW);
        LOG_INFO("Proximity: init trig=GPIO%d echo=GPIO%d threshold=%ucm", PROX_TRIG_PIN, PROX_ECHO_PIN,
                 ghostmesh_config.proxThresholdCm);
        return PROX_POLL_MS;
    }

    long cm = measureCm();
    LOG_DEBUG("Proximity: %ld cm", cm); // watch this to tune the threshold (/set prox N)

    uint16_t thr = ghostmesh_config.proxThresholdCm; // live-tunable via the CLI
    bool isNear;
    if (cm < 0) {
        isNear = false; // no echo -> nothing in range
    } else {
        isNear = wasNear ? (cm < thr + PROX_HYSTERESIS_CM) : (cm < thr);
    }

    // Fire only on the far -> near transition, rate-limited, and only when armed.
    if (isNear && !wasNear && ghostmesh_armed) {
        uint32_t now = millis();
        if (lastSent == 0 || (now - lastSent) >= PROX_MIN_BROADCAST_MS) {
            broadcastPersonDetected(cm);
            lastSent = now;
        }
    }
    wasNear = isNear;

    return PROX_POLL_MS;
}

void ProximityModule::broadcastPersonDetected(long cm)
{
    meshtastic_MeshPacket *p = allocDataPacket(); // portnum = TEXT_MESSAGE_APP
    p->want_ack = false;
    const char *msg = "PERSON_DETECTED";
    p->decoded.payload.size = strlen(msg);
    memcpy(p->decoded.payload.bytes, msg, p->decoded.payload.size);
    LOG_INFO("Proximity: person at %ld cm -> broadcast PERSON_DETECTED", cm);
    service->sendToMesh(p, RX_SRC_LOCAL, true); // ccToPhone=true so a locally-attached Flipper/app also sees it
}
