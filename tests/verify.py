"""Native UI / timing checks. Tests only the isolated --test instance."""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import json, time, subprocess
from PIL import ImageGrab
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'scratch/evidence'; OUT.mkdir(parents=True,exist_ok=True)
u=C.windll.user32
u.SetProcessDpiAwarenessContext(C.c_void_p(-4))
u.FindWindowW.argtypes=[W.LPCWSTR,W.LPCWSTR];u.FindWindowW.restype=W.HWND
u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
u.GetWindowRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
u.SetForegroundWindow.argtypes=[W.HWND]
class Copy(C.Structure):_fields_=[('tag',C.c_size_t),('size',W.DWORD),('data',C.c_void_p)]
h=u.FindWindowW('LightTimer.Native.Test','轻计时')
assert h,'Launch LightTimer.exe --test first'
def cmd(text):
    data=C.create_unicode_buffer(text);cd=Copy(0x4C54494D,C.sizeof(data),C.cast(data,C.c_void_p))
    assert u.SendMessageW(h,0x4a,0,C.addressof(cd))==1,text
def state():
    p=OUT/'state.json';cmd('inspect '+str(p));return json.loads(p.read_text())
def mouse(msg,x=0,y=0,hwnd=h):
    u.SetThreadDpiAwarenessContext(C.c_void_p(-4))
    if msg in (0x200,0x2a3):
        rr=W.RECT();u.GetWindowRect(hwnd,C.byref(rr));u.SetCursorPos(rr.left+x,rr.top+y) if msg==0x200 else u.SetCursorPos(rr.left-20,rr.top-20)
        time.sleep(.48 if msg==0x200 else .05)
    u.SendMessageW(hwnd,msg,0,(y<<16)|(x&0xffff))
def click(hwnd,x,y):mouse(0x201,x,y,hwnd);mouse(0x202,x,y,hwnd)
def shot(name,key='rect'):
    s=state();C.windll.dwmapi.DwmFlush();ImageGrab.grab(tuple(s[key])).save(OUT/(name+'.png'))
checks=[]
def ok(name):checks.append(name);print('PASS',name,flush=True)
cmd('sound 0');cmd('rest 0');cmd('close-settings');cmd('move 500 180');cmd('start 60');time.sleep(.15)
a=state();r=a['rect'];mouse(0x200,70,20);b=state();assert b['controls']==1 and b['rect']==r
time.sleep(1.1);mouse(0x2a3);b=state();assert b['controls']==0 and b['rect']==r and b['remainingMs']<a['remainingMs']-1000;ok('hover swaps controls without moving or pausing')
shot('running');mouse(0x200,70,20);shot('hover')
scale=b['dpi'];pause_x=round((r[2]-r[0])/2+30*scale);click(h,pause_x,round(18*scale));mouse(0x2a3);a=state();time.sleep(.5);b=state();assert not b['running'] and b['remainingMs']==a['remainingMs'];ok('real pause hit target freezes deadline')
mouse(0x200,70,20);click(h,pause_x,round(18*scale));mouse(0x2a3);assert state()['running'];ok('real continue hit target resumes')
cmd('settings');time.sleep(.1);s=state();sh=u.FindWindowW('LightTimer.Native.Test','轻计时 · 设置');assert s['settingsVisible'];shot('settings-light','settingsRect')
click(sh,round(124*scale),round(168*scale));assert state()['restMode'];shot('settings-rest','settingsRect')
click(sh,round(159*scale),round(168*scale));u.SendMessageW(sh,0x102,ord('3'),0);assert state()['restSeconds']==3
click(sh,round(124*scale),round(168*scale));assert not state()['restMode'];ok('settings icons and rest durations respond')
# Type a 2-second duration via actual key/character messages into the three fields.
for i,value in enumerate(['0','0','2']):
    click(sh,round((50+i*86)*scale),round(58*scale))
    for ch in value:u.SendMessageW(sh,0x102,ord(ch),0)
click(sh,round(252*scale),round(168*scale));assert not state()['settingsVisible'];a=state();assert 1700<=a['remainingMs']<=2000
time.sleep(2.2);b=state();assert b['completed'] and not b['restActive'] and b['completionLatencyMs']<150;shot('complete');ok('duration fields, start, compact completion and deadline accuracy')
cmd('theme 1');cmd('settings');time.sleep(.1);shot('settings-dark','settingsRect');cmd('close-settings');cmd('theme 0')
for duration in [3,4,5]:
    cmd(f'rest 1 {duration}');cmd('start 1');time.sleep(1.2);a=state();assert a['restActive'] and a['covers']>=1
    if duration==4:
        # Capture only an opaque portion of our own rest layer, not uncovered desktop.
        cw=u.FindWindowW('LightTimer.Native.Test','轻计时 · 休息');rr=W.RECT();u.GetWindowRect(cw,C.byref(rr));cx=(rr.left+rr.right)//2;cy=(rr.top+rr.bottom)//2
        ImageGrab.grab((cx-220,cy-150,cx+220,cy+150)).save(OUT/'rest.png')
    time.sleep(duration+.05);b=state();assert not b['restActive'] and not b['covers'] and b['completionCount']==a['completionCount'];ok(f'{duration}s rest overlay auto-closes, no duplicate completion')
cmd('rest 1 5');cmd('start 1');time.sleep(1.15);assert state()['restActive'];cw=u.FindWindowW('LightTimer.Native.Test','轻计时 · 休息');u.SendMessageW(cw,0x100,27,0);assert not state()['restActive'];ok('Escape closes rest overlay')
cmd('rest 0');cmd('start 90');cmd('hide');a=state();time.sleep(1.2);b=state();assert b['paintCount']==a['paintCount'] and b['wakeCount']==a['wakeCount'];ok('hidden timer has no periodic paints or callbacks')
cmd('show');cmd('toggle');mouse(0x2a3);time.sleep(.3);a=state();time.sleep(1.2);b=state();assert b['paintCount']==a['paintCount'] and b['wakeCount']==a['wakeCount'];ok('paused timer has no periodic paints or callbacks')
for i in range(30):cmd('settings');cmd('close-settings')
a=state()
for i in range(30):cmd('settings');cmd('close-settings')
b=state();assert b['gdi']==a['gdi'] and b['user']==a['user'];ok('30 additional settings cycles have no GDI or USER handle growth')
cmd('start 1500');cmd('toggle')
(OUT/'functional.json').write_text(json.dumps({'checks':checks,'final':state()},ensure_ascii=False,indent=2),'utf-8')
print('ALL PASS',len(checks),flush=True)
