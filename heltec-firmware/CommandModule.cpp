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
#include "gps/GPS.h"          // gps global + GPS::setTimepulseEnabled (silent-mode GPS LED / native cfg)
#include "graphics/Screen.h"  // screen->setOn() for the OLED-off silent-mode toggle
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
    // Route every reply this command produces back to whoever asked — NOT the whole mesh. from ==
    // our own node num means the command came from the local USB/serial StreamAPI client (the web
    // configurator or a USB FAP, self-addressed), so its replies go phone-only with zero LoRa airtime;
    // a remote node gets a directed unicast. This is what stops "connecting spams a CFG line to the
    // whole channel." Autonomous alerts (tamper/arm/etc.) use the broadcast* helpers, not this path.
    curReplyTo = from;

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
        if (ghostmesh_config.repArm) enqueueReply("ARMED");
    } else if (strcasecmp(cmd, "/disarm") == 0) {
        ghostmesh_armed = false;
        if (ghostmesh_config.repArm) enqueueReply("DISARMED");
    } else if (strcasecmp(cmd, "/buzz") == 0) {
        uint32_t ms = arg ? (uint32_t)atoi(arg) : 300;
        if (ms > 5000)
            ms = 5000; // cap so a typo can't wail forever
        reqBuzzMs = ms ? ms : 300;
        char r[32];
        snprintf(r, sizeof(r), "BUZZ %ums", (unsigned)reqBuzzMs);
        if (ghostmesh_config.repBuzz) enqueueReply(r);
    } else if (strcasecmp(cmd, "/vibrate") == 0) {
        uint32_t ms = arg ? (uint32_t)atoi(arg) : 500;
        if (ms > 5000)
            ms = 5000;
        reqVibrateMs = ms ? ms : 500;
        char r[32];
        snprintf(r, sizeof(r), "VIBRATE %ums", (unsigned)reqVibrateMs);
        if (ghostmesh_config.repVib) enqueueReply(r);
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
        if (ghostmesh_config.repUnknown) enqueueReply("? unknown cmd — try /help");
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
    if (!ghostmesh_config.repHelp) return; // /help reply gated off (rep bit 8)
    static const char *lines[] = {
        "/help @id - this list",
        "/status @id - armed, battery, uptime",
        "/arm @id - arm the node",
        "/disarm @id - disarm the node",
        "/led @id <red|green|blue|gradient|off>",
        "/buzz @id [ms] - sound buzzer",
        "/vibrate @id [ms] - run vibration",
        "/set @id <key> <val> - prox light led buzz vib screen hbled gpsled",
        "/set @id <key> - rep_* bc_* in_* silent sensors gps gpsint telint",
        "/set @id mode <active|deployed|dormant> - power/deploy stance",
        "/cfg @id - report current config (bitmask)",
        "/wipe @id - complete erase (armed+confirm)",
    };
    for (const char *l : lines)
        enqueueReply(l);
}

// ── /status: current node state ─────────────────────────────────────────────────────
void CommandModule::doStatus()
{
    if (!ghostmesh_config.repStatus) return; // /status reply gated off (rep bit 9)
    char me[8];
    snprintf(me, sizeof(me), "%04x", (unsigned)(nodeDB->getNodeNum() & 0xFFFF));
    unsigned bat = powerStatus ? powerStatus->getBatteryChargePercent() : 0;
    char r[64];
    snprintf(r, sizeof(r), "STATUS %s: %s bat %u%% up %us", me, ghostmesh_armed ? "ARMED" : "DISARMED",
             bat, (unsigned)(millis() / 1000));
    enqueueReply(r);
}

// ── /set <key> <val>: tune a config value live and persist it to NVS ─────────────────
// Keys: numerics prox <cm>, light <counts>, gpsint/telint <s>; outputs led|buzz|vib|screen|hbled|
// gpsled|gps <on|off>; masters notify|silent|sensors <on|off>; flags rep_*/bc_*/in_* <on|off>.
static bool parse_onoff(const char *v, bool *out) {
    if (!v) return false;
    if (strcasecmp(v, "on") == 0 || strcmp(v, "1") == 0) { *out = true; return true; }
    if (strcasecmp(v, "off") == 0 || strcmp(v, "0") == 0) { *out = false; return true; }
    return false;
}

void CommandModule::doSet(const char *key, const char *val)
{
    if (!key || !val) {
        if (ghostmesh_config.repErr) enqueueReply("SET needs <key> <val>");
        return;
    }
    GhostMeshConfig &c = ghostmesh_config;
    char reply[48];
    bool onoff;

    // ── Plain on/off flags: stored only; the owning module reads them next poll. No side effect. ──
    static const struct { const char *k; bool *f; } kFlags[] = {
        {"rep_arm", &ghostmesh_config.repArm},   {"rep_buzz", &ghostmesh_config.repBuzz},
        {"rep_vib", &ghostmesh_config.repVib},   {"rep_led", &ghostmesh_config.repLed},
        {"rep_wipe", &ghostmesh_config.repWipe}, {"bc_tilt", &ghostmesh_config.bcTilt},
        {"bc_light", &ghostmesh_config.bcLight}, {"bc_prox", &ghostmesh_config.bcProx},
        {"rep_help", &ghostmesh_config.repHelp}, {"rep_status", &ghostmesh_config.repStatus},
        {"rep_err", &ghostmesh_config.repErr},   {"rep_unknown", &ghostmesh_config.repUnknown},
        {"in_tilt", &ghostmesh_config.inTilt},   {"in_light", &ghostmesh_config.inLight},
        {"in_prox", &ghostmesh_config.inProx},   {"in_ir", &ghostmesh_config.inIr},
    };
    for (auto &kf : kFlags) {
        if (strcasecmp(key, kf.k) == 0) {
            if (!parse_onoff(val, &onoff)) { if (ghostmesh_config.repErr) enqueueReply("SET: bad key/val"); return; }
            *kf.f = onoff;
            snprintf(reply, sizeof(reply), "%s=%d", kf.k, onoff);
            ghostmesh_config_save();
            enqueueReply(reply);
            return;
        }
    }

    // ── Numerics + side-effect keys + masters ──
    if (strcasecmp(key, "prox") == 0) {
        c.proxThresholdCm = (uint16_t)atoi(val);
        snprintf(reply, sizeof(reply), "prox=%u", c.proxThresholdCm);
    } else if (strcasecmp(key, "light") == 0) {
        c.lightThreshold = (uint16_t)atoi(val);
        snprintf(reply, sizeof(reply), "light=%u", c.lightThreshold);
    } else if (strcasecmp(key, "gpsint") == 0) {
        c.gpsUpdateSecs = (uint16_t)atoi(val);
        ghostmesh_apply_native_config();
        snprintf(reply, sizeof(reply), "gpsint=%u", c.gpsUpdateSecs);
    } else if (strcasecmp(key, "telint") == 0) {
        c.telUpdateSecs = (uint16_t)atoi(val);
        ghostmesh_apply_native_config();
        snprintf(reply, sizeof(reply), "telint=%u", c.telUpdateSecs);
    } else if (strcasecmp(key, "led") == 0 && parse_onoff(val, &onoff)) {
        c.notifyLed = onoff;
        if (curFx == FX_NONE) setSteadyLed(steadyR, steadyG, steadyB); // repaint
        snprintf(reply, sizeof(reply), "led=%d", onoff);
    } else if (strcasecmp(key, "buzz") == 0 && parse_onoff(val, &onoff)) {
        c.notifyBuzz = onoff;
        snprintf(reply, sizeof(reply), "buzz=%d", onoff);
    } else if (strcasecmp(key, "vib") == 0 && parse_onoff(val, &onoff)) {
        c.notifyVib = onoff;
        snprintf(reply, sizeof(reply), "vib=%d", onoff);
    } else if (strcasecmp(key, "screen") == 0 && parse_onoff(val, &onoff)) {
        c.outScreen = onoff; applyOutputState();
        snprintf(reply, sizeof(reply), "screen=%d", onoff);
    } else if (strcasecmp(key, "hbled") == 0 && parse_onoff(val, &onoff)) {
        c.outHbled = onoff; applyOutputState();
        snprintf(reply, sizeof(reply), "hbled=%d", onoff);
    } else if (strcasecmp(key, "gpsled") == 0 && parse_onoff(val, &onoff)) {
        c.outGpsled = onoff; applyOutputState();
        snprintf(reply, sizeof(reply), "gpsled=%d", onoff);
    } else if (strcasecmp(key, "gps") == 0 && parse_onoff(val, &onoff)) {
        c.gpsOn = onoff; ghostmesh_apply_native_config();
        snprintf(reply, sizeof(reply), "gps=%d", onoff);
    } else if (strcasecmp(key, "notify") == 0 && parse_onoff(val, &onoff)) {
        c.notifyLed = c.notifyBuzz = c.notifyVib = onoff;
        if (curFx == FX_NONE) setSteadyLed(steadyR, steadyG, steadyB);
        snprintf(reply, sizeof(reply), "notify=%d", onoff);
    } else if (strcasecmp(key, "silent") == 0 && parse_onoff(val, &onoff)) {
        bool en = !onoff; // silent on ⇒ every physical output OFF
        c.notifyLed = c.notifyBuzz = c.notifyVib = en;
        c.outScreen = c.outHbled = c.outGpsled = en;
        applyOutputState();
        snprintf(reply, sizeof(reply), "silent=%d", onoff);
    } else if (strcasecmp(key, "sensors") == 0 && parse_onoff(val, &onoff)) {
        c.inTilt = c.inLight = c.inProx = c.inIr = onoff;
        snprintf(reply, sizeof(reply), "sensors=%d", onoff);
    } else if (strcasecmp(key, "mode") == 0) {
        // HIBERNATE power/deployment stance. One command applies the whole composite so a preset
        // never fires a burst of self-addressed /set packets (which the router would drop past the
        // first). This is the power/sensing axis only — physical outputs are the separate BLACKOUT
        // (silent) axis. Tamper INPUTS stay live for 'deployed' (a watching dead-drop); only
        // 'dormant' stands them down.
        if (strcasecmp(val, "active") == 0) {          // full field use
            c.gpsOn = true;  c.telUpdateSecs = 120;
            c.inTilt = c.inLight = c.inProx = c.inIr = true;
        } else if (strcasecmp(val, "deployed") == 0) { // long-haul: hogs off, tamper watching
            c.gpsOn = false; c.telUpdateSecs = 900;
            c.inTilt = c.inLight = c.inProx = c.inIr = true;
        } else if (strcasecmp(val, "dormant") == 0) {  // transport/storage: minimal, not watching
            c.gpsOn = false; c.telUpdateSecs = 3600;
            c.inTilt = c.inLight = c.inProx = c.inIr = false;
        } else {
            if (ghostmesh_config.repErr) enqueueReply("SET: mode = active|deployed|dormant");
            return;
        }
        ghostmesh_apply_native_config();
        snprintf(reply, sizeof(reply), "mode=%s", val);
    } else {
        if (ghostmesh_config.repErr) enqueueReply("SET: bad key/val");
        return;
    }
    ghostmesh_config_save();
    enqueueReply(reply);
}

// ── /cfg: reply the current config as one compact bitmask line ───────────────────────
// rep bits: 0 arm,1 buzz,2 vib,3 led,4 wipe,5 tilt-bc,6 light-bc,7 prox-bc,8 help,9 status,10 err,11 unknown
// out bits: 0 led,1 buzz,2 vib,3 screen,4 hbled,5 gpsled ; in bits: 0 tilt,1 light,2 prox,3 ir
void CommandModule::doCfg()
{
    const GhostMeshConfig &c = ghostmesh_config;
    uint16_t rep = (c.repArm) | (c.repBuzz << 1) | (c.repVib << 2) | (c.repLed << 3) | (c.repWipe << 4) |
                   (c.bcTilt << 5) | (c.bcLight << 6) | (c.bcProx << 7) |
                   (c.repHelp << 8) | (c.repStatus << 9) | (c.repErr << 10) | (c.repUnknown << 11);
    uint8_t out = (c.notifyLed) | (c.notifyBuzz << 1) | (c.notifyVib << 2) | (c.outScreen << 3) |
                  (c.outHbled << 4) | (c.outGpsled << 5);
    uint8_t in = (c.inTilt) | (c.inLight << 1) | (c.inProx << 2) | (c.inIr << 3);
    char reply[96];
    snprintf(reply, sizeof(reply),
             "CFG prox=%u light=%u rep=%x out=%x in=%x gps=%u gpsint=%u telint=%u arm=%u",
             c.proxThresholdCm, c.lightThreshold, rep, out, in, c.gpsOn, c.gpsUpdateSecs, c.telUpdateSecs,
             ghostmesh_armed ? 1u : 0u);
    enqueueReply(reply);
}

// (Re)apply the physical output state from config: OLED on/off, onboard heartbeat LED, RGB (+mirror),
// and the best-effort GPS PPS/fix LED. Called from /set of any output key, silent, and at boot.
void CommandModule::applyOutputState()
{
    if (screen) screen->setOn(ghostmesh_config.outScreen);            // OLED
    config.device.led_heartbeat_disabled = !ghostmesh_config.outHbled; // stop Meshtastic heartbeat toggling
    if (!ghostmesh_config.outHbled) digitalWrite(LED_PIN, LOW);        // force GPIO35 dark now
    if (curFx == FX_NONE) setSteadyLed(steadyR, steadyG, steadyB);     // repaint RGB + mirror per flags
    if (gps) gps->setTimepulseEnabled(ghostmesh_config.outGpsled);     // GPS LED (best-effort UBX)
}

// Apply our GPS/telemetry settings to Meshtastic's own config, live, and persist. Defined here (not
// in GhostMeshConfig.cpp) so it can reach the Meshtastic globals (config/moduleConfig/gps/nodeDB).
void ghostmesh_apply_native_config()
{
    GhostMeshConfig &c = ghostmesh_config;
    config.position.gps_mode =
        c.gpsOn ? meshtastic_Config_PositionConfig_GpsMode_ENABLED : meshtastic_Config_PositionConfig_GpsMode_DISABLED;
    if (gps) { if (c.gpsOn) gps->enable(); else gps->disable(); }
    if (c.gpsUpdateSecs) config.position.gps_update_interval = c.gpsUpdateSecs;   // secs; re-read live
    if (c.telUpdateSecs) moduleConfig.telemetry.environment_update_interval = c.telUpdateSecs;
    if (nodeDB) nodeDB->saveToDisk(SEGMENT_CONFIG | SEGMENT_MODULECONFIG);
}

// ── /led <color|gradient|off>: set the idle colour, or run the gradient effect ───────
// A solid colour becomes the *steady* idle state the LED returns to after any event effect.
// "gradient" runs the looping green↔red effect. Colours are scaled to RGB_BRIGHT.
void CommandModule::doLed(const char *arg)
{
    const char *name = arg ? arg : "white";

    if (strcasecmp(name, "gradient") == 0 || strcasecmp(name, "sweep") == 0) {
        startEffect(FX_GRADIENT);
        if (ghostmesh_config.repLed) enqueueReply("LED gradient");
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
    if (ghostmesh_config.repLed) enqueueReply(reply);
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
    if (ghostmesh_config.repLed) enqueueReply(reply);
}

// ── Indicator engine ──────────────────────────────────────────────────────────────────
void CommandModule::setSteadyLed(uint8_t r, uint8_t g, uint8_t b)
{
    steadyR = r;
    steadyG = g;
    steadyB = b;
    if (curFx == FX_NONE) { // only paint now if no effect owns the LED
        neopixelWrite(RGB_LED_PIN, ghostmesh_config.notifyLed ? r : 0, ghostmesh_config.notifyLed ? g : 0, ghostmesh_config.notifyLed ? b : 0);
        digitalWrite(LED_PIN, (ghostmesh_config.outHbled && (r || g || b)) ? HIGH : LOW);
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
    digitalWrite(LED_PIN, (ghostmesh_config.outHbled && (steadyR || steadyG || steadyB)) ? HIGH : LOW);
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
    digitalWrite(LED_PIN, (ghostmesh_config.outHbled && (r || g || b)) ? HIGH : LOW);

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
    // NOTE on rep_wipe: the guards below silence only the wipe REPLY TEXT. The armed gate, the
    // one-time token mint/verify, and the erase itself are outside the guards — wipe SAFETY is
    // unchanged. (With rep_wipe off the mesh two-step /wipe can't be completed because the token is
    // never shown — that's the operator's choice; the physical double-press and IR paths still work.)
    if (!ghostmesh_armed) {
        if (ghostmesh_config.repWipe) enqueueReply("WIPE denied: not armed");
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
        if (ghostmesh_config.repWipe) enqueueReply(r);
        return;
    }

    // Stage 2: verify the echoed token.
    if (wipeToken == 0 || (millis() - wipeTokenAt) > WIPE_TOKEN_TTL_MS) {
        wipeToken = 0;
        if (ghostmesh_config.repWipe) enqueueReply("WIPE: token expired, retry");
        return;
    }
    uint16_t got = (uint16_t)strtoul(arg, nullptr, 16);
    if (got != wipeToken) {
        if (ghostmesh_config.repWipe) enqueueReply("WIPE: bad token");
        return;
    }
    wipeToken = 0;
    if (ghostmesh_config.repWipe) enqueueReply("WIPING");
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
                curReplyTo = NODENUM_BROADCAST; // physical wipe has no requester — alert the whole mesh
                if (ghostmesh_config.repWipe) enqueueReply("WIPING (button)");
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
    replyToQ[replyTail] = curReplyTo; // remember who this reply is for (drain routes on it)
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
        applyOutputState(); // boot into the configured silent-mode output state (screen/LEDs/GPS-LED)
        // Native GPS/telemetry settings are persisted in Meshtastic's own config, so they restore on
        // boot without us re-applying (avoids a boot-time flash write + a GPS-init race).
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

    // Emit one queued reply per REPLY_SPACING_MS so /help doesn't hog the airtime. Route by the
    // destination captured when the reply was queued: the local StreamAPI client (self-addressed
    // command ⇒ dest == our node num) gets it phone-only with NO LoRa transmit; a remote requester
    // gets a directed unicast; a broadcast dest (unsolicited/physical events) still goes to everyone.
    if (replyHead != replyTail && now >= nextReplyAt) {
        uint32_t dest = replyToQ[replyHead];
        if (dest == nodeDB->getNodeNum())
            sendTextToPhone(replyQ[replyHead]); // local web/FAP client — off-mesh, no airtime
        else if (dest == NODENUM_BROADCAST)
            sendText(replyQ[replyHead]);        // unsolicited/physical event — everyone hears it
        else
            sendTextTo(replyQ[replyHead], dest); // remote requester — directed, not the whole mesh
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
