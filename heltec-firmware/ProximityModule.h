#pragma once
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

// GhostMesh proximity module (dead-drop surveillance).
//
// Pings an HC-SR04 ultrasonic sensor and broadcasts "PERSON_DETECTED" over LoRa when
// something comes within a threshold distance. Sent as a plain TEXT_MESSAGE_APP packet so it
// surfaces on both the Meshtastic app and the GhostMesh FAP. Broadcasting over the mesh lets a
// deployed backpack alert an operator who is nowhere near it.
class ProximityModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    ProximityModule();

  protected:
    virtual int32_t runOnce() override;

    // Send-only module: never consume incoming packets.
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return false; }

  private:
    bool     firstTime = true;
    bool     wasNear   = false; // last state, for far->near edge detection
    uint32_t lastSent  = 0;     // millis() of last broadcast, for rate limiting
    long     measureCm();
    void     broadcastPersonDetected(long cm);
};

extern ProximityModule *proximityModule;
