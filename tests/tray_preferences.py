from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import subprocess,time,json,wave,sys
from PIL import ImageGrab
r=Path(__file__).resolve().parents[1];out=r/'scratch/tray-preferences';out.mkdir(parents=True,exist_ok=True)
u=C.windll.user32;u.SetThreadDpiAwarenessContext(C.c_void_p(-4))
u.FindWindowW.argtypes=[W.LPCWSTR,W.LPCWSTR];u.FindWindowW.restype=W.HWND
u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
u.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
u.GetSubMenu.argtypes=[W.HMENU,C.c_int];u.GetSubMenu.restype=W.HMENU
u.GetMenuItemCount.argtypes=[W.HMENU]
u.GetMenuStringW.argtypes=[W.HMENU,W.UINT,W.LPWSTR,C.c_int,W.UINT]
u.GetMenuState.argtypes=[W.HMENU,W.UINT,W.UINT]
u.GetMenuItemRect.argtypes=[W.HWND,W.HMENU,W.UINT,C.POINTER(W.RECT)]
u.GetWindowLongPtrW.argtypes=[W.HWND,C.c_int];u.GetWindowLongPtrW.restype=C.c_ssize_t
u.GetWindowRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
class Copy(C.Structure):_fields_=[('tag',C.c_size_t),('size',W.DWORD),('data',C.c_void_p)]
song=out/'song.wav'
with wave.open(str(song),'wb') as f:f.setparams((1,2,22050,0,'NONE','not compressed'));f.writeframes(b'\0'*44100)
config=out/'check.ini';config.write_text('[timer]\nseconds=300\npersonal=0\npin=0\nglass=1\npersonalTheme=2\nrest=0\nsound=0\nmusic=1\nmusicFile='+str(song)+'\n',encoding='utf-16')
assert not u.FindWindowW('LightTimer.Native.Test','轻计时')
p=subprocess.Popen([str(Path(sys.argv[1]) if len(sys.argv)>1 else r/'dist/LightTimer.exe'),'--test','--config',str(config)]);h=0
try:
 for _ in range(60):
  h=u.FindWindowW('LightTimer.Native.Test','轻计时')
  if h:break
  time.sleep(.05)
 assert h
 def cmd(t):
  b=C.create_unicode_buffer(t);cd=Copy(0x4c54494d,C.sizeof(b),C.cast(b,C.c_void_p));assert u.SendMessageW(h,0x4a,0,C.addressof(cd))==1
 def state():
  f=out/'state.json';cmd('inspect '+str(f));return json.loads(f.read_text())
 def click(m,i):
  rc=W.RECT();assert u.GetMenuItemRect(None,m,i,C.byref(rc))
  u.SetCursorPos((rc.left+rc.right)//2,(rc.top+rc.bottom)//2);time.sleep(.08)
  u.mouse_event(2,0,0,0,0);u.mouse_event(4,0,0,0,0);time.sleep(.15)
 def menu():
  u.SetCursorPos(550,300);u.PostMessageW(h,0x8001,0,0x205);time.sleep(.15)
  popup=u.FindWindowW('#32768',None);assert popup
  m=u.SendMessageW(popup,0x1e1,0,0);assert u.GetMenuItemCount(m)==5
  labels=[]
  for i in range(5):
   b=C.create_unicode_buffer(80);u.GetMenuStringW(m,i,b,80,0x400);labels.append(b.value)
   sub=u.GetSubMenu(m,i)
   if sub:
    for j in range(u.GetMenuItemCount(sub)):assert not u.GetSubMenu(sub,j),'Third level submenu'
  assert labels[0].startswith('使用模式') and labels[1:]==['提醒','窗口外观','','退出']
  return m
 def dismiss():
  u.PostMessageW(h,0x1f,0,0);time.sleep(.15)
 def shot(name):
  rects=[]
  @C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
  def enum(w,l):
   b=C.create_unicode_buffer(64);u.GetClassNameW(w,b,64)
   if b.value=='#32768' and u.IsWindowVisible(w):
    rc=W.RECT();u.GetWindowRect(w,C.byref(rc));rects.append((rc.left,rc.top,rc.right,rc.bottom))
   return True
  u.EnumWindows(enum,0)
  assert rects
  ImageGrab.grab((min(a[0] for a in rects),min(a[1] for a in rects),max(a[2] for a in rects),max(a[3] for a in rects))).save(out/(name+'.png'))
 cmd('move 80 200')
 assert not state()['glassActive'] and u.GetWindowLongPtrW(h,-20)&8
 m=menu();assert u.GetMenuState(m,2,0x400)&3;shot('work-menu')
 sounds=u.GetSubMenu(m,1)
 for i in [109,110,111,112]:assert u.GetMenuState(sounds,i,0)&3
 assert not u.GetMenuState(sounds,108,0)&3
 click(m,1);shot('work-sounds');click(sounds,4);assert state()['sound']

 # Reminder choices, duration modal, cancel and saved bounds use the real menu.
 m=menu();sounds=u.GetSubMenu(m,1)
 assert u.GetMenuState(sounds,118,0)&8 and u.GetMenuState(sounds,120,0)&3
 click(m,1);click(sounds,1);assert state()['restMode'] and not state()['breath']
 u.GetDlgItem.argtypes=[W.HWND,C.c_int];u.GetDlgItem.restype=W.HWND
 for choice,accept in [(4,True),(0,False),(0,True),(2,True)]:
  before=state()['restSeconds'];m=menu();sounds=u.GetSubMenu(m,1)
  assert u.GetMenuState(sounds,119,0)&8 and not u.GetMenuState(sounds,120,0)&3
  click(m,1);shot('reminder-menu');click(sounds,2)
  dialog=u.FindWindowW('#32770','全屏提醒时长');assert dialog
  combo=u.GetDlgItem(dialog,1001);assert u.SendMessageW(combo,0x146,0,0)==5
  assert u.SendMessageW(combo,0x147,0,0)==before-1
  u.SendMessageW(combo,0x14e,choice,0)
  rc=W.RECT();u.GetWindowRect(dialog,C.byref(rc));ImageGrab.grab((rc.left,rc.top,rc.right,rc.bottom)).save(out/'duration-dialog.png')
  u.SendMessageW(dialog,0x111,1 if accept else 2,0);time.sleep(.1)
  assert state()['restSeconds']==(choice+1 if accept else before)
 assert 'restSeconds=3' in config.read_text(encoding='utf-16')
 cmd('start 1');time.sleep(1.2);assert state()['restActive']
 time.sleep(3);assert state()['completed'] and not state()['restActive'] and not state()['musicPlaying']
 m=menu();sounds=u.GetSubMenu(m,1);click(m,1);click(sounds,0)
 assert not state()['restMode'] and state()['breath']
 m=menu();modes=u.GetSubMenu(m,0);click(m,0);click(modes,1)
 assert state()['personal'] and not u.GetWindowLongPtrW(h,-20)&8
 saved=config.read_text(encoding='utf-16');assert 'pin=0' in saved and 'glass=1' in saved and 'personalTheme=2' in saved and str(song) in saved
 m=menu();assert not u.GetMenuState(m,2,0x400)&3;shot('personal-menu')
 appearance=u.GetSubMenu(m,2);assert u.GetMenuState(appearance,117,0)&8
 sounds=u.GetSubMenu(m,1)
 for i in [109,110,111,112]:assert not u.GetMenuState(sounds,i,0)&3
 click(m,2);shot('personal-appearance');click(appearance,4)
 assert 'personalTheme=0' in config.read_text(encoding='utf-16')
 m=menu();modes=u.GetSubMenu(m,0);click(m,0);click(modes,0)
 assert not state()['personal'] and not state()['glassActive'] and u.GetWindowLongPtrW(h,-20)&8
 cmd('start 300');before=state()['remainingMs']
 u.PostMessageW(h,0x8001,0,0x202);time.sleep(.15);assert not state()['visible'] and state()['running']
 u.PostMessageW(h,0x8001,0,0x202);time.sleep(.15);assert state()['visible'] and state()['running'] and state()['remainingMs']<before
 assert u.GetWindowLongPtrW(h,-20)&8
 print('PASS: four root actions, max two levels, work appearance/song locks, personal preferences restored, color selection, sound toggle, tray hide/restore preserves countdown and work topmost')
finally:
 if h:cmd('quit')
 p.wait(timeout=5)
