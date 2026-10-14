"""Real picker/menu and audio lifetime checks in an isolated native instance."""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import json, math, struct, subprocess, sys, time, wave
import comtypes, comtypes.client
from PIL import ImageGrab

r=Path(__file__).resolve().parents[1];out=r/'scratch/personal-mode/evidence';out.mkdir(parents=True,exist_ok=True)
exe=Path(sys.argv[1]) if len(sys.argv)>1 else r/'dist/LightTimer.exe'
song=out/'测试歌曲 夏日☀.wav'
with wave.open(str(song),'wb') as f:
 f.setparams((1,2,22050,0,'NONE','not compressed'))
 f.writeframes(b''.join(struct.pack('<h',int(800*math.sin(i*2*math.pi*220/22050)*min(1,i/2205,(22050*8-i)/2205))) for i in range(22050*8)))
mp3=out/'测试歌曲 夏日☀.mp3'
subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',str(song),str(mp3)],check=True)
u=C.windll.user32;u.SetThreadDpiAwarenessContext(C.c_void_p(-4))
u.FindWindowW.argtypes=[W.LPCWSTR,W.LPCWSTR];u.FindWindowW.restype=W.HWND
u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
u.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
u.GetSubMenu.argtypes=[W.HMENU,C.c_int];u.GetSubMenu.restype=W.HMENU
u.GetMenuItemRect.argtypes=[W.HWND,W.HMENU,W.UINT,C.POINTER(W.RECT)]
u.GetWindowRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
u.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
u.GetDlgCtrlID.argtypes=[W.HWND]
class Copy(C.Structure):_fields_=[('tag',C.c_size_t),('size',W.DWORD),('data',C.c_void_p)]
config=out/'test.ini';config.write_text('[timer]\nseconds=300\nrest=0\nrestSeconds=3\nsound=0\n',encoding='utf-8')
assert not u.FindWindowW('LightTimer.Native.Test','轻计时')
p=subprocess.Popen([str(exe),'--test','--config',str(config)]);h=0;checks=[]
oldpoint=W.POINT();u.GetCursorPos(C.byref(oldpoint))
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
 def wait_for(predicate,timeout=3):
  until=time.monotonic()+timeout
  while time.monotonic()<until:
   s=state()
   if predicate(s):return s
   time.sleep(.05)
  raise AssertionError(state())
 def settings():
  cmd('settings');time.sleep(.25)
  return u.FindWindowW('LightTimer.Native.Test','轻计时 · 设置')
 def click(w,x,y):
  d=state()['dpi'];lp=(round(y*d)<<16)|round(x*d)
  u.SendMessageW(w,0x201,1,lp);u.SendMessageW(w,0x202,0,lp)
 def shot(name,kind='settingsRect'):
  ImageGrab.grab(tuple(state()[kind])).save(out/name)
 def menu_click(m,index):
  rc=W.RECT();assert u.GetMenuItemRect(None,m,index,C.byref(rc))
  u.SetCursorPos((rc.left+rc.right)//2,(rc.top+rc.bottom)//2)
  time.sleep(.12);u.mouse_event(2,0,0,0,0);u.mouse_event(4,0,0,0,0);time.sleep(.18)
 def music_menu():
  u.SetCursorPos(450,350);u.PostMessageW(h,0x8001,0,0x205);time.sleep(.2)
  popup=u.FindWindowW('#32768',None);assert popup
  root=u.SendMessageW(popup,0x1e1,0,0)
  music=u.GetSubMenu(root,1);assert music
  menu_click(root,1)
  return music
 def mode(personal):
  u.SetCursorPos(450,350);u.PostMessageW(h,0x8001,0,0x205);time.sleep(.2)
  popup=u.FindWindowW('#32768',None);assert popup
  root=u.SendMessageW(popup,0x1e1,0,0);pref=u.GetSubMenu(root,0);assert pref
  u.GetMenuState.argtypes=[W.HMENU,W.UINT,W.UINT]
  assert bool(u.GetMenuState(pref,114,0)&8)==bool(state()['personal'])
  menu_click(root,0);menu_click(pref,1 if personal else 0)
  assert state()['personal']==personal
 def choose(path=None):
  menu_click(music_menu(),7)
  dialog=0
  for _ in range(50):
   dialog=u.FindWindowW('#32770','选择个人模式的歌曲')
   if dialog:break
   time.sleep(.05)
  assert dialog,'File picker did not open'
  pid=W.DWORD();u.GetWindowThreadProcessId(dialog,C.byref(pid));assert pid.value==p.pid
  if path is None:
   u.PostMessageW(dialog,0x111,2,0);time.sleep(.2);return
  edits=[]
  @C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
  def child(w,l):
   b=C.create_unicode_buffer(128);u.GetClassNameW(w,b,128)
   if b.value=='Edit':edits.append(w)
   return True
  u.EnumChildWindows(dialog,child,0)
  assert edits,'No filename edit'
  # The Explorer-style common dialog's filename edit has ID 1148 or 1001.
  edit=next((e for e in edits if u.GetDlgCtrlID(e) in (1148,1001)),edits[-1])
  b=C.create_unicode_buffer(str(path));u.SendMessageW(edit,0xc,0,C.addressof(b))
  u.PostMessageW(dialog,0x111,1,0)
  for _ in range(60):
   if not u.IsWindow(dialog):break
   time.sleep(.05)
  assert not u.IsWindow(dialog),'Picker did not accept path'
  time.sleep(.15)

 cmd('move 80 200');sh=settings()
 mod=comtypes.client.GetModule('oleacc.dll');acc=C.POINTER(mod.IAccessible)()
 f=C.oledll.oleacc.AccessibleObjectFromWindow;f.argtypes=[W.HWND,W.DWORD,C.POINTER(comtypes.GUID),C.POINTER(C.POINTER(mod.IAccessible))]
 f(sh,0xfffffffc,C.byref(mod.IAccessible._iid_),C.byref(acc))
 names=lambda:[acc.accName(i) for i in range(1,acc.accChildCount+1)]
 assert not state()['personal'] and not state()['hasSong']
 assert not any('模式' in n for n in names()), 'Mode controls must not remain in the settings window'
 click(sh,215,22);assert not state()['personal'], 'Former mode switch must be inert'
 assert not any('音乐' in n or '歌曲' in n for n in names() if '模式' not in n)
 assert not any('提醒' in n for n in names())
 click(sh,130,100);assert not state()['restMode'] and not state()['tipVisible']
 checks.append('reminder_controls_removed_from_main_window')
 mode(True);sh=settings();shot('personal-settings.png')
 shot('personal-timer.png','rect')
 choose();assert not state()['hasSong']
 choose(song);assert state()['hasSong'] and state()['music'] and not state()['musicPlaying']
 assert str(song) in config.read_text(encoding='utf-16')
 checks.append('actual_tray_picker_cancel_unicode_path_saved')
 # Work mode never plays selected music, even at full-screen completion.
 mode(False);sh=settings();shot('work-settings.png');shot('work-timer.png','rect');menu_click(music_menu(),1)
 cmd('start 1');wait_for(lambda s:s['restActive'])
 assert not state()['musicPlaying'];wait_for(lambda s:not s['restActive'],4)
 assert state()['completed'] and not state()['musicPlaying']
 checks.append('work_fullscreen_finishes_without_music')
 # Personal floating mode starts music and does not truncate it after five seconds.
 mode(True);sh=settings();menu_click(music_menu(),0);cmd('close-settings');cmd('start 1')
 s=wait_for(lambda s:s['musicPlaying']);assert not s['restActive'] and s['audioLengthMs']>=7900
 time.sleep(5.5);s=state();assert s['musicPlaying'] and s['audioPositionMs']>5000
 shot('floating-stop.png','rect');wait_for(lambda s:not s['musicPlaying'],4)
 checks.append('personal_floating_plays_past_five_seconds_and_ends')
 # MP3 follows the same actual picker and survives the full-screen reminder.
 choose(mp3);sh=settings();menu_click(music_menu(),1);cmd('close-settings');cmd('start 1')
 wait_for(lambda s:s['restActive'] and s['musicPlaying'])
 time.sleep(.3)
 screen=ImageGrab.grab();cx,cy=screen.width//2,screen.height//2
 screen.crop((cx-160,cy-160,cx+160,cy+160)).save(out/'fullscreen-playing.png')
 wait_for(lambda s:not s['restActive'],4);assert state()['musicPlaying']
 before=state()['audioPositionMs'];assert before>=2500
 # Direct stop remains visible without hovering the completed timer.
 cmd('show');time.sleep(.1);s=state();width=(s['rect'][2]-s['rect'][0])/s['dpi']
 click(h,width-18,18);assert not state()['musicPlaying'] and state()['completed']
 checks.append('personal_fullscreen_exits_mp3_continues_visible_stop_works')
 # Preview plays fully, closing settings keeps it playing; work switch stops it.
 sh=settings();menu_click(music_menu(),8);wait_for(lambda s:s['musicPlaying'])
 cmd('close-settings');assert state()['musicPlaying']
 mode(False);assert not state()['personal'] and not state()['musicPlaying']
 mode(True);assert state()['personal'] and not state()['musicPlaying']
 cmd('close-settings');cmd('quit');p.wait(timeout=5);h=0
 p=subprocess.Popen([str(exe),'--test','--config',str(config)])
 for _ in range(60):
  h=u.FindWindowW('LightTimer.Native.Test','轻计时')
  if h:break
  time.sleep(.05)
 assert state()['personal'] and state()['hasSong'] and not state()['musicPlaying']
 assert str(mp3) in config.read_text(encoding='utf-16')
 checks.append('preview_work_switch_restart_persistence')
 # Closing the full-screen reminder early must leave audio playing.
 cmd('start 1');wait_for(lambda s:s['restActive'] and s['musicPlaying'])
 u.SendMessageW(h,0x312,1,0);assert not state()['restActive'] and state()['musicPlaying']
 menu_click(music_menu(),8);assert not state()['musicPlaying']
 checks.append('escape_reminder_keeps_song_tray_stop_works')
 # A missing file cannot trap the timer or block the reminder with a modal dialog.
 unavailable=mp3.with_suffix('.unavailable');mp3.rename(unavailable)
 try:
  cmd('reduced 1');cmd('start 1');s=wait_for(lambda s:s['restActive'])
  assert s['audioFailed'] and not s['musicPlaying'] and s['reducedMotion']
  wait_for(lambda s:not s['restActive'],4);assert state()['completed']
 finally:unavailable.rename(mp3)
 checks.append('missing_song_finishes_with_reduced_motion')
 (out/'result.json').write_text(json.dumps({'checks':checks,'final':state()},indent=2))
 print('PASS:',', '.join(checks))
finally:
 if u.FindWindowW('#32768',None):u.keybd_event(27,0,0,0);u.keybd_event(27,0,2,0)
 dialog=u.FindWindowW('#32770','选择个人模式的歌曲')
 if dialog:u.PostMessageW(dialog,0x111,2,0);time.sleep(.2)
 if h:cmd('quit')
 p.wait(timeout=5);u.SetCursorPos(oldpoint.x,oldpoint.y)

