#pragma once
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

// GhostMesh arming module.
//
// Reads the operator slide switch (SPDT on GPIO4) and maintains the shared `ghostmesh_armed`
// state that gates the tamper modules. Broadcasts "ARMED" / "DISARMED" over LoRa when the
// switch is flipped so the operator sees the state on the app / FAP.
class ArmingModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    ArmingModule();

  protected:
    virtual int32_t runOnce() override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return false; }

  private:
    bool firstTime = true;
    bool lastArmed = false;
    void broadcastArmState(bool armed);
};

extern ArmingModule *armingModule;
