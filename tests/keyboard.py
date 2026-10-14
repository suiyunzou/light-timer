"""Real SendInput regression: editing settings while countdown continues."""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import time,json,sys
from PIL import ImageGrab
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'scratch/evidence';u=C.windll.user32
u.FindWindowW.argtypes=[W.LPCWSTR,W.LPCWSTR];u.FindWindowW.restype=W.HWND
u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
u.GetForegroundWindow.restype=W.HWND
u.WindowFromPoint.argtypes=[W.POINT];u.WindowFromPoint.restype=W.HWND
class Copy(C.Structure):_fields_=[('tag',C.c_size_t),('size',W.DWORD),('data',C.c_void_p)]
class MI(C.Structure):_fields_=[('dx',W.LONG),('dy',W.LONG),('mouseData',W.DWORD),('dwFlags',W.DWORD),('time',W.DWORD),('extra',C.c_size_t)]
class KI(C.Structure):_fields_=[('vk',W.WORD),('scan',W.WORD),('flags',W.DWORD),('time',W.DWORD),('extra',C.c_size_t)]
class U(C.Union):_fields_=[('mi',MI),('ki',KI)]
class Input(C.Structure):_anonymous_=('u',);_fields_=[('type',W.DWORD),('u',U)]
class GUI(C.Structure):_fields_=[('size',W.DWORD),('flags',W.DWORD),('active',W.HWND),('focus',W.HWND),('capture',W.HWND),('menu',W.HWND),('move',W.HWND),('caret',W.HWND),('rect',W.RECT)]
u.SendInput.argtypes=[W.UINT,C.POINTER(Input),C.c_int];u.SendInput.restype=W.UINT
u.GetGUIThreadInfo.argtypes=[W.DWORD,C.POINTER(GUI)]
h=u.FindWindowW('LightTimer.Native.Test','轻计时');assert h

def cmd(t):
 b=C.create_unicode_buffer(t);cd=Copy(0x4c54494d,C.sizeof(b),C.cast(b,C.c_void_p));assert u.SendMessageW(h,0x4a,0,C.addressof(cd))==1

def state():
 p=OUT/'keyboard-state.json';cmd('inspect '+str(p));return json.loads(p.read_text())

def focus():
 g=GUI();g.size=C.sizeof(g);assert u.GetGUIThreadInfo(0,C.byref(g));return g.focus

def send(events):
 arr=(Input*len(events))(*events);assert u.SendInput(len(arr),arr,C.sizeof(Input))==len(arr)

def click(x,y):
 u.SetThreadDpiAwarenessContext(C.c_void_p(-4));u.SetCursorPos(x,y);assert u.WindowFromPoint(W.POINT(x,y))==sh
 send([Input(type=0,mi=MI(dwFlags=2)),Input(type=0,mi=MI(dwFlags=4))]);time.sleep(.1)

def type_text(s):
 assert focus()==sh and u.GetForegroundWindow()==sh,'Do not send keys to another window'
 for ch in s:send([Input(type=1,ki=KI(vk=ord(ch),flags=0)),Input(type=1,ki=KI(vk=ord(ch),flags=2))])
 time.sleep(.1)

cmd('sound 0');cmd('rest 0');cmd('start 90');cmd('settings');time.sleep(.15)
sh=u.FindWindowW('LightTimer.Native.Test','轻计时 · 设置');s=state();x,y=s['settingsRect'][:2];d=s['dpi']
click(x+round(136*d),y+round(58*d))
result=dict(keyboard_focus_is_settings=focus()==sh,settings_is_foreground=u.GetForegroundWindow()==sh,running=state()['running'])
if '--before' in sys.argv:
 (OUT/'keyboard-before.json').write_text(json.dumps(result,indent=2));print('BEFORE',result);sys.exit(0)
assert result['keyboard_focus_is_settings'],result
u.SetThreadDpiAwarenessContext(C.c_void_p(-4));ImageGrab.grab(tuple(s['settingsRect'])).save(OUT/'input-selected.png')
for i,value in enumerate(['0','7','13']):
 click(x+round((50+i*86)*d),y+round(58*d));type_text(value)
# Real keyboard input is queued to the focused native window. A second must not steal focus.
time.sleep(1.1);assert focus()==sh and state()['running']
u.SetThreadDpiAwarenessContext(C.c_void_p(-4));ImageGrab.grab(tuple(s['settingsRect'])).save(OUT/'input-edited.png')
click(x+round(252*d),y+round(122*d));s=state();assert s['running'] and not s['settingsVisible'] and 432000<s['remainingMs']<=433000
result.update(typed_duration_seconds=433,started_remaining_ms=s['remainingMs'],focus_survived_timer_tick=True)
(OUT/'keyboard-after.json').write_text(json.dumps(result,indent=2),'utf-8');print('PASS real mouse + keyboard: running timer -> custom 00:07:13 -> start',result)
cmd('start 1500');cmd('toggle')
