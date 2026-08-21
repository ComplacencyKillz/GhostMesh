#pragma once
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

// GhostMesh mesh command CLI.
//
// Unlike the other GhostMesh modules (which only *broadcast* events), this one *listens*: it
// parses plain-text commands sent over the private channel and drives the backpack's outputs.
// That is what lets any operator control any backpack over the mesh — sound its buzzer, run its
// vibration motor, arm/disarm it, query status, or wipe it — with no Flipper required.
//
// Command format (see docs/command-cli.md):   /command @target [args]
//   @target = our node's last 4 hex digits, or ALL. A node acts only if addressed.
//
// Threading: Meshtastic's router and OSThread both run in the main loop, so handleReceived()
// and runOnce() never preempt each other — no mutexes are needed here (unlike the FAP's ISR).
class CommandModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    CommandModule();

  protected:
    // We DO want incoming text: SinglePortModule's default wantPacket() already returns true for
    // our port (TEXT_MESSAGE_APP), so — unlike the broadcast-only modules that override it to
    // false — we simply don't opt out, and override handleReceived() to parse commands.
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    virtual int32_t runOnce() override; // output timing, reply pacing, physical wipe button

  private:
    bool firstTime = true;

    // ── non-blocking outputs: handleReceived requests, runOnce actuates on a timer ──
    uint32_t reqBuzzMs = 0, buzzUntil = 0;
    uint32_t reqVibrateMs = 0, vibrateUntil = 0;

    // ── spaced reply queue (so /help's 8 messages don't flood the airtime) ──
    static constexpr uint8_t kReplyQ = 12;
    char replyQ[kReplyQ][64];
    uint8_t replyHead = 0, replyTail = 0;
    uint32_t nextReplyAt = 0;

    // ── wipe state (defense in depth: node must be ARMED for every path) ──
    uint16_t wipeToken = 0;      // one-time mesh confirm token (0 = none outstanding)
    uint32_t wipeTokenAt = 0;    // millis the token was issued (for TTL)
    bool     wipePending = false; // set once confirmed; runOnce fires it after replies flush

    // ── physical wipe button (GPIO37): armed + double-press, 2nd press 2–5 s after 1st ──
    bool     btnPrev = false;
    uint32_t btnLastEdge = 0, btnFirstAt = 0;

    // ── Indicator effect engine (synced LED + buzzer + vibration) ──
    // An "effect" is a timeline of colour ramps with optional tone + vibration; runOnce steps it
    // non-blocking. Events (arm/disarm/wipe/message/CLI) each map to an effect. When no effect is
    // active the LED holds `steady*` (default off — covert), and the plain /buzz//vibrate timers run.
    uint8_t  curFx = 0;            // active effect id (0 = FX_NONE / idle)
    uint8_t  fxIdx = 0;           // current segment
    uint32_t fxSegStart = 0;
    bool     fxSegEntered = false; // has this segment's tone/vib been applied?
    uint8_t  steadyR = 0, steadyG = 0, steadyB = 0; // idle LED colour, restored after an effect
    // Per-channel enable (the covert toggle) lives in the persisted config: ghostmesh_config.notify*
    bool     lastArmedSeen = false; // arm-state edge detection → arm/disarm effects
    bool     eraseArmed = false;    // a wipe is scheduled once its effect finishes
    uint32_t eraseAt = 0;

    // helpers
    void handleCommandText(char *text, uint32_t from);
    bool targetsMe(const char *tgt);
    void doHelp();
    void doStatus();
    void doLed(const char *arg);
    void doFx(const char *arg);          // /fx <name> — play an effect for tuning (no side effects)
    void doSet(const char *key, const char *val); // /set <key> <val> — tune + persist config
    void doCfg();                        // /cfg — reply the current config
    // indicator engine
    void startEffect(uint8_t fx);
    void tickEffect(uint32_t now);
    void stopEffect();                   // → idle, outputs off
    void setSteadyLed(uint8_t r, uint8_t g, uint8_t b);
    void doWipeCommand(const char *arg);
    void serviceWipeButton(uint32_t now);
    void doFactoryWipe();
    void enqueueReply(const char *msg);
    void sendText(const char *msg);

    // ── payload file transfer (/put) — defined in CommandModule_payload.cpp ──
    // The web configurator's file uploader. Chunked base64 over the PROTO text channel (the only
    // channel a Meshtastic node exposes on serial), reassembled to LittleFS and CRC32-verified.
    void handlePut(char *text);            // dispatch a '/put ...' line addressed to us
    void putBegin(char *save);             // open file, reset bitmap, size checks
    void putData(char *save);              // one base64 data chunk -> flash (silent)
    void putEnd(char *save);               // completeness + size + CRC32 -> reply ok/need/fail
    void servicePutTimeout(uint32_t now);  // called from runOnce: abort a stalled transfer
};

extern CommandModule *commandModule;
