"""Isolated native UI checks for view selection, timing ownership and compact geometry."""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import datetime, json, subprocess, sys, time, wave
import comtypes, comtypes.client
from PIL import ImageGrab

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'scratch/time-views/evidence'
OUT.mkdir(parents=True, exist_ok=True)
exe = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'dist/LightTimer.exe'
u = C.windll.user32
u.SetThreadDpiAwarenessContext(C.c_void_p(-4))
u.FindWindowW.argtypes = [W.LPCWSTR, W.LPCWSTR]; u.FindWindowW.restype = W.HWND
u.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]; u.SendMessageW.restype = C.c_ssize_t
u.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u.GetMenuState.argtypes = [W.HMENU, W.UINT, W.UINT]
u.GetMenuItemID.argtypes = [W.HMENU, C.c_int]
u.GetSubMenu.argtypes = [W.HMENU, C.c_int]; u.GetSubMenu.restype = W.HMENU
u.GetMenuItemRect.argtypes = [W.HWND, W.HMENU, W.UINT, C.POINTER(W.RECT)]
u.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
u.SetForegroundWindow.argtypes = [W.HWND]
class Copy(C.Structure):
    _fields_ = [('tag', C.c_size_t), ('size', W.DWORD), ('data', C.c_void_p)]

assert not u.FindWindowW('LightTimer.Native.Test', '轻计时'), 'Another isolated test is running'
config = OUT / 'test.ini'
config.write_text('[timer]\nsound=0\nrest=0\nseconds=1500\n', encoding='utf-8')
original = W.POINT(); u.GetCursorPos(C.byref(original))
p = subprocess.Popen([str(exe), '--test', '--config', str(config)])
h = 0; checks = []
try:
    for _ in range(60):
        h = u.FindWindowW('LightTimer.Native.Test', '轻计时')
        if h: break
        time.sleep(.05)
    assert h
    def cmd(text):
        b = C.create_unicode_buffer(text)
        cd = Copy(0x4c54494d, C.sizeof(b), C.cast(b, C.c_void_p))
        assert u.SendMessageW(h, 0x4a, 0, C.addressof(cd)) == 1
    def state():
        path = OUT / 'state.json'; cmd('inspect ' + str(path))
        return json.loads(path.read_text())
    def size(width=116):
        s = state(); r = s['rect']; d = s['dpi']
        assert r[2]-r[0] == int(width*d) and r[3]-r[1] == int(36*d), s
    def key(vk):
        u.SetForegroundWindow(h)
        u.SendMessageW(h, 0x100, vk, 0); u.SendMessageW(h, 0x101, vk, 0)
    def leave():
        u.SetCursorPos(800, 700); key(27); u.SendMessageW(h, 0x2a3, 0, 0)
    def click(x, y=18, post=False):
        d = state()['dpi']; lp = (round(y*d)<<16) | round(x*d)
        u.SendMessageW(h, 0x201, 1, lp)
        if post: u.PostMessageW(h, 0x202, 0, lp)
        else: u.SendMessageW(h, 0x202, 0, lp)
    def shot(name):
        leave(); time.sleep(.08); ImageGrab.grab(tuple(state()['rect'])).save(OUT / name)
    def menu_select(view):
        # Use the visible mode button and the native popup's real menu item rectangles.
        leave()
        s = state(); d = s['dpi']; r = s['rect']
        u.SetCursorPos(r[0] + round(14*d), r[1] + round(18*d))
        time.sleep(.48)
        click(14, post=True)
        popup = 0
        for _ in range(40):
            popup = u.FindWindowW('#32768', None)
            if popup: break
            time.sleep(.025)
        assert popup, 'Mode button did not open its menu'
        menu = u.SendMessageW(popup, 0x1e1, 0, 0)
        assert all(u.GetMenuItemID(menu, i) == 130+i for i in range(3))
        assert u.GetMenuState(menu, 130+state()['view'], 0) & 8
        if view == 1:
            time.sleep(.25)  # Wait for the native popup fade before capturing its colors.
            rc = W.RECT(); u.GetWindowRect(popup, C.byref(rc))
            ImageGrab.grab((rc.left, rc.top, rc.right, rc.bottom)).save(OUT/'view-menu.png')
        rc = W.RECT(); assert u.GetMenuItemRect(None, menu, view, C.byref(rc))
        u.SetCursorPos((rc.left+rc.right)//2, (rc.top+rc.bottom)//2)
        u.mouse_event(2, 0, 0, 0, 0); u.mouse_event(4, 0, 0, 0, 0)
        time.sleep(.12)
        assert state()['view'] == view, state()
        size()
        leave()

    cmd('move 160 160'); cmd('reduced 1'); leave(); size(); shot('countdown.png')
    cmd('start 30'); menu_select(2)
    s = state(); assert s['running'] and not s['stopwatchRunning']
    assert s['display'] == datetime.datetime.now().strftime('%H:%M')
    assert 0 < s['scheduledMs'] <= 30000
    shot('clock.png')
    menu_select(1); assert state()['running'], 'Viewing stopwatch paused countdown'
    key(9)  # Reveal keyboard controls, then exercise native release-only action.
    s = state(); d = s['dpi']; lp = (round(18*d)<<16) | round(53*d)
    u.SendMessageW(h, 0x201, 1, lp)
    assert not state()['stopwatchRunning'], 'Action ran on mouse down'
    u.SendMessageW(h, 0x202, 0, lp)
    s = state(); assert s['stopwatchRunning'] and not s['running'], s
    remaining = s['remainingMs']; time.sleep(1.12)
    shot('stopwatch.png')
    assert state()['elapsedMs'] >= 1000 and state()['remainingMs'] == remaining
    checks.append('real selector menu, view-only switching, release-only start, single running task')

    mod = comtypes.client.GetModule('oleacc.dll'); acc = C.POINTER(mod.IAccessible)()
    getacc = C.oledll.oleacc.AccessibleObjectFromWindow
    getacc.argtypes = [W.HWND, W.DWORD, C.POINTER(comtypes.GUID), C.POINTER(C.POINTER(mod.IAccessible))]
    getacc(h, 0xfffffffc, C.byref(mod.IAccessible._iid_), C.byref(acc))
    key(9)
    names = lambda: [acc.accName(i) for i in range(1, acc.accChildCount+1)]
    reset = names().index('归零')+1
    assert acc.accState(reset) & 1
    before = state()['elapsedMs']; acc.accDoDefaultAction(reset)
    assert state()['elapsedMs'] >= before
    ImageGrab.grab(tuple(state()['rect'])).save(OUT/'stopwatch-controls.png')
    click(53); s = state(); assert not s['stopwatchRunning']
    frozen = s['elapsedMs']; time.sleep(.16); assert state()['elapsedMs'] == frozen
    click(95); assert state()['elapsedMs'] == 0; size()
    checks.append('disabled reset exposed to accessibility, pause freezes, reset works only when paused')

    menu_select(0); assert state()['remainingMs'] == remaining
    key(9); click(53); assert state()['running']
    menu_select(1); key(9); click(53)
    assert state()['stopwatchRunning'] and not state()['running']
    cmd('hide'); a=state(); time.sleep(.12); b=state()
    assert b['elapsedMs'] > a['elapsedMs'] and b['paintCount'] == a['paintCount']
    cmd('show'); leave()
    cmd('elapsed 3599500'); cmd('toggle'); time.sleep(.65)
    size(152); assert state()['display'] == '01:00:00'; shot('stopwatch-hour.png')
    cmd('toggle'); key(9); click(120); size()
    assert state()['elapsedMs'] == 0
    cmd('elapsed 359998900'); cmd('toggle'); time.sleep(.18)
    assert not state()['stopwatchRunning'] and state()['display'] == '99:59:59'
    size(152); shot('stopwatch-maximum.png')
    checks.append('hidden stopwatch keeps time without redraw, hour boundary, upper bound and compact sizes')

    cmd('start 1'); menu_select(2); time.sleep(1.05)
    s=state(); assert s['completed'] and s['view'] == 0 and s['display'] == '00:00', s
    size(); shot('finished.png')
    key(9); click(53); assert state()['running'] and not state()['completed']
    cmd('toggle'); menu_select(2); leave()
    assert '当前时间' in acc.accName(0)
    assert not any(n in ('暂停', '归零') for n in names())
    a=state(); time.sleep(.12); b=state()
    assert b['wakeCount'] == a['wakeCount'], 'Wall clock wakes unnecessarily every frame'
    u.SendMessageW(h, 0x1e, 0, 0) # WM_TIMECHANGE: no system clock mutation.
    assert state()['display'] == datetime.datetime.now().strftime('%H:%M')
    assert 'view=2' in config.read_text(encoding='utf-16')
    checks.append('background countdown finishes visibly, restart, clock accessibility and system-time notification')

    # Restart the isolated instance: retain selected view, never revive old progress.
    cmd('quit'); p.wait(timeout=5); h=0
    p=subprocess.Popen([str(exe), '--test', '--config', str(config)])
    for _ in range(60):
        h=u.FindWindowW('LightTimer.Native.Test', '轻计时')
        if h:break
        time.sleep(.05)
    assert h
    s=state(); assert s['view']==2 and not s['running'] and not s['stopwatchRunning'] and s['elapsedMs']==0
    size(); checks.append('selected view persists across actual process restart')
    cmd('quit'); p.wait(timeout=5); h=0
    song = OUT/'layout-check.wav'
    with wave.open(str(song), 'wb') as wav:
        wav.setparams((1, 2, 22050, 0, 'NONE', 'not compressed'))
        wav.writeframes(b'\0\0' * 22050 * 8)  # Exercise real playback without audible test noise.
    config.write_text('[timer]\npersonal=1\nsound=0\nrest=0\nmusic=1\nmusicFile='+str(song)+'\n', encoding='utf-16')
    p=subprocess.Popen([str(exe), '--test', '--config', str(config)])
    for _ in range(60):
        h=u.FindWindowW('LightTimer.Native.Test', '轻计时')
        if h:break
        time.sleep(.05)
    assert h
    cmd('move 160 160');cmd('start 1');time.sleep(1.15)
    assert state()['musicPlaying']
    size();shot('music-countdown.png');key(9)
    ImageGrab.grab(tuple(state()['rect'])).save(OUT/'music-controls.png')
    acc = C.POINTER(mod.IAccessible)()
    getacc(h, 0xfffffffc, C.byref(mod.IAccessible._iid_), C.byref(acc))
    assert acc.accChildCount == 4
    # Hit targets fit the float and do not overlap, including all four music controls.
    previous_right = state()['rect'][0]
    for i in range(1, acc.accChildCount+1):
        x,y,w,ht = acc.accLocation(i)
        assert x >= previous_right and x+w <= state()['rect'][2]
        previous_right = x+w
    menu_select(1);key(9)
    assert '停止音乐' in names() and '归零' in names()
    menu_select(2);shot('music-clock.png')
    assert '停止音乐' in names()
    click(102);assert not state()['musicPlaying'];size()
    checks.append('actual music playback: four non-overlapping controls, compact clock digits, stop in every view')
    (OUT/'result.json').write_text(json.dumps({'checks':checks,'final':s}, ensure_ascii=False, indent=2), encoding='utf-8')
    print('PASS:', *checks, sep='\n- ')
finally:
    if h and u.IsWindow(h):
        u.PostMessageW(h, 0x100, 27, 0)
        cmd('quit')
    p.wait(timeout=5)
    u.SetCursorPos(original.x, original.y)
