"""Passive counters for the final native binary; no working-set trimming."""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import json,time,statistics,hashlib,platform
import psutil
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'scratch/evidence'
u=C.windll.user32
u.FindWindowW.argtypes=[W.LPCWSTR,W.LPCWSTR];u.FindWindowW.restype=W.HWND
u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
class Copy(C.Structure):_fields_=[('tag',C.c_size_t),('size',W.DWORD),('data',C.c_void_p)]
h=u.FindWindowW('LightTimer.Native.Test','轻计时');assert h
def cmd(text):
 b=C.create_unicode_buffer(text);cd=Copy(0x4C54494D,C.sizeof(b),C.cast(b,C.c_void_p));assert u.SendMessageW(h,0x4a,0,C.addressof(cd))==1
def state():
 p=OUT/'profile-state.json';cmd('inspect '+str(p));return json.loads(p.read_text())
p=psutil.Process(state()['pid'])
def cpu():
 t=p.cpu_times();return t.user+t.system
def mem():
 m=p.memory_full_info();return dict(private_working_set=m.uss,working_set=m.rss,private_commit=m.private,handles=p.num_handles())
def p95(xs):return sorted(xs)[max(0,int(len(xs)*.95)-1)]
rows=[]
initial_state=state()
def sample(name,seconds):
 time.sleep(.4);before=state()
 expected_running=name!='paused_glass';expected_visible=name!='hidden_running'
 assert bool(before['running'])==expected_running and bool(before['visible'])==expected_visible and not before['controls'], ('Scenario changed before sampling',name,before)
 a=cpu();start=time.monotonic();data=[]
 for i in range(seconds):
  data.append(mem());time.sleep(max(0,start+i+1-time.monotonic()))
  if (i+1)%30==0:print(name,i+1,'seconds',flush=True)
 elapsed=time.monotonic()-start;used=cpu()-a;after=state()
 assert bool(after['running'])==expected_running and bool(after['visible'])==expected_visible and not after['controls'], ('Scenario changed during sampling',name,after)
 callbacks=after['wakeCount']-before['wakeCount']
 if name in ('paused_glass','hidden_running'):assert callbacks==0, ('Unexpected timer activity',name,callbacks)
 else:assert callbacks>=seconds*.95, ('Sampling interrupted by interaction',name,callbacks)
 row=dict(scenario=name,seconds=elapsed,cpu_seconds=used,single_core_percent=used/elapsed*100,paints=after['paintCount']-before['paintCount'],callbacks=after['wakeCount']-before['wakeCount'],p95={k:p95([d[k] for d in data]) for k in data[0]},peak={k:max(d[k] for d in data) for k in data[0]},first=data[0],last=data[-1],gdi_before=before['gdi'],gdi_after=after['gdi'],user_before=before['user'],user_after=after['user'])
 rows.append(row);save();print(json.dumps(row),flush=True)
def save():
 (OUT/'performance.json').write_text(json.dumps(dict(binary_sha256=hashlib.sha256((ROOT/'dist/LightTimer.exe').read_bytes()).hexdigest(),os=platform.platform(),logical_cpus=psutil.cpu_count(),initial_state=initial_state,samples=rows),indent=2),'utf-8')
cmd('sound 0');cmd('rest 0');cmd('close-settings');cmd('show');cmd('glass 1');cmd('move 24 940');u.SetThreadDpiAwarenessContext(C.c_void_p(-4));u.SetCursorPos(30,30);u.SendMessageW(h,0x100,27,0);u.SendMessageW(h,0x2a3,0,0);cmd('start 1500');save()
print('Warm up 60 seconds',flush=True)
for _ in range(6):time.sleep(10)
sample('visible_glass',60)
cmd('start 1500');cmd('toggle');sample('paused_glass',30)
cmd('start 1500');cmd('hide');sample('hidden_running',30)
cmd('show');cmd('glass 0');cmd('start 1500');sample('visible_solid',30)
cmd('glass 1');cmd('start 1500');sample('visible_glass_soak',600)
# Short effects: repeated runs, measure peaks and recovery; deliberately exclude waiting time.
baseline=mem(); effect=[]; c=cpu();started=time.monotonic()
for _ in range(3):
 cmd('rest 1 5');cmd('start 6')
 for i in range(115):effect.append(mem());time.sleep(.1)
elapsed=time.monotonic()-started;after=state()
rows.append(dict(scenario='breathing_and_rest_3_cycles',seconds=elapsed,cpu_seconds=cpu()-c,peak={k:max(d[k] for d in effect) for k in effect[0]},before=baseline,after=mem(),rest_active_after=after['restActive'],covers_after=after['covers']))
save();cmd('rest 0');cmd('start 1500');cmd('toggle');print('PROFILE COMPLETE',flush=True)
