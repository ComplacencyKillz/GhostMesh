#pragma once
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

// GhostMesh light-tamper module.
//
// Watches a photoresistor voltage divider on an ADC pin (Heltec GPIO5) and broadcasts a
// "TAMPER_LIGHT" text message over the mesh when the light level rises above a threshold —
// i.e. the case is opened / the deployed node is exposed to light.
//
// Sent as a plain TEXT_MESSAGE_APP packet so it surfaces on both the Meshtastic app and the
// GhostMesh FAP (which decodes text). Broadcasting over LoRa (not the serial link) is what
// lets a deployed backpack alert an operator who is nowhere near it.
class LightTamperModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    LightTamperModule();

  protected:
    virtual int32_t runOnce() override;

    // Send-only module: never consume incoming packets.
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return false; }

  private:
    bool     firstTime = true;
    bool     wasLight  = false; // last observed state, for dark->light edge detection
    uint32_t lastSent  = 0;     // millis() of last broadcast, for rate limiting
    void broadcastTamperLight(uint16_t raw);
};

extern LightTamperModule *lightTamperModule;
