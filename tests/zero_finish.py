"""Targeted expiry, restart and accessibility checks in an isolated instance."""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import json
import subprocess
import sys
import time
import comtypes
import comtypes.client
from PIL import ImageGrab

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'scratch/zero-finish'
OUT.mkdir(parents=True, exist_ok=True)
exe = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'dist/LightTimer.exe'
u = C.windll.user32
u.SetThreadDpiAwarenessContext(C.c_void_p(-4))
u.FindWindowW.argtypes = [W.LPCWSTR, W.LPCWSTR]
u.FindWindowW.restype = W.HWND
u.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u.SendMessageW.restype = C.c_ssize_t
u.WindowFromPoint.argtypes = [W.POINT]
u.WindowFromPoint.restype = W.HWND
class Copy(C.Structure):
    _fields_ = [('tag', C.c_size_t), ('size', W.DWORD), ('data', C.c_void_p)]

assert not u.FindWindowW('LightTimer.Native.Test', '轻计时')
config = OUT / 'test.ini'
config.write_text('[timer]\nsound=0\nrest=0\nbreath=0\n', encoding='utf-8')
original = W.POINT()
u.GetCursorPos(C.byref(original))
p = subprocess.Popen([str(exe), '--test', '--config', str(config)])
h = 0
try:
    for _ in range(50):
        h = u.FindWindowW('LightTimer.Native.Test', '轻计时')
        if h:
            break
        time.sleep(.05)
    assert h
    def cmd(text):
        b = C.create_unicode_buffer(text)
        cd = Copy(0x4c54494d, C.sizeof(b), C.cast(b, C.c_void_p))
        assert u.SendMessageW(h, 0x4a, 0, C.addressof(cd)) == 1
    def state():
        f = OUT / 'state.json'
        cmd('inspect ' + str(f))
        return json.loads(f.read_text())
    # Start away from the shared pointer so expiry is visible as digits.
    cmd('move 40 40' if original.x > 240 or original.y > 140 else 'move 500 500')
    cmd('start 2')
    time.sleep(2.15)
    s = state()
    assert s['completed'] and not s['running'] and s['remainingMs'] == 0
    mod = comtypes.client.GetModule('oleacc.dll')
    acc = C.POINTER(mod.IAccessible)()
    getacc = C.oledll.oleacc.AccessibleObjectFromWindow
    getacc.argtypes = [W.HWND, W.DWORD, C.POINTER(comtypes.GUID), C.POINTER(C.POINTER(mod.IAccessible))]
    getacc(h, 0xfffffffc, C.byref(mod.IAccessible._iid_), C.byref(acc))
    assert acc.accName(0) == '时间到，00:00'
    time.sleep(.7)
    a = state()
    assert not a['completionEmphasis'] and not a['controls']
    ImageGrab.grab(tuple(a['rect'])).save(OUT / 'finished.png')
    time.sleep(.25)
    b = state()
    assert b['paintCount'] == a['paintCount'] and b['wakeCount'] == a['wakeCount']
    r, d = b['rect'], b['dpi']
    xy = (round((r[0] + r[2]) / 2 + 30 * d), round(r[1] + 18 * d))
    u.SetThreadDpiAwarenessContext(C.c_void_p(-4))
    u.SetCursorPos(*xy)
    time.sleep(.55)
    cursor = W.POINT()
    u.GetCursorPos(C.byref(cursor))
    assert u.WindowFromPoint(W.POINT(*xy)) == h and state()['controls'], (xy, (cursor.x, cursor.y), h, u.WindowFromPoint(W.POINT(*xy)), state())
    names = [acc.accName(i) for i in range(1, acc.accChildCount + 1)]
    restart = names.index('重新开始') + 1
    assert not acc.accState(restart) & 1, 'Restart must be enabled'
    ImageGrab.grab(tuple(state()['rect'])).save(OUT / 'restart.png')
    u.SetThreadDpiAwarenessContext(C.c_void_p(-4))
    # Drive this window's native press/release handlers without clicking another app
    # if the user moves the shared desktop pointer during screenshot capture.
    lp = (round(18 * d) << 16) | round((r[2] - r[0]) / 2 + 30 * d)
    u.SendMessageW(h, 0x201, 1, lp)
    time.sleep(.06)
    pressed = state()
    assert pressed['completed'] and pressed['pointerDown'], pressed
    u.SendMessageW(h, 0x202, 0, lp)
    time.sleep(.06)
    s = state()
    assert s['running'] and not s['completed'] and 1700 < s['remainingMs'] <= 2000, s
    assert s['rect'] == r
    assert '暂停' in [acc.accName(i) for i in range(1, acc.accChildCount + 1)]
    (OUT / 'result.json').write_text(json.dumps({
        'checks': ['expiry at zero', 'accessible time-up label', 'emphasis stops',
                   'idle has no redraw loop', 'restart enabled', 'restart on release',
                   'original duration restored', 'running pause control restored'],
        'final': s}, ensure_ascii=False, indent=2), encoding='utf-8')
    print('PASS: zero display, settled idle, enabled restart, release-only action, original duration')
finally:
    if h:
        cmd('quit')
    p.wait(timeout=5)
    u.SetThreadDpiAwarenessContext(C.c_void_p(-4))
    u.SetCursorPos(original.x, original.y)
