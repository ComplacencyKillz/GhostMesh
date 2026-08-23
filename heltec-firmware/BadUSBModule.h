#pragma once
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

// BadUSBModule — native USB HID keyboard on the Heltec V3, triggered via mesh or IR.
//
// The ESP32-S3's built-in USB OTG (GPIO19/20) is independent of the CP2102 serial bridge — both
// share the USB-C connector. This module turns the backpack into a USB HID keyboard that types out
// DuckyScript payloads pre-staged in /ghostmesh/ on LittleFS. No Flipper required.
//
// Triggers (all armed-gated):
//   /key @id <name>     — mesh command (CommandModule dispatches to us)
//   GM_IR_RUN (0x05)    — IR NECext, fires whatever /key last staged (IRModule calls trigger())
//
// DuckyScript parser: state machine processes one .txt file line-by-line from LittleFS.
// Supported commands: REM, DELAY, STRING, ENTER, TAB, ESC, SPACE, GUI, CTRL, ALT, SHIFT,
// single printable chars, and empty lines. Unrecognized commands are silently skipped.
//
// The USB HID keyboard uses the Arduino-ESP32 framework's USBHIDKeyboard class (TinyUSB).
// Requires build flags: -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=0.
class BadUSBModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    BadUSBModule();

    // Called from CommandModule::handleCommandText (mesh) or IRModule (IR). Name is the basename
    // of a file in /ghostmesh/. Returns true if the payload was loaded and execution started.
    bool trigger(const char *name);

  protected:
    virtual int32_t runOnce() override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return false; }

  private:
    bool firstTime = true;

    // ── parser state ──
    enum { S_IDLE, S_STRING, S_DELAY } parseState = S_IDLE;
    bool     running    = false;   // a payload is currently being typed out
    uint32_t lineStart  = 0;       // millis when the current line began executing
    uint32_t delayUntil = 0;       // DELAY <ms> blocks until this time
    File     scriptFile;           // open .txt payload
    char     curName[40];          // payload name currently running (for /status)
    char     lineBuf[256];         // the line currently being typed (STRING builds it up)

    // ── internals ──
    void beginPayload(const char *name);
    void abortPayload();           // stop + close file, reset parser
    void stepParser();             // process one DuckyScript command from the file
    void typeString(const char *s);  // USB HID Keyboard.print, with delay pacing
    void pressRelease(uint8_t key);
    void pressReleaseMod(uint8_t mod, uint8_t key);
    uint8_t duckyKeyCode(const char *name); // map DuckyScript key name → USB HID code
};

extern BadUSBModule *badUSBModule;