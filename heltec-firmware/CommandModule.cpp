#include "CommandModule.h"
#include "GhostMeshArming.h"
#include "GhostMeshConfig.h"
#include "GhostMeshWipe.h"
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
#define LED_PIN         35   // onboard white LED — a simple on/off mirror of the RGB state
#define RGB_LED_PIN     26   // external SK6812 (WS2812 family) data — driven via neopixelWrite()
#define RGB_BRIGHT      64   // 0-255 cap; a status LED at ~1/4 is plenty and easy on the 3.3V rail

// ── Indicator effect engine ──────────────────────────────────────────────────────────
// One EffectSeg = a colour ramp (r0,g0,b0)→(r1,g1,b1) over dur_ms, with an optional buzzer tone
// and vibration for the whole segment. A solid flash sets start==end. A list of segments is a
// little synced light/sound sequence. runOnce interpolates the colour each tick and applies the
// tone/vibration once per segment. This is what turns the outputs from CLI test toys into real
// deployment feedback.
struct EffectSeg {
    uint16_t dur_ms;
    uint8_t  r0, g0, b0, r1, g1, b1;
    uint16_t buzz_hz; // 0 = silent
    bool     vib;
};

enum { FX_NONE = 0, FX_ARMED, FX_DISARMED, FX_WIPE, FX_MSG, FX_CLI, FX_GRADIENT };

#define BR RGB_BRIGHT
// Armed: green→yellow→red, rising two-note + a vibration kick. "Coming online."
static const EffectSeg FX_ARMED_SEGS[] = {
    {500,  0, BR, 0, BR, BR, 0, 1500, true},
    {500, BR, BR, 0, BR,  0, 0, 2200, false},
};
// Disarmed: red→yellow→green, falling two-note. "Standing down."
static const EffectSeg FX_DISARMED_SEGS[] = {
    {500, BR,  0, 0, BR, BR, 0, 2200, true},
    {500, BR, BR, 0,  0, BR, 0, 1500, false},
};
// Wipe: three blue flashes + a low tone, then fade to off. Plays *before* the erase.
static const EffectSeg FX_WIPE_SEGS[] = {
    {120, 0, 0, BR, 0, 0, BR, 400, true},  {120, 0, 0, 0, 0, 0, 0, 0, false},
    {120, 0, 0, BR, 0, 0, BR, 400, true},  {120, 0, 0, 0, 0, 0, 0, 0, false},
    {120, 0, 0, BR, 0, 0, BR, 400, true},  {120, 0, 0, 0, 0, 0, 0, 0, false},
    {480, 0, 0, BR, 0, 0, 0, 300, true},
};
// Message received: three yellow flashes + a double-buzz.
static const EffectSeg FX_MSG_SEGS[] = {
    {120, BR, BR, 0, BR, BR, 0, 2000, true},  {120, 0, 0, 0, 0, 0, 0, 0, false},
    {120, BR, BR, 0, BR, BR, 0, 2000, false}, {120, 0, 0, 0, 0, 0, 0, 0, false},
    {120, BR, BR, 0, BR, BR, 0, 2000, false}, {120, 0, 0, 0, 0, 0, 0, 0, false},
};
// CLI command received: two green flashes + a short high buzz.
static const EffectSeg FX_CLI_SEGS[] = {
    {120, 0, BR, 0, 0, BR, 0, 2500, true}, {120, 0, 0, 0, 0, 0, 0, 0, false},
    {120, 0, BR, 0, 0, BR, 0, 2500, false}, {120, 0, 0, 0, 0, 0, 0, 0, false},
};
// Gradient: continuous green↔red, no sound. A CLI test / "loud idle."
static const EffectSeg FX_GRADIENT_SEGS[] = {
    {2000, 0, BR, 0, BR, 0, 0, 0, false},
    {2000, BR, 0, 0, 0, BR, 0, 0, false},
};
#undef BR

// Resolve an effect id → its segment list. loop=true replays instead of stopping.
static bool fx_lookup(uint8_t fx, const EffectSeg **segs, uint8_t *count, bool *loop) {
    *loop = false;
    switch (fx) {
    case FX_ARMED:    *segs = FX_ARMED_SEGS;    *count = 2; return true;
    case FX_DISARMED: *segs = FX_DISARMED_SEGS; *count = 2; return true;
    case FX_WIPE:     *segs = FX_WIPE_SEGS;     *count = 7; return true;
    case FX_MSG:      *segs = FX_MSG_SEGS;      *count = 6; return true;
    case FX_CLI:      *segs = FX_CLI_SEGS;      *count = 4; return true;
    case FX_GRADIENT: *segs = FX_GRADIENT_SEGS; *count = 2; *loop = true; return true;
    default: return false;
    }
}

// Confirmed wipe (any path) sets this; CommandModule::runOnce plays FX_WIPE then erases.
volatile bool ghostmesh_wipe_request = false;
#define WIPE_PREROLL_MS 1300 // ~FX_WIPE length; the erase is scheduled this far out, guaranteed
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
    const meshtastic_Data &d = mp.decoded;
    if (d.payload.size == 0)
        return ProcessMessage::CONTINUE;

    // Copy to a mutable, NUL-terminated buffer for the tokenizer.
    char text[232];
    size_t n = d.payload.size < sizeof(text) - 1 ? d.payload.size : sizeof(text) - 1;
    memcpy(text, d.payload.bytes, n);
    text[n] = '\0';

    // We no longer blanket-ignore our own traffic. handleCommandText only acts on a '/'-command
    // that names THIS node (targetsMe), so our own broadcasts (replies, ARMED, non-command chat)
    // fall through harmlessly — but a self-directed command from the attached FAP (a local /set or
    // /cfg addressed to this node) now gets processed. That's what enables local config.
    bool fromUs = isFromUs(&mp);

    // Payload transfer (/put ...) is high-rate: intercept it BEFORE the reception effect and the
    // command tokenizer, so a stream of hundreds of data chunks neither strobes the LED nor runs
    // the generic dispatch. handlePut targets-checks internally and stays silent on data chunks.
    {
        const char *p = text;
        while (*p == ' ')
            p++;
        if (strncasecmp(p, "/put ", 5) == 0) {
            handlePut(text);
            return ProcessMessage::CONTINUE;
        }
    }

    // Reception feedback: only for genuinely incoming traffic, not our own self-sends. A '/'-command
    // flashes the CLI effect; other text the message effect. An arm/disarm overrides via arm-edge.
    if (!fromUs) {
        const char *p = text;
        while (*p == ' ')
            p++;
        startEffect(*p == '/' ? FX_CLI : FX_MSG);
    }

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
    char *cmd = strtok_r(s, " ", &save);       // "/help"
    char *tgt = strtok_r(nullptr, " ", &save); // "@f69c"
    char *arg = strtok_r(nullptr, " ", &save); // optional: ms, token, color, /set key…
    char *arg2 = strtok_r(nullptr, " ", &save); // optional 2nd arg: /set <key> <val>
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
    } else if (strcasecmp(cmd, "/fx") == 0) {
        doFx(arg);
    } else if (strcasecmp(cmd, "/set") == 0) {
        doSet(arg, arg2);
    } else if (strcasecmp(cmd, "/cfg") == 0) {
        doCfg();
    } else if (strcasecmp(cmd, "/wipe") == 0) {
        doWipeCommand(arg);
    } else {
        enqueueReply("? unknown cmd — try /help");
    }
}

// ── Targeting: does @target name THIS node? ─────────────────────────────────────────
// No broadcast target. Every command must name this node's last-4 hex id — there is deliberately
// no @ALL. That removes the one-message fleet-wide amplifier: even a key-holding attacker on the
// private channel must know and name each node individually and can never hit the whole mesh at
// once. The channel PSK is the outer gate; this is the next layer in.
bool CommandModule::targetsMe(const char *tgt)
{
    if (!tgt)
        return false;
    const char *t = (tgt[0] == '@') ? tgt + 1 : tgt;
    char me[8];
    snprintf(me, sizeof(me), "%04x", (unsigned)(nodeDB->getNodeNum() & 0xFFFF));
    return strcasecmp(t, me) == 0;
}

// ── /help: one message PER command (mesh text caps at ~200 chars, so we never cram) ──
void CommandModule::doHelp()
{
    static const char *lines[] = {
        "/help @id - this list",
        "/status @id - armed, battery, uptime",
        "/arm @id - arm the node",
        "/disarm @id - disarm the node",
        "/led @id <red|green|blue|gradient|off>",
        "/buzz @id [ms] - sound buzzer",
        "/vibrate @id [ms] - run vibration",
        "/set @id <prox|light|led|buzz|vib|notify> <val>",
        "/cfg @id - report current config",
        "/wipe @id - complete erase (armed+confirm)",
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

// ── /set <key> <val>: tune a config value live and persist it to NVS ─────────────────
// Keys: prox <cm>, light <counts>, led|buzz|vib <on|off>, notify <on|off> (all three at once).
static bool parse_onoff(const char *v, bool *out) {
    if (!v) return false;
    if (strcasecmp(v, "on") == 0 || strcmp(v, "1") == 0) { *out = true; return true; }
    if (strcasecmp(v, "off") == 0 || strcmp(v, "0") == 0) { *out = false; return true; }
    return false;
}

void CommandModule::doSet(const char *key, const char *val)
{
    if (!key || !val) {
        enqueueReply("SET needs <key> <val>");
        return;
    }
    char reply[48];
    bool onoff;
    if (strcasecmp(key, "prox") == 0) {
        ghostmesh_config.proxThresholdCm = (uint16_t)atoi(val);
        snprintf(reply, sizeof(reply), "prox=%u", ghostmesh_config.proxThresholdCm);
    } else if (strcasecmp(key, "light") == 0) {
        ghostmesh_config.lightThreshold = (uint16_t)atoi(val);
        snprintf(reply, sizeof(reply), "light=%u", ghostmesh_config.lightThreshold);
    } else if (strcasecmp(key, "led") == 0 && parse_onoff(val, &onoff)) {
        ghostmesh_config.notifyLed = onoff;
        if (!onoff && curFx == FX_NONE) setSteadyLed(steadyR, steadyG, steadyB); // repaint (off)
        snprintf(reply, sizeof(reply), "led=%d", onoff);
    } else if (strcasecmp(key, "buzz") == 0 && parse_onoff(val, &onoff)) {
        ghostmesh_config.notifyBuzz = onoff;
        snprintf(reply, sizeof(reply), "buzz=%d", onoff);
    } else if (strcasecmp(key, "vib") == 0 && parse_onoff(val, &onoff)) {
        ghostmesh_config.notifyVib = onoff;
        snprintf(reply, sizeof(reply), "vib=%d", onoff);
    } else if (strcasecmp(key, "notify") == 0 && parse_onoff(val, &onoff)) {
        ghostmesh_config.notifyLed = ghostmesh_config.notifyBuzz = ghostmesh_config.notifyVib = onoff;
        if (!onoff && curFx == FX_NONE) setSteadyLed(steadyR, steadyG, steadyB);
        snprintf(reply, sizeof(reply), "notify=%d (led/buzz/vib)", onoff);
    } else {
        enqueueReply("SET: bad key/val");
        return;
    }
    ghostmesh_config_save();
    enqueueReply(reply);
}

// ── /cfg: reply the current config in one compact message ────────────────────────────
void CommandModule::doCfg()
{
    char reply[64];
    snprintf(reply, sizeof(reply), "CFG prox=%u light=%u led=%d buzz=%d vib=%d",
             ghostmesh_config.proxThresholdCm, ghostmesh_config.lightThreshold,
             ghostmesh_config.notifyLed, ghostmesh_config.notifyBuzz, ghostmesh_config.notifyVib);
    enqueueReply(reply);
}

// ── /led <color|gradient|off>: set the idle colour, or run the gradient effect ───────
// A solid colour becomes the *steady* idle state the LED returns to after any event effect.
// "gradient" runs the looping green↔red effect. Colours are scaled to RGB_BRIGHT.
void CommandModule::doLed(const char *arg)
{
    const char *name = arg ? arg : "white";

    if (strcasecmp(name, "gradient") == 0 || strcasecmp(name, "sweep") == 0) {
        startEffect(FX_GRADIENT);
        enqueueReply("LED gradient");
        return;
    }

    uint8_t r = 0, g = 0, b = 0;
    if (strcasecmp(name, "off") == 0 || strcmp(name, "0") == 0) {
        r = g = b = 0;
    } else if (strcasecmp(name, "red") == 0) {
        r = RGB_BRIGHT;
    } else if (strcasecmp(name, "green") == 0) {
        g = RGB_BRIGHT;
    } else if (strcasecmp(name, "blue") == 0) {
        b = RGB_BRIGHT;
    } else if (strcasecmp(name, "yellow") == 0) {
        r = g = RGB_BRIGHT;
    } else if (strcasecmp(name, "cyan") == 0) {
        g = b = RGB_BRIGHT;
    } else if (strcasecmp(name, "magenta") == 0 || strcasecmp(name, "purple") == 0) {
        r = b = RGB_BRIGHT;
    } else { // "white", "on", or anything unrecognized
        name = "white";
        r = g = b = RGB_BRIGHT;
    }

    stopEffect(); // a solid colour cancels any running effect
    setSteadyLed(r, g, b);
    char reply[40];
    snprintf(reply, sizeof(reply), "LED %s", name);
    enqueueReply(reply);
}

// ── /fx <name>: play an indicator effect for tuning (VISUAL only — never triggers a wipe) ──
void CommandModule::doFx(const char *arg)
{
    uint8_t fx = FX_NONE;
    if (arg) {
        if (strcasecmp(arg, "armed") == 0) fx = FX_ARMED;
        else if (strcasecmp(arg, "disarmed") == 0) fx = FX_DISARMED;
        else if (strcasecmp(arg, "wipe") == 0) fx = FX_WIPE;
        else if (strcasecmp(arg, "msg") == 0) fx = FX_MSG;
        else if (strcasecmp(arg, "cli") == 0) fx = FX_CLI;
        else if (strcasecmp(arg, "gradient") == 0) fx = FX_GRADIENT;
    }
    startEffect(fx); // fx==FX_NONE just stops
    char reply[32];
    snprintf(reply, sizeof(reply), "FX %s", arg ? arg : "off");
    enqueueReply(reply);
}

// ── Indicator engine ──────────────────────────────────────────────────────────────────
void CommandModule::setSteadyLed(uint8_t r, uint8_t g, uint8_t b)
{
    steadyR = r;
    steadyG = g;
    steadyB = b;
    if (curFx == FX_NONE) { // only paint now if no effect owns the LED
        neopixelWrite(RGB_LED_PIN, ghostmesh_config.notifyLed ? r : 0, ghostmesh_config.notifyLed ? g : 0, ghostmesh_config.notifyLed ? b : 0);
        digitalWrite(LED_PIN, (r || g || b) ? HIGH : LOW);
    }
}

void CommandModule::startEffect(uint8_t fx)
{
    // Cancel the plain output timers so nothing fights the effect.
    reqBuzzMs = reqVibrateMs = 0;
    buzzUntil = vibrateUntil = 0;
    if (fx == FX_NONE) { stopEffect(); return; }
    curFx = fx;
    fxIdx = 0;
    fxSegStart = millis();
    fxSegEntered = false;
}

void CommandModule::stopEffect()
{
    curFx = FX_NONE;
    noTone(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(VIBRATE_PIN, LOW);
    // Restore the idle LED.
    neopixelWrite(RGB_LED_PIN, ghostmesh_config.notifyLed ? steadyR : 0, ghostmesh_config.notifyLed ? steadyG : 0, ghostmesh_config.notifyLed ? steadyB : 0);
    digitalWrite(LED_PIN, (steadyR || steadyG || steadyB) ? HIGH : LOW);
}

void CommandModule::tickEffect(uint32_t now)
{
    const EffectSeg *segs;
    uint8_t count;
    bool loop;
    if (!fx_lookup(curFx, &segs, &count, &loop)) { stopEffect(); return; }
    const EffectSeg &s = segs[fxIdx];

    // Apply the tone + vibration once when a segment begins.
    if (!fxSegEntered) {
        fxSegEntered = true;
        if (ghostmesh_config.notifyBuzz && s.buzz_hz) {
            tone(BUZZER_PIN, s.buzz_hz);
        } else {
            noTone(BUZZER_PIN);
            digitalWrite(BUZZER_PIN, LOW);
        }
        digitalWrite(VIBRATE_PIN, (ghostmesh_config.notifyVib && s.vib) ? HIGH : LOW);
    }

    // Interpolate the colour across the segment each tick.
    uint32_t el = now - fxSegStart;
    if (el > s.dur_ms) el = s.dur_ms;
    uint8_t r = (uint8_t)((int)s.r0 + ((int)s.r1 - (int)s.r0) * (int)el / s.dur_ms);
    uint8_t g = (uint8_t)((int)s.g0 + ((int)s.g1 - (int)s.g0) * (int)el / s.dur_ms);
    uint8_t b = (uint8_t)((int)s.b0 + ((int)s.b1 - (int)s.b0) * (int)el / s.dur_ms);
    neopixelWrite(RGB_LED_PIN, ghostmesh_config.notifyLed ? r : 0, ghostmesh_config.notifyLed ? g : 0, ghostmesh_config.notifyLed ? b : 0);
    digitalWrite(LED_PIN, (r || g || b) ? HIGH : LOW);

    // Advance at the end of the segment.
    if (now - fxSegStart >= s.dur_ms) {
        fxIdx++;
        fxSegStart = now;
        fxSegEntered = false;
        if (fxIdx >= count) {
            if (loop) fxIdx = 0;
            else stopEffect();
        }
    }
}

// ── /wipe: two-step mesh path — issue a one-time token, then verify it. Armed-gated. ─
// Reached only when @target already named this node exactly (no @ALL exists), so a single message
// can never wipe the fleet. Still armed-gated + one-time token on top of that.
void CommandModule::doWipeCommand(const char *arg)
{
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

// ── The nuke: request the wipe. runOnce plays FX_WIPE then runs the complete flash erase. ─
void CommandModule::doFactoryWipe()
{
    LOG_WARN("Command: WIPE confirmed -> pre-roll effect, then complete flash erase");
    ghostmesh_wipe_request = true; // serviced in runOnce (plays the effect, then erases)
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
    sendTextTo(msg, NODENUM_BROADCAST); // replies broadcast so every operator sees them
}

// Like sendText, but addressed to a specific node.
void CommandModule::sendTextTo(const char *msg, uint32_t to)
{
    meshtastic_MeshPacket *p = allocDataPacket(); // portnum = TEXT_MESSAGE_APP
    p->to = to;
    p->want_ack = false;
    size_t n = strlen(msg);
    if (n > sizeof(p->decoded.payload.bytes))
        n = sizeof(p->decoded.payload.bytes);
    p->decoded.payload.size = n;
    memcpy(p->decoded.payload.bytes, msg, n);
    service->sendToMesh(p, RX_SRC_LOCAL, true); // ccToPhone=true → also reaches the FAP/app
}

// Deliver a reply ONLY to the connected StreamAPI client (USB/BLE — the web GUI, FAP, or app),
// via the FromRadio queue, with NO LoRa transmit. "Phone" is Meshtastic's name for that client; it
// is whatever is reading the serial stream, not a literal phone. Used for the high-rate /put acks so
// a USB file transfer never spends LoRa airtime (a mesh-transmitted ack costs ~1s each and throttled
// the whole transfer to a crawl).
void CommandModule::sendTextToPhone(const char *msg)
{
    meshtastic_MeshPacket *p = allocDataPacket(); // portnum = TEXT_MESSAGE_APP
    p->to = nodeDB->getNodeNum();
    p->want_ack = false;
    size_t n = strlen(msg);
    if (n > sizeof(p->decoded.payload.bytes))
        n = sizeof(p->decoded.payload.bytes);
    p->decoded.payload.size = n;
    memcpy(p->decoded.payload.bytes, msg, n);
    service->sendToPhone(p); // straight to the FromRadio queue — no mesh transmit, no airtime
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
        neopixelWrite(RGB_LED_PIN, 0, 0, 0); // SK6812 dark at boot (no random-color power-on)
        pinMode(WIPE_BTN_PIN, INPUT_PULLUP);
        ghostmesh_config_ensure_loaded();
        lastArmedSeen = ghostmesh_armed; // seed the edge detector — don't fire on boot
        LOG_INFO("Command: init (buzz %d, vibrate %d, led %d/rgb %d, wipe-btn %d)", BUZZER_PIN, VIBRATE_PIN,
                 LED_PIN, RGB_LED_PIN, WIPE_BTN_PIN);
        return CMD_POLL_MS;
    }

    // Arm-state edge → indicator effect. Catches every source (switch, IR, mesh) in one place.
    if (ghostmesh_armed != lastArmedSeen) {
        lastArmedSeen = ghostmesh_armed;
        startEffect(ghostmesh_armed ? FX_ARMED : FX_DISARMED);
    }

    // Wipe pre-roll: a confirmed wipe (any path) requests the effect; play it, then erase on a
    // guaranteed deadline so the light/sound show happens before the chip goes dark.
    if (ghostmesh_wipe_request && !eraseArmed) {
        ghostmesh_wipe_request = false;
        startEffect(FX_WIPE);
        eraseArmed = true;
        eraseAt = now + WIPE_PREROLL_MS;
    }
    if (eraseArmed && now >= eraseAt) {
        ghostmesh_complete_wipe(); // does not return
    }

    // Outputs: an active effect owns all three; otherwise the plain /buzz//vibrate timers run.
    if (curFx != FX_NONE) {
        tickEffect(now);
    } else {
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
        if (reqVibrateMs) {
            digitalWrite(VIBRATE_PIN, HIGH);
            vibrateUntil = now + reqVibrateMs;
            reqVibrateMs = 0;
        }
        if (vibrateUntil && now >= vibrateUntil) {
            digitalWrite(VIBRATE_PIN, LOW);
            vibrateUntil = 0;
        }
    }

    serviceWipeButton(now);
    servicePutAck();        // emit a pending /put chunk ack immediately (flow control, off the reply queue)
    servicePutTimeout(now); // abort a file transfer that has gone quiet mid-stream

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
