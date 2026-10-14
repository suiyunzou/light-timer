from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import comtypes,comtypes.client
import json,time
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'scratch/evidence'
mod=comtypes.client.GetModule('oleacc.dll')
u=C.windll.user32;u.FindWindowW.argtypes=[W.LPCWSTR,W.LPCWSTR];u.FindWindowW.restype=W.HWND
u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
h=u.FindWindowW('LightTimer.Native.Test','轻计时');assert h
class Copy(C.Structure):_fields_=[('tag',C.c_size_t),('size',W.DWORD),('data',C.c_void_p)]
def cmd(text):
 b=C.create_unicode_buffer(text);cd=Copy(0x4C54494D,C.sizeof(b),C.cast(b,C.c_void_p));u.SendMessageW(h,0x4a,0,C.addressof(cd))
cmd('settings');time.sleep(.1)
sh=u.FindWindowW('LightTimer.Native.Test','轻计时 · 设置')
ptr=C.POINTER(mod.IAccessible)()
f=C.oledll.oleacc.AccessibleObjectFromWindow;f.argtypes=[W.HWND,W.DWORD,C.POINTER(comtypes.GUID),C.POINTER(C.POINTER(mod.IAccessible))]
f(sh,0xfffffffc,C.byref(mod.IAccessible._iid_),C.byref(ptr))
count=ptr.accChildCount;names=[ptr.accName(i) for i in range(1,count+1)]
assert '小时' in names and '到时提示音' not in names and not any('提醒' in n for n in names)
hours=names.index('小时')+1
ptr.accValue[hours]='1';assert ptr.accValue(hours)=='1'
role=ptr.accRole(hours);assert role==52 # ROLE_SYSTEM_SPINBUTTON
(OUT/'accessibility.json').write_text(json.dumps(dict(children=names,editable_hours=ptr.accValue(hours)),ensure_ascii=False,indent=2),'utf-8')
cmd('rest 0');cmd('close-settings');print('PASS: MSAA child enumeration, names, roles, editable value, and no retired reminder controls')
