"""Targeted native UI, audio lifecycle, persistence and reduced-motion checks."""
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
OUT = ROOT / 'scratch/rest-music/evidence'
OUT.mkdir(parents=True, exist_ok=True)
exe = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'dist/LightTimer.exe'
u = C.windll.user32
u.SetThreadDpiAwarenessContext(C.c_void_p(-4))
u.FindWindowW.argtypes = [W.LPCWSTR, W.LPCWSTR]
u.FindWindowW.restype = W.HWND
u.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u.SendMessageW.restype = C.c_ssize_t
class Copy(C.Structure):
    _fields_ = [('tag', C.c_size_t), ('size', W.DWORD), ('data', C.c_void_p)]

assert not u.FindWindowW('LightTimer.Native.Test', '轻计时'), 'Existing test instance'
config = OUT / 'test.ini'
config.write_text('[timer]\nseconds=1500\nsound=1\nrest=0\nbreath=0\n', encoding='utf-8')
p = None
h = 0
checks = []
def launch():
    global p, h
    p = subprocess.Popen([str(exe), '--test', '--config', str(config)])
    for _ in range(60):
        h = u.FindWindowW('LightTimer.Native.Test', '轻计时')
        if h:
            return
        time.sleep(.05)
    raise AssertionError('No test window')
def cmd(text):
    b = C.create_unicode_buffer(text)
    cd = Copy(0x4c54494d, C.sizeof(b), C.cast(b, C.c_void_p))
    assert u.SendMessageW(h, 0x4a, 0, C.addressof(cd)) == 1
def state():
    f = OUT / 'state.json'
    cmd('inspect ' + str(f))
    return json.loads(f.read_text())
def settings():
    cmd('settings')
    time.sleep(.25)
    return u.FindWindowW('LightTimer.Native.Test', '轻计时 · 设置')
def click(sh, x, y):
    d = state()['dpi']
    lp = (round(y * d) << 16) | round(x * d)
    u.SendMessageW(sh, 0x201, 1, lp)
    u.SendMessageW(sh, 0x202, 0, lp)
def shot_settings(name):
    time.sleep(.22)
    ImageGrab.grab(tuple(state()['settingsRect'])).save(OUT / name)
def shot_orb(name):
    sh = u.FindWindowW('LightTimer.Native.Test', '轻计时 · 呼吸')
    # The primary monitor center is the application's real rest orb position.
    w, height = u.GetSystemMetrics(0), u.GetSystemMetrics(1)
    radius = round(112 * state()['dpi'])
    ImageGrab.grab((w//2-radius, height//2-radius, w//2+radius, height//2+radius)).save(OUT / name)

try:
    launch()
    cmd('move 60 300')
    sh = settings()
    s = state()
    assert not s['music'] and not s['musicPlaying']
    assert s['settingsRect'][3] - s['settingsRect'][1] == int(204 * s['dpi'])
    click(sh, 70, 168)
    assert state()['music']
    cmd('theme 1')
    shot_settings('settings-dark.png')
    cmd('theme 0')
    shot_settings('settings-light.png')
    cmd('theme 1')
    mod = comtypes.client.GetModule('oleacc.dll')
    acc = C.POINTER(mod.IAccessible)()
    getacc = C.oledll.oleacc.AccessibleObjectFromWindow
    getacc.argtypes = [W.HWND, W.DWORD, C.POINTER(comtypes.GUID), C.POINTER(C.POINTER(mod.IAccessible))]
    getacc(sh, 0xfffffffc, C.byref(mod.IAccessible._iid_), C.byref(acc))
    names = [acc.accName(i) for i in range(1, acc.accChildCount + 1)]
    music_id = names.index('全屏休息时播放音乐，开启时试听') + 1
    assert acc.accRole(music_id) == 44 and acc.accState(music_id) & 16
    # Keyboard activation follows the same user-facing music control.
    acc.accSelect(1, music_id)
    u.SendMessageW(sh, 0x100, 0x20, 0)
    assert not state()['music']
    u.SendMessageW(sh, 0x100, 0x20, 0)
    assert state()['music']
    checks += ['settings_layout', 'toggle_mouse_keyboard_msaa']
    assert state()['musicPlaying'] and state()['musicPreview'] and not state()['audioFailed']
    time.sleep(.8)
    assert state()['musicPlaying'], 'Playback must not be stopped by the old chime timer'
    click(sh, 70, 168)
    assert not state()['musicPlaying'] and not state()['music']
    click(sh, 70, 168)
    cmd('close-settings')
    assert not state()['musicPlaying']
    checks += ['preview_play_stop', 'preview_close_cleanup']
    cmd('quit')
    p.wait(timeout=5)
    h = 0
    launch()
    assert state()['music'], 'Music preference must survive restart'
    sh = settings()
    click(sh, 34, 168)
    click(sh, 159, 168)
    u.SendMessageW(sh, 0x102, ord('5'), 0)
    assert state()['restMode'] and state()['restSeconds'] == 5
    shot_settings('settings-rest.png')
    # Use the actual time fields and play button to reach the full-screen rest.
    for i, value in enumerate('001'):
        click(sh, 50 + 86 * i, 58)
        u.SendMessageW(sh, 0x102, ord(value), 0)
    click(sh, 252, 168)
    time.sleep(1.3)
    s = state()
    assert s['restActive'] and s['musicPlaying'] and not s['musicPreview'] and not s['audioFailed']
    shot_orb('rest-early.png')
    time.sleep(.8)
    shot_orb('rest-later.png')
    assert state()['musicPlaying']
    u.SendMessageW(h, 0x100, 0x1b, 0)
    assert not state()['restActive'] and not state()['musicPlaying']
    checks += ['persistence', 'settings_start_to_rest', 'escape_stops_music']
    # Shortest and longest durations terminate sound and overlays together.
    for seconds in [1, 5]:
        cmd(f'rest 1 {seconds}')
        cmd('start 1')
        time.sleep(1.12)
        assert state()['restActive'] and state()['musicPlaying']
        time.sleep(seconds + .15)
        assert not state()['restActive'] and not state()['musicPlaying']
    checks.append('one_and_five_second_auto_stop')
    # A floating completion never plays the rest music.
    cmd('rest 0')
    cmd('sound 0')
    cmd('start 1')
    time.sleep(1.2)
    assert not state()['musicPlaying'] and not state()['restActive']
    cmd('reduced 1')
    sh = settings()
    click(sh, 70, 168)
    cmd('rest 1 1')
    cmd('start 1')
    time.sleep(1.12)
    assert state()['restActive'] and not state()['musicPlaying']
    time.sleep(1.2)
    sh = settings()
    time.sleep(.3)
    a = state()['paintCount']
    time.sleep(.3)
    assert state()['paintCount'] == a, 'Settled UI must stop animation redraws'
    checks += ['music_off', 'floating_mode_silent', 'reduced_motion_settled_idle']
    (OUT / 'result.json').write_text(json.dumps({'checks': checks, 'final': state()}, indent=2), encoding='utf-8')
    print('PASS: ' + ', '.join(checks))
finally:
    if h:
        cmd('quit')
    if p:
        p.wait(timeout=5)
