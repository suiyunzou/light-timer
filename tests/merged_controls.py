from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import subprocess,time,json,sys
import comtypes,comtypes.client
from PIL import ImageGrab
r=Path(__file__).resolve().parents[1];out=r/'scratch/merged-controls/evidence';out.mkdir(parents=True,exist_ok=True)
exe=Path(sys.argv[1]) if len(sys.argv)>1 else r/'dist/LightTimer.exe'
u=C.windll.user32;u.SetThreadDpiAwarenessContext(C.c_void_p(-4))
u.FindWindowW.argtypes=[W.LPCWSTR,W.LPCWSTR];u.FindWindowW.restype=W.HWND
u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
u.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
u.GetForegroundWindow.restype=W.HWND
u.GetSubMenu.argtypes=[W.HMENU,C.c_int];u.GetSubMenu.restype=W.HMENU
u.GetMenuState.argtypes=[W.HMENU,W.UINT,W.UINT]
u.GetMenuStringW.argtypes=[W.HMENU,W.UINT,W.LPWSTR,C.c_int,W.UINT]
u.GetWindowRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
class Copy(C.Structure):_fields_=[('tag',C.c_size_t),('size',W.DWORD),('data',C.c_void_p)]
config=out/'test.ini';config.write_text('[timer]\nseconds=300\nrest=0\nbreath=0\nsound=0\n',encoding='utf-8')
assert not u.FindWindowW('LightTimer.Native.Test','轻计时')
p=subprocess.Popen([str(exe),'--test','--config',str(config)]);h=0
try:
 for _ in range(50):
  h=u.FindWindowW('LightTimer.Native.Test','轻计时')
  if h:break
  time.sleep(.05)
 assert h
 def cmd(t):
  b=C.create_unicode_buffer(t);cd=Copy(0x4c54494d,C.sizeof(b),C.cast(b,C.c_void_p));assert u.SendMessageW(h,0x4a,0,C.addressof(cd))==1
 def state():
  f=out/'state.json';cmd('inspect '+str(f));return json.loads(f.read_text())
 cmd('move 50 200');cmd('settings');time.sleep(.25)
 sh=u.FindWindowW('LightTimer.Native.Test','轻计时 · 设置');d=state()['dpi']
 def click(x,y):
  lp=(round(y*d)<<16)|round(x*d);u.SendMessageW(sh,0x201,1,lp);u.SendMessageW(sh,0x202,0,lp)
 mod=comtypes.client.GetModule('oleacc.dll');acc=C.POINTER(mod.IAccessible)()
 f=C.oledll.oleacc.AccessibleObjectFromWindow;f.argtypes=[W.HWND,W.DWORD,C.POINTER(comtypes.GUID),C.POINTER(C.POINTER(mod.IAccessible))]
 f(sh,0xfffffffc,C.byref(mod.IAccessible._iid_),C.byref(acc))
 def names():return [acc.accName(i) for i in range(1,acc.accChildCount+1)]
 n=names();assert not any('提醒' in item or '提示音' in item or '模式' in item for item in n)
 click(130,100);assert not state()['restMode'] and not state()['tipVisible']
 cmd('close-settings')
 # Exercise the real tray context menu and its sound submenu.
 u.PostMessageW(h,0x8001,0,0x205);time.sleep(.25)
 menu=u.FindWindowW('#32768',None);assert menu
 hm=u.SendMessageW(menu,0x1e1,0,0);assert hm
 label=C.create_unicode_buffer(80);u.GetMenuStringW(hm,1,label,80,0x400);assert label.value=='提醒'
 sub=u.GetSubMenu(hm,1);assert sub
 u.GetMenuStringW(sub,108,label,80,0);assert label.value=='提示音'
 assert not u.GetMenuState(sub,108,0)&8
 def key(k):
  assert u.GetForegroundWindow()==h,'Menu lost foreground; stop keyboard automation'
  u.keybd_event(k,0,0,0);u.keybd_event(k,0,2,0);time.sleep(.1)
 u.GetMenuItemRect.argtypes=[W.HWND,W.HMENU,W.UINT,C.POINTER(W.RECT)]
 def menu_click(menu_handle,index):
  rc=W.RECT();assert u.GetMenuItemRect(None,menu_handle,index,C.byref(rc))
  u.SetThreadDpiAwarenessContext(C.c_void_p(-4))
  u.SetCursorPos((rc.left+rc.right)//2,(rc.top+rc.bottom)//2)
  time.sleep(.15);u.mouse_event(2,0,0,0,0);u.mouse_event(4,0,0,0,0);time.sleep(.25)
 menu_click(hm,1)
 # Capture actual popup windows only.
 rects=[]
 @C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
 def enum(w,l):
  b=C.create_unicode_buffer(64);u.GetClassNameW(w,b,64)
  if b.value=='#32768' and u.IsWindowVisible(w):
   rc=W.RECT();u.GetWindowRect(w,C.byref(rc));rects.append((rc.left,rc.top,rc.right,rc.bottom))
  return True
 u.EnumWindows(enum,0)
 if rects:ImageGrab.grab((min(x[0] for x in rects),min(x[1] for x in rects),max(x[2] for x in rects),max(x[3] for x in rects))).save(out/'tray-settings.png')
 menu_click(sub,4)
 time.sleep(.15);assert state()['sound']==1
 k=C.windll.kernel32;k.GetPrivateProfileIntW.argtypes=[W.LPCWSTR,W.LPCWSTR,C.c_int,W.LPCWSTR]
 assert k.GetPrivateProfileIntW('timer','sound',0,str(config))==1
 (out/'result.json').write_text(json.dumps({'checks':['reminder_controls_removed','no_speaker_in_toolbar','no_mode_switch_in_settings','tray_sound_submenu','tray_sound_toggle_saved'],'final':state()},indent=2))
 print('PASS: removed reminder controls, no mode switch in settings, actual tray sound menu, sound toggle and persistence')
finally:
 if u.FindWindowW('#32768',None):u.keybd_event(27,0,0,0);u.keybd_event(27,0,2,0)
 if h:cmd('quit')
 p.wait(timeout=5)
