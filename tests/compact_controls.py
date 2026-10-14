from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import subprocess,time,json,sys
from PIL import ImageGrab
r=Path(__file__).resolve().parents[1]; out=r/'scratch/compact-controls';out.mkdir(parents=True,exist_ok=True); baseline='--baseline' in sys.argv
u=C.windll.user32; u.SetProcessDpiAwarenessContext(C.c_void_p(-4))
u.FindWindowW.argtypes=[W.LPCWSTR,W.LPCWSTR];u.FindWindowW.restype=W.HWND
u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
class Copy(C.Structure):_fields_=[('tag',C.c_size_t),('size',W.DWORD),('data',C.c_void_p)]
personal='--personal' in sys.argv
config=out/'preview.ini';config.write_text('[timer]\nrest=0\nrestSeconds=4\nbreath=1\nsound=0\n'+f'personal={int(personal)}\n',encoding='utf-8')
assert not u.FindWindowW('LightTimer.Native.Test','轻计时'),'Existing test instance'
exe=Path(sys.argv[1]) if len(sys.argv)>1 and sys.argv[1]!='--baseline' else r/'dist/LightTimer.exe'
p=subprocess.Popen([str(exe),'--test','--config',str(config)])
h=0
try:
 for _ in range(40):
  h=u.FindWindowW('LightTimer.Native.Test','轻计时')
  if h:break
  time.sleep(.05)
 assert h
 def cmd(t):
  b=C.create_unicode_buffer(t);cd=Copy(0x4c54494d,C.sizeof(b),C.cast(b,C.c_void_p));assert u.SendMessageW(h,0x4a,0,C.addressof(cd))==1
 def state():
  f=out/'state.json';cmd('inspect '+str(f));return json.loads(f.read_text())
 cmd('move 40 400');cmd('start 1500');cmd('settings')
 sh=u.FindWindowW('LightTimer.Native.Test','轻计时 · 设置');s=state();d=s['dpi']
 def click(x,y):
  u.SendMessageW(sh,0x201,1,(round(y*d)<<16)|round(x*d));u.SendMessageW(sh,0x202,0,(round(y*d)<<16)|round(x*d))
 def shot(name):
  time.sleep(.3);ImageGrab.grab(tuple(state()['settingsRect'])).save(out/(('personal-' if personal else 'work-')+name))
 # Main window retains only duration inputs, presets and start.
 initial=state()['settingsRect'];assert initial[2]-initial[0]==round(288*d) and initial[3]-initial[1]==round(156*d)
 click(130,100)
 assert not state()['restMode'] and state()['restSeconds']==4 and not state()['tipVisible']
 assert state()['settingsRect']==initial
 shot('settings.png')
 import comtypes,comtypes.client
 mod=comtypes.client.GetModule('oleacc.dll');acc=C.POINTER(mod.IAccessible)()
 f=C.oledll.oleacc.AccessibleObjectFromWindow;f.argtypes=[W.HWND,W.DWORD,C.POINTER(comtypes.GUID),C.POINTER(C.POINTER(mod.IAccessible))]
 f(sh,0xfffffffc,C.byref(mod.IAccessible._iid_),C.byref(acc))
 names=[acc.accName(i) for i in range(1,acc.accChildCount+1)]
 minute=names.index('分钟')+1
 field_ids=[names.index(name)+1 for name in ['小时','分钟','秒']]
 def wheel(index,delta=-120):
  rc=state()['settingsRect'];x=rc[0]+round((50+index*86)*d);y=rc[1]+round(58*d)
  u.SendMessageW(sh,0x20a,(delta&0xffff)<<16,((y&0xffff)<<16)|(x&0xffff))
 def values():return [int(acc.accValue(i)) for i in field_ids]
 # Real OS wheel down increases; wheel up decreases.
 rc=state()['settingsRect'];u.SetCursorPos(rc[0]+round(136*d),rc[1]+round(58*d));time.sleep(.1)
 u.mouse_event(0x800,0,0,C.c_ulong(-120).value,0);time.sleep(.07)
 C.windll.dwmapi.DwmFlush()
 ImageGrab.grab(tuple(rc)).save(out/(('personal-' if personal else 'work-')+'wheel-frame.png'))
 assert values()==[0,26,0]
 u.mouse_event(0x800,0,0,120,0);time.sleep(.26)
 assert values()==[0,25,0]
 wheel(1,-60);assert values()==[0,25,0]
 wheel(1,-60);assert values()==[0,26,0]
 time.sleep(.025);ImageGrab.grab(tuple(state()['settingsRect'])).save(out/(('personal-' if personal else 'work-')+'reel-motion.png'))
 wheel(2);assert values()==[0,26,1]
 wheel(0,120);assert values()==[0,26,1]
 wheel(0,-120*120);wheel(1,-120*100);wheel(2,-120*100)
 assert values()==[99,59,59]
 wheel(2);assert values()==[99,59,59]
 wheel(2,120*100);assert values()==[99,59,0]
 # Partial wheel motion must not transfer to another field.
 wheel(2,-60);wheel(1,60);assert values()==[99,59,0]
 wheel(1,60);assert values()==[99,58,0]
 cmd('reduced 1');wheel(2);assert values()==[99,58,1]
 shot('reduced-motion.png');cmd('reduced 0')
 # Click and type immediately after a wheel animation; no stale digit returns.
 wheel(1);click(136,58);u.SendMessageW(sh,0x102,ord('2'),0)
 time.sleep(.26);assert values()==[99,2,1]
 cmd('toggle');time.sleep(.25);paints=state()['paintCount'];time.sleep(.3)
 assert state()['paintCount']==paints,'Settled settings must not keep repainting'
 for i,value in enumerate([5,15,25,45]):
  click(40+i*51,122)
  assert acc.accValue(minute)==str(value) and state()['settingsVisible']
  assert state()['settingsRect']==initial
 # Invalid zero duration adds its message below the action row.
 for i in range(3):
  click(50+i*86,58);u.SendMessageW(sh,0x102,ord('0'),0)
 click(252,122);assert state()['settingsVisible']
 rc=state()['settingsRect'];assert rc[3]-rc[1]==round(176*d)
 shot('zero-error.png')
 click(40,122);assert state()['settingsRect']==initial
 for i,value in enumerate(['0','0','10']):
  click(50+i*86,58)
  for ch in value:u.SendMessageW(sh,0x102,ord(ch),0)
 click(252,122);s=state();assert s['running'] and 9000<s['remainingMs']<=10000 and not s['settingsVisible'] and not s['tipVisible']
 (out/'result.json').write_text(json.dumps(dict(pass_checks=['layout','retired_controls_inert','duration_input','play'],final=s),indent=2),encoding='utf-8')
 print('PASS: compact layout, wheel target/partial deltas/bounds/reduced motion/type interruption, all presets, zero error and recovery, input and start; personal=',personal)

finally:
 if h:cmd('quit')
 p.wait(timeout=5)
