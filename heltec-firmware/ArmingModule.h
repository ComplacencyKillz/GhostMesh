#pragma once
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

// GhostMesh arming module.
//
// Reads the operator slide switch (SPDT on GPIO4) as a TOGGLE: any flip inverts the shared
// `ghostmesh_armed` flag that gates the tamper modules, and broadcasts "ARMED"/"DISARMED". The
// switch position is NOT tied to a state, so it can never disagree with an IR/mesh arm/disarm —
// every input just flips/sets the one shared flag, last action wins.
class ArmingModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    ArmingModule();

  protected:
    virtual int32_t runOnce() override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return false; }

  private:
    bool firstTime = true;
    bool lastLevel = false; // last raw switch reading; a change = a flip = toggle
    void broadcastArmState(bool armed);
};

extern ArmingModule *armingModule;
