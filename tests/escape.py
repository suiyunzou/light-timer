from pathlib import Path
import ctypes as C,json,time
from ctypes import wintypes as W
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'scratch/evidence';u=C.windll.user32
u.FindWindowW.argtypes=[W.LPCWSTR,W.LPCWSTR];u.FindWindowW.restype=W.HWND;u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SetForegroundWindow.argtypes=[W.HWND]
h=u.FindWindowW('LightTimer.Native.Test','轻计时');assert h
class Copy(C.Structure):_fields_=[('tag',C.c_size_t),('size',W.DWORD),('data',C.c_void_p)]
def cmd(s):
 b=C.create_unicode_buffer(s);cd=Copy(0x4c54494d,C.sizeof(b),C.cast(b,C.c_void_p));u.SendMessageW(h,0x4a,0,C.addressof(cd))
def state():
 p=OUT/'escape-state.json';cmd('inspect '+str(p));return json.loads(p.read_text())
cmd('sound 0');cmd('rest 0');cmd('close-settings');cmd('show');cmd('start 60');u.SetThreadDpiAwarenessContext(C.c_void_p(-4));u.SetCursorPos(400,500);u.SendMessageW(h,0x2a3,0,0);u.SetForegroundWindow(h);u.SendMessageW(h,0x100,9,0);a=state();assert a['controls']
u.SendMessageW(h,0x100,27,0);b=state();assert not b['controls'] and b['scheduledMs']<=1000
time.sleep(1.1);c=state();assert c['paintCount']>b['paintCount'] and c['remainingMs']<b['remainingMs']
(OUT/'escape.json').write_text(json.dumps(dict(before=a,after=b,later=c),indent=2),'utf-8');cmd('start 1500');cmd('toggle');print('PASS Escape: controls -> time, 1Hz refresh resumes')
