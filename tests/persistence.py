"""Check persistence and single-instance behavior in a temporary production config."""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import json,subprocess,time
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'scratch/evidence';exe=ROOT/'dist/LightTimer.exe'
u=C.windll.user32;u.FindWindowW.argtypes=[W.LPCWSTR,W.LPCWSTR];u.FindWindowW.restype=W.HWND
u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
assert not u.FindWindowW('LightTimer.Native','轻计时'),'A production instance already exists; leave it untouched'
class Copy(C.Structure):_fields_=[('tag',C.c_size_t),('size',W.DWORD),('data',C.c_void_p)]
cfg=OUT/'persistence-config.ini';cfg.write_text('[timer]\nseconds=73\nsound=0\nbreath=0\nrest=1\nrestSeconds=3\nx=1100\ny=180\n',encoding='ascii')
h=None

def cmd(text):
 b=C.create_unicode_buffer(text);cd=Copy(0x4C54494D,C.sizeof(b),C.cast(b,C.c_void_p));assert u.SendMessageW(h,0x4a,0,C.addressof(cd))==1

def state():
 p=OUT/'persistence-state.json';cmd('inspect '+str(p));return json.loads(p.read_text())

def launch():
 global h
 proc=subprocess.Popen([str(exe),'--config',str(cfg)])
 for _ in range(50):
  h=u.FindWindowW('LightTimer.Native','轻计时')
  if h:return proc
  time.sleep(.05)
 raise AssertionError('Window did not appear')

p=launch()
try:
 s=state();assert s['remainingMs']==73000 and not s['breath'] and s['restMode'] and s['restSeconds']==3
 second=subprocess.run([str(exe),'--config',str(cfg)],timeout=5);assert second.returncode==0 and state()['pid']==s['pid']
 cmd('start 91');cmd('toggle');cmd('quit');p.wait(timeout=5);h=None
 p=launch();s=state();assert not s['running'] and s['remainingMs']==91000 and not s['breath'] and s['restMode'] and s['restSeconds']==3
 (OUT/'persistence.json').write_text(json.dumps(dict(passed=['preferences load from config','second launch reuses process','duration survives restart','restart is paused, not accidentally counting'],final=s),indent=2),'utf-8')
 print('PASS persistence, isolated configuration, single instance and paused startup')
finally:
 if h and u.FindWindowW('LightTimer.Native','轻计时')==h:cmd('quit')
 p.wait(timeout=5)
