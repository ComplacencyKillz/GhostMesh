#pragma once
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

// GhostMesh IR remote module.
//
// Decodes the GhostMesh NECext IR command set (16-bit address 0x474D = 'GM', 8-bit command) from a
// VS1838B IR receiver on GPIO48. Commands:
//   0x01 ARM · 0x02 DISARM · 0x03 WIPE · 0x04 CONFIRM · 0x05 RUN
// ARM/DISARM set the shared `ghostmesh_armed` state (see GhostMeshArming.h) and broadcast
// ARMED/DISARMED over LoRa, alongside the slide-switch ArmingModule (last action wins). WIPE and
// CONFIRM drive an out-of-band destruct: the operator must send ARM → WIPE → CONFIRM in order,
// while armed, with CONFIRM inside a short window. That destruct path never touches the mesh, so
// it survives a compromised radio/key. Every decoded frame is logged (addr + cmd) so any remote
// can be identified. TX side: `flipper-app/GhostMeshBackpack.ir` (and, later, the FAP itself).
class IRModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    IRModule();

  protected:
    virtual int32_t runOnce() override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return false; }

  private:
    bool firstTime = true;
    bool irAttached = false; // is the GPIO48 falling-edge ISR currently attached? (in_ir gate)

    // ARM → WIPE → CONFIRM sequence state (the out-of-band destruct).
    uint8_t  wipeStep = 0; // 0 = idle, 1 = WIPE seen (armed), awaiting CONFIRM
    uint32_t wipeAt = 0;   // millis WIPE was seen, for the CONFIRM window

    void handleCommand(uint8_t cmd);
    void broadcastArmState(bool armed);
    void doFactoryWipe();
};

extern IRModule *irModule;
