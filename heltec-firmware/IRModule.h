#pragma once
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

// GhostMesh IR remote arm/disarm module.
//
// Decodes NEC-protocol codes from a VS1838B IR receiver on GPIO48 and, on the configured
// arm/disarm buttons, sets the shared `ghostmesh_armed` state (see GhostMeshArming.h) and
// broadcasts ARMED / DISARMED over LoRa. Works alongside the slide-switch ArmingModule —
// last action wins. Every decoded code is logged so the operator can pick which buttons to use.
class IRModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    IRModule();

  protected:
    virtual int32_t runOnce() override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return false; }

  private:
    bool firstTime = true;
    void broadcastArmState(bool armed);
};

extern IRModule *irModule;
