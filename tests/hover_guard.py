"""Focused native mouse checks, in an isolated instance and config."""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import json
import subprocess
import sys
import time
from PIL import ImageGrab
import comtypes
import comtypes.client

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'scratch/hover-guard'
OUT.mkdir(parents=True, exist_ok=True)
exe = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'dist/LightTimer.exe'
u = C.windll.user32
u.SetProcessDpiAwarenessContext(C.c_void_p(-4))
u.FindWindowW.argtypes = [W.LPCWSTR, W.LPCWSTR]
u.FindWindowW.restype = W.HWND
u.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u.SendMessageW.restype = C.c_ssize_t
u.WindowFromPoint.argtypes = [W.POINT]
u.WindowFromPoint.restype = W.HWND
u.SetForegroundWindow.argtypes = [W.HWND]

class Copy(C.Structure):
    _fields_ = [('tag', C.c_size_t), ('size', W.DWORD), ('data', C.c_void_p)]

class MI(C.Structure):
    _fields_ = [('dx', W.LONG), ('dy', W.LONG), ('mouseData', W.DWORD),
                ('dwFlags', W.DWORD), ('time', W.DWORD), ('extra', C.c_size_t)]

class Input(C.Structure):
    _fields_ = [('type', W.DWORD), ('mi', MI)]

u.SendInput.argtypes = [W.UINT, C.POINTER(Input), C.c_int]
assert not u.FindWindowW('LightTimer.Native.Test', '轻计时'), 'Existing test instance'
config = OUT / 'test.ini'
config.write_text('[timer]\nsound=0\nrest=0\nbreath=0\n', encoding='utf-8')
original = W.POINT()
u.GetCursorPos(C.byref(original))
p = subprocess.Popen([str(exe), '--test', '--config', str(config)])
h = 0
checks = []

def cmd(text):
    b = C.create_unicode_buffer(text)
    cd = Copy(0x4c54494d, C.sizeof(b), C.cast(b, C.c_void_p))
    assert u.SendMessageW(h, 0x4a, 0, C.addressof(cd)) == 1

def state():
    f = OUT / 'state.json'
    cmd('inspect ' + str(f))
    return json.loads(f.read_text())

def button(down):
    event = Input(type=0, mi=MI(dwFlags=2 if down else 4))
    assert u.SendInput(1, C.byref(event), C.sizeof(Input)) == 1
    time.sleep(.025)

def move(x, y):
    assert u.SetCursorPos(round(x), round(y))
    time.sleep(.025)

def target(id=2):
    s = state()
    r = s['rect']
    return (r[0] + round((95 if id == 1 else 14 if id == 4 else 53) * s['dpi']),
            r[1] + round(18 * s['dpi']))

def outside():
    r = state()['rect']
    move(r[0] - 50, r[1] - 50)
    time.sleep(.04)

def reveal(id=2):
    outside()
    xy = target(id)
    move(*xy)
    assert u.WindowFromPoint(W.POINT(*xy)) == h
    time.sleep(.48)
    assert state()['controls']
    return xy

def ok(name):
    checks.append(name)
    print('PASS:', name, flush=True)

try:
    for _ in range(60):
        h = u.FindWindowW('LightTimer.Native.Test', '轻计时')
        if h:
            break
        time.sleep(.05)
    assert h
    cmd('move 500 400')
    cmd('start 600')
    outside()
    initial = state()
    move(*target())
    time.sleep(.12)
    assert not state()['controls']
    outside()
    time.sleep(.45)
    assert not state()['controls'] and state()['running']
    ok('quick pass and leave cancel hover')

    # Moving across the window for longer than the dwell must not reveal controls.
    r = state()['rect']
    for x in [r[0]+20, r[2]-20, r[0]+30, r[2]-30]:
        move(x, (r[1]+r[3])//2)
        time.sleep(.13)
        assert not state()['controls']
    time.sleep(.48)
    s = state()
    cursor = W.POINT()
    u.GetCursorPos(C.byref(cursor))
    assert s['controls'] and s['rect'] == initial['rect'], (s, (cursor.x, cursor.y), h, u.WindowFromPoint(cursor))
    ok('dwell requires a steady pointer and does not resize window')

    xy = reveal()
    ImageGrab.grab(tuple(state()['rect'])).save(OUT/'controls.png')
    button(True)
    time.sleep(.1)
    assert state()['running'], 'Mouse down must not pause'
    button(False)
    assert not state()['running']
    xy = reveal()
    button(True)
    button(False)
    assert state()['running']
    ok('pause and resume execute only on release')

    xy = reveal(1)
    button(True)
    assert not state()['settingsVisible']
    button(False)
    assert state()['settingsVisible']
    cmd('close-settings')
    ok('settings opens only on release')

    for id in (1, 2, 4, 0):
        outside()
        xy = reveal(id) if id else target()
        if not id:
            move(*xy)
            assert not state()['controls']
        old = state()['rect']
        button(True)
        move(xy[0]+18, xy[1]+10)
        move(xy[0]+100, xy[1]+45)
        time.sleep(.1)
        mid = state()
        assert not mid['controls'] and mid['running'] and not mid['settingsVisible']
        ImageGrab.grab(tuple(mid['rect'])).save(OUT/f'drag-{id}.png')
        button(False)
        time.sleep(.1)
        s = state()
        assert s['rect'] != old and not s['controls']
        assert s['running'] and not s['settingsVisible']
        time.sleep(.48)
        assert state()['controls'], 'Dwell must restart after drag'
    ok('drag from action buttons, mode selector and digits cancels click and restarts dwell')

    # Press before controls appear cannot become a click merely by waiting.
    outside()
    move(*target())
    button(True)
    time.sleep(.5)
    assert not state()['controls']
    button(False)
    assert state()['running'] and not state()['controls']
    time.sleep(.48)
    assert state()['controls']
    ok('press on time cannot activate a hidden button')

    reveal()
    button(True)
    u.SendMessageW(h, 0x1f, 0, 0)  # WM_CANCELMODE
    button(False)
    assert state()['running'] and not state()['controls']
    ok('cancelled capture cannot click')

    outside()
    move(*target())
    cmd('hide')
    time.sleep(.5)
    assert not state()['controls']
    outside()
    cmd('show')
    assert not state()['controls']
    # Keyboard activation remains immediate and does not depend on hover.
    # A real click gives focus after hide/show; background focus requests can be denied.
    move(*target())
    button(True)
    button(False)
    u.SendMessageW(h, 0x100, 9, 0)
    assert state()['controls']
    mod = comtypes.client.GetModule('oleacc.dll')
    acc = C.POINTER(mod.IAccessible)()
    getacc = C.oledll.oleacc.AccessibleObjectFromWindow
    getacc.argtypes = [W.HWND, W.DWORD, C.POINTER(comtypes.GUID), C.POINTER(C.POINTER(mod.IAccessible))]
    getacc(h, 0xfffffffc, C.byref(mod.IAccessible._iid_), C.byref(acc))
    for _ in range(4):
        if acc.accName(acc.accFocus) == '暂停': break
        u.SendMessageW(h, 0x100, 9, 0)
    assert acc.accName(acc.accFocus) == '暂停'
    u.SendMessageW(h, 0x100, 32, 0)
    assert not state()['running']
    u.SendMessageW(h, 0x100, 27, 0)
    assert not state()['controls']
    ok('hide cancels dwell; keyboard controls still work')
    (OUT/'result.json').write_text(json.dumps({'checks': checks, 'final': state()}, indent=2))
finally:
    button(False)
    if h:
        cmd('quit')
    p.wait(timeout=5)
    u.SetCursorPos(original.x, original.y)
