#pragma once
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

// GhostMesh tilt-tamper module.
//
// Custom replacement for Meshtastic's built-in Detection Sensor (which must be DISABLED in
// the app when this runs). Watches the SW-520D tilt switch on GPIO2 and broadcasts "TAMPER"
// over LoRa on any movement — but only when the backpack is ARMED (see GhostMeshArming.h).
// Sent as plain text so it shows on the app and the GhostMesh FAP.
class TiltModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    TiltModule();

  protected:
    virtual int32_t runOnce() override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return false; }

  private:
    bool     firstTime = true;
    bool     lastState = false; // last raw pin level, for edge (movement) detection
    uint32_t lastSent  = 0;
    void broadcastTamper();
};

extern TiltModule *tiltModule;
