#include "IRModule.h"
#include "GhostMeshArming.h"
#include "GhostMeshWipe.h"
#include "MeshService.h"
#include "configuration.h"
#include <Arduino.h>
#include <string.h>

IRModule *irModule;

// VS1838B IR receiver on GPIO48 (OUT->GPIO48, VCC->3.3V, GND->GND). Demodulated, active-low.
#define IR_PIN     48
#define IR_POLL_MS 50

// ── GhostMesh NECext command set ────────────────────────────────────────────────────
// 16-bit address = our namespace so random remotes / room noise never match. Commands select the
// action. Defined values (also in flipper-app/GhostMeshBackpack.ir and, later, the FAP TX):
#define GM_IR_ADDR    0x474Du // 'GM'
#define GM_IR_ARM     0x01u
#define GM_IR_DISARM  0x02u
#define GM_IR_WIPE    0x03u
#define GM_IR_CONFIRM 0x04u

// After WIPE, CONFIRM must arrive within this window or the destruct sequence resets.
#define IR_WIPE_WINDOW_MS 10000u

// ── NECext decode (falling-edge interval method) ──────────────────────────────────────
// From each falling edge, the gap to the next falling edge is: ~13.5 ms header, ~1.125 ms for a 0
// bit, ~2.25 ms for a 1 bit. 32 bits per frame, transmitted LSB-first: address_lo, address_hi,
// command_lo, command_hi (the Flipper's NECext sends a 16-bit command, high byte 0 — not cmd/~cmd).
// We accumulate LSB-first (bit N → position N), so the finished word is [cmd_hi][cmd_lo][addr]. We
// use only the low command byte + the 16-bit address.
// NEC repeat codes (holding a button) have a ~11.25 ms header gap and are ignored.
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

    uint32_t bit;
    if (gap > 2000 && gap < 2700)
        bit = 1;
    else if (gap > 900 && gap < 1400)
        bit = 0;
    else {
        ir_inFrame = false; // not a valid 0 or 1 -> glitch, abort this frame
        return;
    }
    ir_acc |= (bit << ir_bits); // LSB-first: first bit received is bit 0
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
        LOG_INFO("IR: init on GPIO%d (NECext addr 0x%04X)", IR_PIN, GM_IR_ADDR);
        return IR_POLL_MS;
    }

    if (ir_ready) {
        uint32_t raw = ir_decoded;
        ir_ready = false;
        uint16_t addr = raw & 0xFFFF;
        uint8_t  cmd  = (raw >> 16) & 0xFF;
        LOG_INFO("IR: addr 0x%04X cmd 0x%02X", addr, cmd); // point any remote here to identify it
        // Match on the 16-bit address only. The Flipper's NECext carries the command as a 16-bit
        // value (high byte 0), not the classic cmd/~cmd pair, so an inverse check would reject
        // every frame. The address is the namespace gate; handleCommand ignores unknown commands.
        if (addr == GM_IR_ADDR)
            handleCommand(cmd);
    }

    // Expire a stale WIPE so a much-later CONFIRM can't fire the destruct.
    if (wipeStep && (millis() - wipeAt) > IR_WIPE_WINDOW_MS)
        wipeStep = 0;

    return IR_POLL_MS;
}

void IRModule::handleCommand(uint8_t cmd)
{
    uint32_t now = millis();
    switch (cmd) {
    case GM_IR_ARM:
        ghostmesh_armed = true;
        wipeStep = 0; // (re)arming resets any pending destruct
        broadcastArmState(true);
        break;
    case GM_IR_DISARM:
        ghostmesh_armed = false;
        wipeStep = 0; // disarming cancels a pending destruct
        broadcastArmState(false);
        break;
    case GM_IR_WIPE:
        if (ghostmesh_armed) {
            wipeStep = 1;
            wipeAt = now;
            LOG_WARN("IR: WIPE armed - awaiting CONFIRM within %us", IR_WIPE_WINDOW_MS / 1000);
        } else {
            LOG_INFO("IR: WIPE ignored (not armed)");
        }
        break;
    case GM_IR_CONFIRM:
        if (wipeStep == 1 && ghostmesh_armed && (now - wipeAt) < IR_WIPE_WINDOW_MS) {
            wipeStep = 0;
            doFactoryWipe();
        } else {
            LOG_INFO("IR: CONFIRM ignored (no pending wipe / timed out / disarmed)");
            wipeStep = 0;
        }
        break;
    default:
        break;
    }
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

// ARM → WIPE → CONFIRM completed while armed → the out-of-band destruct: a complete flash erase
// (NVS + filesystem + firmware) that drops the chip to USB download mode. Does not return.
void IRModule::doFactoryWipe()
{
    LOG_WARN("IR: WIPE confirmed -> complete flash erase");
    ghostmesh_complete_wipe();
}
