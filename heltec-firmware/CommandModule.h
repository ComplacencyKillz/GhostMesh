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

    // ── spaced reply queue (so /help's messages don't flood the airtime) ──
    // Slot is 96 (not 64) so the compact /cfg bitmask line (~74 chars) isn't truncated at source.
    // Depth 16: /help alone is 14 lines, and /ls can enqueue one line per staged payload on top of
    // whatever's already queued — keep headroom rather than silently dropping the tail (see
    // enqueueReply: a full queue drops, it doesn't block).
    static constexpr uint8_t kReplyQ = 16;
    char replyQ[kReplyQ][96];
    uint32_t replyToQ[kReplyQ] = {0}; // per-reply destination (see enqueueReply / runOnce drain)
    uint8_t replyHead = 0, replyTail = 0;
    uint32_t nextReplyAt = 0;
    // Where the reply for the command currently being handled should go. Set from the command's
    // sender in handleCommandText: our own node num ⇒ the reply is for the local USB/serial client
    // (web configurator / FAP) and goes phone-only, no LoRa; a remote node ⇒ directed unicast back to
    // it; broadcast ⇒ an unsolicited/physical event (e.g. button wipe) that everyone should see.
    uint32_t curReplyTo = 0xFFFFFFFFu; // == NODENUM_BROADCAST (literal so the header needs no mesh include)

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
    // /out* (screen/hbled/gpsled) — and the `silent` master flips all six physical outputs at once.
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
    void doCfg();                        // /cfg — reply the current config (bitmask form)
    void applyOutputState();             // (re)apply screen / heartbeat-LED / RGB / GPS-LED from config
    // indicator engine
    void startEffect(uint8_t fx);
    void tickEffect(uint32_t now);
    void stopEffect();                   // → idle, outputs off
    void setSteadyLed(uint8_t r, uint8_t g, uint8_t b);
    void doWipeCommand(const char *arg);
    void serviceWipeButton(uint32_t now);
    void doFactoryWipe();
    // /run @id <name> — the Heltec never executes anything; this just accepts/denies (armed-gated)
    // so the requester gets confirmation, and — since the raw text still flows through to the app
    // view (see handleReceived's CONTINUE) — the FAP wired to id sees the same line and can offer to
    // launch the named payload via Bad USB. See docs/command-cli.md and payloads/README.md.
    void doRun(const char *name);
    void enqueueReply(const char *msg);
    void sendText(const char *msg);
    void sendTextTo(const char *msg, uint32_t to);  // like sendText but to a specific node
    void sendTextToPhone(const char *msg);          // deliver ONLY to the USB/BLE client — no LoRa TX

    // ── payload file transfer (/put, /get, /ls) — defined in CommandModule_payload.cpp ──
    // The web configurator's file uploader (/put) and its mirror-image downloader (/get), plus a
    // directory listing (/ls). Chunked base64 over the PROTO text channel (the only channel a
    // Meshtastic node exposes on serial), reassembled/read from LittleFS, CRC32-verified. Both
    // directions are stop-and-wait: one side paces the other with a per-chunk ack so a burst never
    // overruns serial ingest (see the file's top comment for the full protocol + rationale).
    void handlePut(char *text);            // dispatch a '/put ...' line addressed to us
    void putBegin(char *save);             // open file, reset progress, size checks
    void putData(char *save);              // one in-order base64 chunk -> append, queue an ack
    void putEnd(char *save);               // size + CRC32 -> reply ok/need/fail
    void servicePutAck();                  // called from runOnce: emit a pending chunk ack (fast)
    void servicePutTimeout(uint32_t now);  // called from runOnce: abort a stalled transfer

    void doLs();                           // /ls @id -> one reply line per file in /ghostmesh/, then LS end
    void handleGet(char *text, uint32_t from); // dispatch a '/get ...' line addressed to us
    void getBegin(char *save, uint32_t from);  // open + CRC the requested file, send GET begin + chunk 0
    void getAck(char *save);                   // client acked a chunk -> queue the next send (or finish)
    void serviceGetSend();                     // called from runOnce: flush a pending chunk/finish line
    void serviceGetTimeout(uint32_t now);      // called from runOnce: abort a stalled download
    void getSendRouted(const char *msg);       // route a GET reply: phone-only if the requester is us, else unicast
};

extern CommandModule *commandModule;
