#include "CommandModule.h"
#include "GhostMeshArming.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "mesh/MeshTypes.h" // getFrom(), isFromUs()
#include "PowerStatus.h"
#include "configuration.h"
#include "main.h"
#include <Arduino.h>
#include <string.h>
#include <strings.h> // strcasecmp
#include <stdlib.h>
#include <esp_random.h>

CommandModule *commandModule;

// ── Backpack output pins (verified against the board header photo, heltec_front_back/) ──
#define BUZZER_PIN   39   // passive buzzer via PN2222 low-side driver — needs a PWM tone, not DC
#define VIBRATE_PIN  40   // vibration motor via PN2222 low-side driver — plain on/off
#define LED_PIN      35   // PLACEHOLDER: onboard white LED. The real RGB (SK6812 on GPIO26) is
                          //   not wired yet; /led toggles this until it is.
#define WIPE_BTN_PIN 37   // tact switch to GND; INPUT_PULLUP, so a press reads LOW

#define BUZZ_FREQ         2000  // Hz — passive buzzers are loudest in the 2–4 kHz range
#define CMD_POLL_MS       50    // 20 Hz: responsive button + tight output/reply timing
#define REPLY_SPACING_MS  800   // gap between queued replies, to respect LoRa airtime
#define WIPE_TOKEN_TTL_MS 30000 // mesh confirm token is valid ~30 s
#define WIPE_DBL_MIN_MS   2000  // physical double-press: 2nd press no sooner than 2 s…
#define WIPE_DBL_MAX_MS   5000  //   …and no later than 5 s after the 1st
#define BTN_DEBOUNCE_MS   60

CommandModule::CommandModule()
    : SinglePortModule("command", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("Command")
{
}

// ── RX: called by the router for every decoded TEXT_MESSAGE_APP packet ──────────────
ProcessMessage CommandModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Ignore our own traffic — we broadcast replies and events on this same port.
    if (isFromUs(&mp))
        return ProcessMessage::CONTINUE;

    const meshtastic_Data &d = mp.decoded;
    if (d.payload.size == 0)
        return ProcessMessage::CONTINUE;

    // Copy to a mutable, NUL-terminated buffer for the tokenizer.
    char text[232];
    size_t n = d.payload.size < sizeof(text) - 1 ? d.payload.size : sizeof(text) - 1;
    memcpy(text, d.payload.bytes, n);
    text[n] = '\0';

    handleCommandText(text, getFrom(&mp));
    return ProcessMessage::CONTINUE; // let other modules (e.g. the app text view) see it too
}

// ── Parse "/command @target [arg]" and dispatch ─────────────────────────────────────
void CommandModule::handleCommandText(char *text, uint32_t from)
{
    (void)from; // replies are broadcast, not directed — every operator sees the response

    char *s = text;
    while (*s == ' ')
        s++;
    if (*s != '/')
        return; // not a command — ordinary chat / other modules' events pass through untouched

    char *save = nullptr;
    char *cmd = strtok_r(s, " ", &save);      // "/help"
    char *tgt = strtok_r(nullptr, " ", &save); // "@f69c" / "@ALL" / "ALL"
    char *arg = strtok_r(nullptr, " ", &save); // optional: ms, token, color…
    if (!cmd)
        return;

    // Commands must be addressed. If we're not the target (and it's not ALL), stay silent — this
    // is what keeps only the intended backpack (and not the whole mesh) acting and replying.
    if (!targetsMe(tgt))
        return;

    if (strcasecmp(cmd, "/help") == 0) {
        doHelp();
    } else if (strcasecmp(cmd, "/status") == 0) {
        doStatus();
    } else if (strcasecmp(cmd, "/arm") == 0) {
        ghostmesh_armed = true; // NOTE: the physical slide switch overrides on its next toggle
        enqueueReply("ARMED");
    } else if (strcasecmp(cmd, "/disarm") == 0) {
        ghostmesh_armed = false;
        enqueueReply("DISARMED");
    } else if (strcasecmp(cmd, "/buzz") == 0) {
        uint32_t ms = arg ? (uint32_t)atoi(arg) : 300;
        if (ms > 5000)
            ms = 5000; // cap so a typo can't wail forever
        reqBuzzMs = ms ? ms : 300;
        char r[32];
        snprintf(r, sizeof(r), "BUZZ %ums", (unsigned)reqBuzzMs);
        enqueueReply(r);
    } else if (strcasecmp(cmd, "/vibrate") == 0) {
        uint32_t ms = arg ? (uint32_t)atoi(arg) : 500;
        if (ms > 5000)
            ms = 5000;
        reqVibrateMs = ms ? ms : 500;
        char r[32];
        snprintf(r, sizeof(r), "VIBRATE %ums", (unsigned)reqVibrateMs);
        enqueueReply(r);
    } else if (strcasecmp(cmd, "/led") == 0) {
        doLed(arg);
    } else if (strcasecmp(cmd, "/wipe") == 0) {
        doWipeCommand(tgt, arg);
    } else {
        enqueueReply("? unknown cmd — try /help");
    }
}

// ── Targeting: does @target address this node? ──────────────────────────────────────
bool CommandModule::targetsMe(const char *tgt)
{
    if (!tgt)
        return false;
    const char *t = (tgt[0] == '@') ? tgt + 1 : tgt;
    if (strcasecmp(t, "ALL") == 0)
        return true;
    char me[8];
    snprintf(me, sizeof(me), "%04x", (unsigned)(nodeDB->getNodeNum() & 0xFFFF));
    return strcasecmp(t, me) == 0;
}

bool CommandModule::targetIsAll(const char *tgt)
{
    if (!tgt)
        return false;
    const char *t = (tgt[0] == '@') ? tgt + 1 : tgt;
    return strcasecmp(t, "ALL") == 0;
}

// ── /help: one message PER command (mesh text caps at ~200 chars, so we never cram) ──
void CommandModule::doHelp()
{
    static const char *lines[] = {
        "/help @id - this list",
        "/status @id - armed, battery, uptime",
        "/arm @id - arm the node",
        "/disarm @id - disarm the node",
        "/led @id <color|off> - status LED",
        "/buzz @id [ms] - sound buzzer",
        "/vibrate @id [ms] - run vibration",
        "/wipe @id - factory reset (armed+confirm)",
    };
    for (const char *l : lines)
        enqueueReply(l);
}

// ── /status: current node state ─────────────────────────────────────────────────────
void CommandModule::doStatus()
{
    char me[8];
    snprintf(me, sizeof(me), "%04x", (unsigned)(nodeDB->getNodeNum() & 0xFFFF));
    unsigned bat = powerStatus ? powerStatus->getBatteryChargePercent() : 0;
    char r[64];
    snprintf(r, sizeof(r), "STATUS %s: %s bat %u%% up %us", me, ghostmesh_armed ? "ARMED" : "DISARMED",
             bat, (unsigned)(millis() / 1000));
    enqueueReply(r);
}

// ── /led: placeholder on the onboard LED until the RGB on GPIO26 is wired ────────────
void CommandModule::doLed(const char *arg)
{
    bool off = arg && (strcasecmp(arg, "off") == 0 || strcmp(arg, "0") == 0);
    digitalWrite(LED_PIN, off ? LOW : HIGH);
    char r[40];
    snprintf(r, sizeof(r), "LED %s", off ? "off" : (arg ? arg : "on"));
    enqueueReply(r);
}

// ── /wipe: two-step mesh path — issue a one-time token, then verify it. Armed-gated. ─
void CommandModule::doWipeCommand(const char *tgt, const char *arg)
{
    if (targetIsAll(tgt)) {
        enqueueReply("WIPE needs a node id, not ALL");
        return;
    }
    if (!ghostmesh_armed) {
        enqueueReply("WIPE denied: not armed");
        return;
    }

    if (!arg) {
        // Stage 1: mint a fresh random token. esp_random() is a true hardware RNG, so the token
        // can't be predicted or replayed.
        wipeToken = (uint16_t)(esp_random() & 0xFFFF);
        if (wipeToken == 0)
            wipeToken = 0xA3F9; // 0 means "none outstanding"; avoid it as a real token
        wipeTokenAt = millis();
        char r[28];
        snprintf(r, sizeof(r), "WIPE confirm: send %04X", wipeToken);
        enqueueReply(r);
        return;
    }

    // Stage 2: verify the echoed token.
    if (wipeToken == 0 || (millis() - wipeTokenAt) > WIPE_TOKEN_TTL_MS) {
        wipeToken = 0;
        enqueueReply("WIPE: token expired, retry");
        return;
    }
    uint16_t got = (uint16_t)strtoul(arg, nullptr, 16);
    if (got != wipeToken) {
        enqueueReply("WIPE: bad token");
        return;
    }
    wipeToken = 0;
    enqueueReply("WIPING");
    wipePending = true; // runOnce fires it once this reply has actually gone out
}

// ── Physical wipe button: armed + double-press with a 2–5 s gap ──────────────────────
void CommandModule::serviceWipeButton(uint32_t now)
{
    bool pressed = (digitalRead(WIPE_BTN_PIN) == LOW); // INPUT_PULLUP → pressed reads LOW

    // Debounced falling edge = one clean press.
    if (pressed && !btnPrev && (now - btnLastEdge) > BTN_DEBOUNCE_MS) {
        btnLastEdge = now;
        if (btnFirstAt == 0) {
            btnFirstAt = now; // first press — start the window
        } else {
            uint32_t dt = now - btnFirstAt;
            if (ghostmesh_armed && dt > WIPE_DBL_MIN_MS && dt < WIPE_DBL_MAX_MS) {
                LOG_WARN("Command: WIPE via physical double-press");
                enqueueReply("WIPING (button)");
                wipePending = true;
            }
            btnFirstAt = 0; // reset whether it fired or was mistimed
        }
    }
    btnPrev = pressed;

    // A lone first press ages out after the window, so a stray tap can't arm a later one.
    if (btnFirstAt && (now - btnFirstAt) > WIPE_DBL_MAX_MS)
        btnFirstAt = 0;
}

// ── The nuke: Meshtastic factory reset wipes channel keys + config, then reboots ─────
void CommandModule::doFactoryWipe()
{
    LOG_WARN("Command: FACTORY WIPE — erasing channel keys + config");
    nodeDB->factoryReset();          // clears config/channels (and reboots on current firmware)
    rebootAtMsec = millis() + 2000;  // belt-and-suspenders reboot in case it didn't
}

// ── Reply plumbing ──────────────────────────────────────────────────────────────────
void CommandModule::enqueueReply(const char *msg)
{
    uint8_t next = (uint8_t)((replyTail + 1) % kReplyQ);
    if (next == replyHead)
        return; // queue full — drop (only happens under a flood; benign)
    strncpy(replyQ[replyTail], msg, sizeof(replyQ[0]) - 1);
    replyQ[replyTail][sizeof(replyQ[0]) - 1] = '\0';
    replyTail = next;
}

void CommandModule::sendText(const char *msg)
{
    meshtastic_MeshPacket *p = allocDataPacket(); // portnum = TEXT_MESSAGE_APP
    p->want_ack = false;
    size_t n = strlen(msg);
    if (n > sizeof(p->decoded.payload.bytes))
        n = sizeof(p->decoded.payload.bytes);
    p->decoded.payload.size = n;
    memcpy(p->decoded.payload.bytes, msg, n);
    service->sendToMesh(p, RX_SRC_LOCAL, true); // ccToPhone=true → also reaches the FAP/app
}

// ── Main loop: actuate outputs on a timer, pace replies, watch the wipe button ───────
int32_t CommandModule::runOnce()
{
    uint32_t now = millis();

    if (firstTime) {
        firstTime = false;
        // Drive every output LOW before anything can request it, so nothing twitches at boot.
        pinMode(BUZZER_PIN, OUTPUT);
        digitalWrite(BUZZER_PIN, LOW);
        pinMode(VIBRATE_PIN, OUTPUT);
        digitalWrite(VIBRATE_PIN, LOW);
        pinMode(LED_PIN, OUTPUT);
        digitalWrite(LED_PIN, LOW);
        pinMode(WIPE_BTN_PIN, INPUT_PULLUP);
        LOG_INFO("Command: init (buzz %d, vibrate %d, led %d, wipe-btn %d)", BUZZER_PIN, VIBRATE_PIN, LED_PIN,
                 WIPE_BTN_PIN);
        return CMD_POLL_MS;
    }

    // Buzzer — passive, so a PWM tone (not a level). tone()/noTone() ride the ESP32 LEDC.
    // (If a build lacks tone(): ledcAttach(BUZZER_PIN, BUZZ_FREQ, 8) once, then
    //  ledcWriteTone(BUZZER_PIN, BUZZ_FREQ) to start / ledcWriteTone(BUZZER_PIN, 0) to stop.)
    if (reqBuzzMs) {
        tone(BUZZER_PIN, BUZZ_FREQ);
        buzzUntil = now + reqBuzzMs;
        reqBuzzMs = 0;
    }
    if (buzzUntil && now >= buzzUntil) {
        noTone(BUZZER_PIN);
        digitalWrite(BUZZER_PIN, LOW);
        buzzUntil = 0;
    }

    // Vibration motor — plain on/off.
    if (reqVibrateMs) {
        digitalWrite(VIBRATE_PIN, HIGH);
        vibrateUntil = now + reqVibrateMs;
        reqVibrateMs = 0;
    }
    if (vibrateUntil && now >= vibrateUntil) {
        digitalWrite(VIBRATE_PIN, LOW);
        vibrateUntil = 0;
    }

    serviceWipeButton(now);

    // Emit one queued reply per REPLY_SPACING_MS so /help doesn't hog the airtime.
    if (replyHead != replyTail && now >= nextReplyAt) {
        sendText(replyQ[replyHead]);
        replyHead = (uint8_t)((replyHead + 1) % kReplyQ);
        nextReplyAt = now + REPLY_SPACING_MS;
    }

    // Fire a confirmed wipe only after its "WIPING" reply has actually left the queue — otherwise
    // factoryReset() reboots us before anyone hears the confirmation.
    if (wipePending && replyHead == replyTail && now >= nextReplyAt) {
        wipePending = false;
        doFactoryWipe();
    }

    return CMD_POLL_MS;
}
