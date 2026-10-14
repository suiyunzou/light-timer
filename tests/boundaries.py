"""Additional native boundary checks. Run after resource profiling, not during it."""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import json,time
import psutil
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'scratch/evidence'
u=C.windll.user32
u.SetProcessDpiAwarenessContext(C.c_void_p(-4))
u.FindWindowW.argtypes=[W.LPCWSTR,W.LPCWSTR];u.FindWindowW.restype=W.HWND
u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
class Copy(C.Structure):_fields_=[('tag',C.c_size_t),('size',W.DWORD),('data',C.c_void_p)]
h=u.FindWindowW('LightTimer.Native.Test','轻计时');assert h

def cmd(text):
 b=C.create_unicode_buffer(text);cd=Copy(0x4C54494D,C.sizeof(b),C.cast(b,C.c_void_p));assert u.SendMessageW(h,0x4a,0,C.addressof(cd))==1

def state():
 p=OUT/'boundaries-state.json';cmd('inspect '+str(p));return json.loads(p.read_text())

checks=[]
def ok(s):checks.append(s);print('PASS',s,flush=True)
cmd('sound 0');cmd('rest 0');cmd('move 500 180');cmd('start 1');cmd('hide');time.sleep(1.2)
s=state();assert s['completed'] and not s['visible'];ok('hidden timer still completes without showing window')
cmd('show');cmd('start 1');s=state();x,y=s['rect'][:2];u.SetThreadDpiAwarenessContext(C.c_void_p(-4));u.SetCursorPos(x+30,y+20);u.SendMessageW(h,0x200,0,20<<16|30);time.sleep(1.2)
s=state();pt=W.POINT();u.GetCursorPos(C.byref(pt));assert s['completed'] and s['controls'],dict(state=s,cursor=[pt.x,pt.y]);u.SetCursorPos(20,20);u.SendMessageW(h,0x2a3,0,0);assert not state()['controls'];ok('expiry while hovering preserves control state then reveals completion')
cmd('start 60');u.SetForegroundWindow.argtypes=[W.HWND];u.SetForegroundWindow(h);u.SendMessageW(h,0x100,9,0)
assert state()['controls'];u.SendMessageW(h,0x100,27,0);a=state();assert not a['controls'] and a['scheduledMs']<=1000
time.sleep(1.1);assert state()['paintCount']>a['paintCount'];ok('Escape restores normal countdown refresh after keyboard controls')
cmd('start 359999');cmd('toggle');s=state();assert s['rect'][2]-s['rect'][0]==int(152*s['dpi']);ok('99:59:59 uses wider hour format')
# These are simulated WM_DPICHANGED notifications, not physical mixed-DPI monitor tests.
rect=W.RECT(*s['rect']);sizes=[]
for dpi in [96,144,192,120]:
 u.SendMessageW(h,0x2e0,(dpi<<16)|dpi,C.addressof(rect));s=state();width=s['rect'][2]-s['rect'][0];height=s['rect'][3]-s['rect'][1]
 assert width==int(152*dpi/96) and height==int(36*dpi/96)
 sizes.append([dpi,width,height])
ok('simulated DPI 100/150/200/125 percent preserves logical size')
cmd('move -5000 -5000');s=state();assert s['rect'][0]>=0 and s['rect'][1]>=0;ok('offscreen position clamps into current monitor work area')
cmd('move 500 180');cmd('settings');sh=u.FindWindowW('LightTimer.Native.Test','轻计时 · 设置');s=state();scale=s['dpi']
for i in range(3):
 x=round((50+i*86)*scale);y=round(58*scale);u.SendMessageW(sh,0x201,0,y<<16|x);u.SendMessageW(sh,0x102,ord('0'),0)
u.SendMessageW(sh,0x100,13,0);s=state();assert s['settingsVisible'] and not s['running'];ok('zero duration is rejected and settings stay open')
# Start with native keyboard messages from the seconds field.
u.SendMessageW(sh,0x102,ord('2'),0);u.SendMessageW(sh,0x100,13,0);assert state()['running'] and not state()['settingsVisible'];ok('Enter starts typed duration')
proc=psutil.Process(state()['pid'])
def memory():
 m=proc.memory_full_info();return dict(private_working_set=m.uss,working_set=m.rss,private_commit=m.private)
audio_before=memory();cmd('sound 1');cmd('start 1');audio_samples=[]
for _ in range(30):
 audio_samples.append(memory());time.sleep(.1)
assert state()['completed'];ok('generated completion sound path finishes without crash')
audio_after=memory();audio_peak={k:max(m[k] for m in audio_samples) for k in audio_before}
cmd('sound 0');cmd('start 1500');cmd('toggle');u.SetCursorPos(20,20)
(OUT/'boundaries.json').write_text(json.dumps(dict(checks=checks,simulated_dpi_sizes=sizes,audio_memory=dict(before=audio_before,peak=audio_peak,after=audio_after),final=state()),indent=2),'utf-8')
print('BOUNDARIES PASS',len(checks),flush=True)
