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
config=out/'preview.ini';config.write_text('[timer]\nrest=0\nrestSeconds=4\nbreath=1\nsound=0\n',encoding='utf-8')
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
  time.sleep(.1);ImageGrab.grab(tuple(state()['settingsRect'])).save(out/name)
 # Two mode changes and one input/arrow sequence cover the edited controls.
 initial=state()['settingsRect'];assert initial[2]-initial[0]==round(288*d) and initial[3]-initial[1]==round(204*d)
 shot('floating.png')
 click(34,168);assert state()['breath']==0 and state()['restMode']==1 and state()['tipVisible']
 shot('mode-tip.png')
 time.sleep(1.3);assert not state()['tipVisible']
 assert state()['settingsRect']==initial
 shot('rest.png')
 click(159,168);u.SendMessageW(sh,0x102,ord('5'),0);assert state()['restSeconds']==5
 click(199,158);assert state()['restSeconds']==5
 click(199,178);assert state()['restSeconds']==4
 click(159,168);u.SendMessageW(sh,0x102,ord('1'),0)
 u.SendMessageW(sh,0x100,0x28,0);assert state()['restSeconds']==1
 u.SendMessageW(sh,0x100,0x26,0);assert state()['restSeconds']==2
 u.SendMessageW(sh,0x102,ord('4'),0)
 # Hover uses the actual pointer location so TrackMouseEvent remains valid.
 rect=state()['settingsRect'];u.SetCursorPos(rect[0]+round(70*d),rect[1]+round(168*d))
 u.SendMessageW(sh,0x200,0,round(168*d)<<16|round(70*d));time.sleep(.55)
 assert state()['tipVisible'];shot('hover-tip.png')
 u.SetCursorPos(rect[0]-20,rect[1]-20);u.SendMessageW(sh,0x2a3,0,0);assert not state()['tipVisible']
 cmd('close-settings');cmd('settings')
 for _ in range(7):u.SendMessageW(sh,0x100,9,0)
 assert state()['tipVisible'],'Keyboard focus should explain icon'
 cmd('close-settings');assert not state()['tipVisible']
 k=C.windll.kernel32;k.GetPrivateProfileIntW.argtypes=[W.LPCWSTR,W.LPCWSTR,C.c_int,W.LPCWSTR]
 assert k.GetPrivateProfileIntW('timer','restSeconds',0,str(config))==4
 assert k.GetPrivateProfileIntW('timer','rest',0,str(config))==1
 cmd('settings');click(34,168);assert state()['restMode']==0 and state()['breath']==1
 assert state()['settingsRect']==initial
 for i,value in enumerate(['0','0','10']):
  click(50+i*86,58)
  for ch in value:u.SendMessageW(sh,0x102,ord(ch),0)
 click(252,168);s=state();assert s['running'] and 9000<s['remainingMs']<=10000 and not s['settingsVisible'] and not s['tipVisible']
 (out/'result.json').write_text(json.dumps(dict(pass_checks=['layout','toggles','mode_switch','transient_tip','hover_tip','keyboard_tip','duration_input_and_bounds','persistence','play'],final=s),indent=2),encoding='utf-8')
 print('PASS: compact layout, mode, tips, duration, persistence, play; targeted check only')

finally:
 if h:cmd('quit')
 p.wait(timeout=5)
