#include "IRModule.h"
#include "GhostMeshArming.h"
#include "MeshService.h"
#include "configuration.h"
#include "main.h"
#include <Arduino.h>
#include <string.h>

IRModule *irModule;

// VS1838B IR receiver on GPIO48 (OUT->GPIO48, VCC->3.3V, GND->GND). Demodulated, active-low.
#define IR_PIN      48
#define IR_POLL_MS  50

// NEC button codes for arm / disarm. CALIBRATE: flash this, point your remote, read the
// "IR: code 0x........" serial lines, then set these to the two buttons you want and rebuild.
#define IR_ARM_CODE    0x7D82857Au
#define IR_DISARM_CODE 0x7D8245BAu

// ── NEC decode (falling-edge interval method) ─────────────────────────────────
// From each falling edge, the gap to the next falling edge is: ~13.5 ms header,
// ~1.125 ms for a 0 bit, ~2.25 ms for a 1 bit. 32 bits per frame; NEC repeat codes
// (holding a button) have a ~11.25 ms header gap and are ignored.
static volatile uint32_t ir_lastFall = 0;
static volatile uint32_t ir_acc      = 0;
static volatile uint8_t  ir_bits     = 0;
static volatile bool     ir_inFrame  = false;
static volatile uint32_t ir_decoded  = 0;
static volatile bool     ir_ready    = false;

static void IRAM_ATTR ir_isr()
{
    uint32_t now = micros();
    uint32_t gap = now - ir_lastFall;
    ir_lastFall = now;

    if (gap > 12000 && gap < 15000) { // frame header
        ir_inFrame = true;
        ir_bits = 0;
        ir_acc = 0;
        return;
    }
    if (!ir_inFrame)
        return;

    ir_acc <<= 1;
    if (gap > 2000 && gap < 2700) {
        ir_acc |= 1; // 1 bit
    } else if (!(gap > 900 && gap < 1400)) {
        ir_inFrame = false; // not a valid 0 or 1 -> glitch, abort this frame
        return;
    }
    if (++ir_bits == 32) {
        ir_decoded = ir_acc;
        ir_ready = true;
        ir_inFrame = false;
    }
}

IRModule::IRModule()
    : SinglePortModule("ir", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("IR")
{
}

int32_t IRModule::runOnce()
{
    if (firstTime) {
        firstTime = false;
        pinMode(IR_PIN, INPUT);
        attachInterrupt(digitalPinToInterrupt(IR_PIN), ir_isr, FALLING);
        LOG_INFO("IR: init on GPIO%d", IR_PIN);
        return IR_POLL_MS;
    }

    if (ir_ready) {
        uint32_t code = ir_decoded;
        ir_ready = false;
        LOG_INFO("IR: code 0x%08X", code); // read this to pick arm/disarm buttons
        if (code == IR_ARM_CODE && IR_ARM_CODE != 0) {
            ghostmesh_armed = true;
            broadcastArmState(true);
        } else if (code == IR_DISARM_CODE && IR_DISARM_CODE != 0) {
            ghostmesh_armed = false;
            broadcastArmState(false);
        }
    }
    return IR_POLL_MS;
}

void IRModule::broadcastArmState(bool armed)
{
    meshtastic_MeshPacket *p = allocDataPacket(); // portnum = TEXT_MESSAGE_APP
    p->want_ack = false;
    const char *msg = armed ? "ARMED" : "DISARMED";
    p->decoded.payload.size = strlen(msg);
    memcpy(p->decoded.payload.bytes, msg, p->decoded.payload.size);
    LOG_INFO("IR: %s (remote) -> broadcast", msg);
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}
