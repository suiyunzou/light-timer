#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <psapi.h>
#include <oleacc.h>
#include <vector>
#include <memory>
#include <sstream>
#include <fstream>
#include <algorithm>
#include "engine.hpp"
#include "paint.hpp"
#include "file_audio.hpp"

static uint64_t nowMs() {
  return GetTickCount64();
}
static UINT dpiFor(HWND h) {
  using F = UINT(WINAPI *)(HWND);
  static auto f = (F)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
  return f ? f(h) : 96;
}
static void dpiAware() {
  using F = BOOL(WINAPI *)(HANDLE);
  auto f = (F)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
  if (f)
    f((HANDLE)-4);
}
static constexpr int TIMER_SHORT_WIDTH = 116;
static constexpr int TIMER_LONG_WIDTH = 152;
static constexpr int TIMER_HEIGHT = 36;
static constexpr DWORD CONTROLS_HOVER_MS = 400;
static constexpr int PRESET_MINUTES[] = {5, 15, 25, 45};
static const UINT TRAY = WM_APP + 1;
static const ULONG_PTR PROTOCOL = 0x4C54494D;
struct Hit {
  int id;
  RECT r;
  std::wstring name;
};
struct Win {
  HWND h = nullptr;
  int kind = 0;
  float dpi = 1;
  Surface surface;
  std::vector<Hit> hits;
  int focus = 0;
  bool glass = false;
};
struct Settings {
  bool breath = true, sound = true, rest = false, pin = true, glass = true, music = false;
  int seconds = 1500, restSeconds = 4, x = INT_MIN, y = INT_MIN;
  std::wstring path, musicFile;
  void load() {
    seconds =
        std::clamp((int)GetPrivateProfileIntW(L"timer", L"seconds", 1500, path.c_str()), 1, 359999);
    restSeconds =
        std::clamp((int)GetPrivateProfileIntW(L"timer", L"restSeconds", 4, path.c_str()), 1, 5);
    breath = GetPrivateProfileIntW(L"timer", L"breath", 1, path.c_str());
    sound = GetPrivateProfileIntW(L"timer", L"sound", 1, path.c_str());
    music = GetPrivateProfileIntW(L"timer", L"music", 0, path.c_str());
    wchar_t file[32768]{};
    GetPrivateProfileStringW(L"timer", L"musicFile", L"", file, 32768, path.c_str());
    musicFile = file;
    if (musicFile.empty()) music = false;
    rest = GetPrivateProfileIntW(L"timer", L"rest", 0, path.c_str());
    breath = !rest; // One mode control: breathing reminder or full-screen rest.
    pin = GetPrivateProfileIntW(L"timer", L"pin", 1, path.c_str());
    glass = GetPrivateProfileIntW(L"timer", L"glass", 1, path.c_str());
    x = (int)GetPrivateProfileIntW(L"timer", L"x", INT_MIN, path.c_str());
    y = (int)GetPrivateProfileIntW(L"timer", L"y", INT_MIN, path.c_str());
  }
  void save() {
    std::wstring section;
    for (auto p : std::vector<std::pair<const wchar_t *, int>>{{L"seconds", seconds},
                                                               {L"restSeconds", restSeconds},
                                                               {L"breath", breath},
                                                               {L"sound", sound},
                                                               {L"music", music},
                                                               {L"rest", rest},
                                                               {L"pin", pin},
                                                               {L"glass", glass},
                                                               {L"x", x},
                                                               {L"y", y}}) {
      section += std::wstring(p.first) + L"=" + std::to_wstring(p.second);
      section.push_back(L'\0');
    }
    section += L"musicFile=" + musicFile;
    section.push_back(L'\0');
    section.push_back(L'\0');
    WritePrivateProfileSectionW(L"timer", section.c_str(), path.c_str());
  }
};
class App;
static App *app = nullptr;
class Accessible : public IAccessible {
  LONG refs = 1;
  HWND hwnd;

public:
  explicit Accessible(HWND h) : hwnd(h) {}
  virtual ~Accessible() = default;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **p) override {
    if (!p)
      return E_POINTER;
    *p = nullptr;
    if (id == IID_IUnknown || id == IID_IDispatch || id == IID_IAccessible) {
      *p = static_cast<IAccessible *>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    return InterlockedIncrement(&refs);
  }
  ULONG STDMETHODCALLTYPE Release() override {
    auto r = InterlockedDecrement(&refs);
    if (!r)
      delete this;
    return r;
  }
  HRESULT info(ITypeInfo **p) {
    ITypeLib *lib = nullptr;
    HRESULT hr = LoadTypeLibEx(L"oleacc.dll", REGKIND_NONE, &lib);
    if (FAILED(hr))
      return hr;
    hr = lib->GetTypeInfoOfGuid(IID_IAccessible, p);
    lib->Release();
    return hr;
  }
  HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *p) override {
    if (p)
      *p = 1;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT index, LCID, ITypeInfo **p) override {
    return index ? DISP_E_BADINDEX : info(p);
  }
  HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR *names, UINT count, LCID,
                                          DISPID *ids) override {
    ITypeInfo *t = nullptr;
    HRESULT hr = info(&t);
    if (FAILED(hr))
      return hr;
    hr = DispGetIDsOfNames(t, names, count, ids);
    t->Release();
    return hr;
  }
  HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID, LCID, WORD flags, DISPPARAMS *args,
                                   VARIANT *result, EXCEPINFO *ex, UINT *error) override {
    ITypeInfo *t = nullptr;
    HRESULT hr = info(&t);
    if (FAILED(hr))
      return hr;
    hr = DispInvoke(this, t, id, flags, args, result, ex, error);
    t->Release();
    return hr;
  }

  HRESULT STDMETHODCALLTYPE get_accParent(IDispatch **p) override {
    *p = nullptr;
    return S_FALSE;
  }
  HRESULT STDMETHODCALLTYPE get_accChildCount(long *) override;
  HRESULT STDMETHODCALLTYPE get_accChild(VARIANT, IDispatch **p) override {
    *p = nullptr;
    return S_FALSE;
  }
  HRESULT STDMETHODCALLTYPE get_accName(VARIANT, BSTR *) override;
  HRESULT STDMETHODCALLTYPE get_accValue(VARIANT, BSTR *) override;
  HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT, BSTR *p) override {
    *p = nullptr;
    return S_FALSE;
  }
  HRESULT STDMETHODCALLTYPE get_accRole(VARIANT, VARIANT *) override;
  HRESULT STDMETHODCALLTYPE get_accState(VARIANT, VARIANT *) override;
  HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT, BSTR *p) override {
    *p = nullptr;
    return S_FALSE;
  }
  HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR *p, VARIANT, long *) override {
    *p = nullptr;
    return S_FALSE;
  }
  HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT, BSTR *p) override {
    *p = nullptr;
    return S_FALSE;
  }
  HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT *) override;
  HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT *p) override {
    VariantInit(p);
    return S_FALSE;
  }
  HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT, BSTR *p) override {
    *p = SysAllocString(L"执行");
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE accSelect(long, VARIANT) override;
  HRESULT STDMETHODCALLTYPE accLocation(long *, long *, long *, long *, VARIANT) override;
  HRESULT STDMETHODCALLTYPE accNavigate(long, VARIANT, VARIANT *p) override {
    VariantInit(p);
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE accHitTest(long, long, VARIANT *) override;
  HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT) override;
  HRESULT STDMETHODCALLTYPE put_accName(VARIANT, BSTR) override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE put_accValue(VARIANT, BSTR) override;
};
class App {
public:
  HINSTANCE instance;
  Win timer, settings, tip;
  int settingsHover = 0, pressedId = 0;
  bool hoverArmed = false, timerPointerDown = false;
  bool completionEmphasis = false;
  int timerPressedId = 0;
  POINT timerPressPoint{};
  std::wstring tipTitle, tipDetail;
  Settings cfg;
  Countdown clock;
  std::vector<std::unique_ptr<Win>> covers, orbs;
  bool dark = true, hover = false, keyMode = false, dragging = false, reduced = false,
       highContrast = false, test = false, editing = false, settingError = false,
       settingsDirty = false;
  int fields[3]{0, 25, 0};
  uint64_t restEnd = 0, paints = 0, wakes = 0, completions = 0, lastLatency = 0;
  HWND previous = nullptr;
  UINT taskbarMessage;
  std::wstring className;
  std::vector<BYTE> soundData;
  FileAudio fileAudio;
  bool musicPlaying = false, musicPreview = false, audioFailed = false;
  uint64_t settingsOpened = 0, musicChanged = 0, restStarted = 0;
  float musicFrom = 0;
  HICON icon = nullptr, largeIcon = nullptr;
  UINT scheduled = 0;
  bool transparentAllowed = true;
  App(HINSTANCE i, bool t) : instance(i), test(t) {
    timer.kind = 0;
    settings.kind = 1;
    tip.kind = 4;
    className = test ? L"LightTimer.Native.Test" : L"LightTimer.Native";
    taskbarMessage = RegisterWindowMessageW(L"TaskbarCreated");
  }
  ~App() {
    if (icon)
      DestroyIcon(icon);
    if (largeIcon)
      DestroyIcon(largeIcon);
  }
  static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return app ? app->event(h, m, w, l) : DefWindowProcW(h, m, w, l);
  }
  Win *window(HWND h) {
    if (h == timer.h)
      return &timer;
    if (h == settings.h)
      return &settings;
    if (h == tip.h)
      return &tip;
    for (auto &v : orbs)
      if (v->h == h)
        return v.get();
    for (auto &v : covers)
      if (v->h == h)
        return v.get();
    return nullptr;
  }
  void theme() {
    DWORD n = sizeof(DWORD), v = 0;
    RegGetValueW(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &n);
    dark = !v;
    n = 4;
    v = 1;
    RegGetValueW(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &v, &n);
    transparentAllowed = v != 0;
    HIGHCONTRASTW hc{sizeof(hc)};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0);
    highContrast = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
    BOOL animation = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0);
    reduced = !animation;
  }
  bool backdrop(HWND h, bool enable) {
    enable = enable && transparentAllowed && !highContrast;
    int type = enable ? 3 : 1;
    if (SUCCEEDED(DwmSetWindowAttribute(h, 38, &type, sizeof(type))))
      return enable;
    // Win10 compatibility path; dynamically loaded, with a fully opaque fallback.
    struct Accent {
      int state, flags;
      DWORD color;
      int animation;
    } accent{enable ? 3 : 0, 0, 0, 0};
    struct Data {
      int attr;
      void *data;
      SIZE_T size;
    } data{19, &accent, sizeof(accent)};
    using F = BOOL(WINAPI *)(HWND, Data *);
    auto f = (F)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
    return f && f(h, &data) && enable;
  }
  bool restBackdrop(HWND h) {
    if (!cfg.glass || !transparentAllowed || highContrast)
      return false;
    struct Accent {
      int state, flags;
      DWORD color;
      int animation;
    } accent{4, 2, dark ? 0xF21B1B1Bu : 0xF2FAFAFAu, 0};
    struct Data {
      int attr;
      void *data;
      SIZE_T size;
    } data{19, &accent, sizeof(accent)};
    using F = BOOL(WINAPI *)(HWND, Data *);
    auto f = (F)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
    return f && f(h, &data);
  }
  HWND make(Win &w, int width, int height, HWND owner = nullptr) {
    DWORD ex = WS_EX_TOOLWINDOW | WS_EX_LAYERED;
    HWND h = CreateWindowExW(ex, className.c_str(),
                             w.kind == 4   ? L"轻计时 · 提示"
                             : w.kind == 1   ? L"轻计时 · 设置"
                             : w.kind == 0 ? L"轻计时"
                                           : L"轻计时 · 休息",
                             WS_POPUP, 0, 0, width, height, owner, nullptr, instance, nullptr);
    w.h = h;
    w.dpi = dpiFor(h) / 96.f;
    return h;
  }
  void shape(Win &w, int width, int height) {
    if (w.kind == 2)
      return;
    int r = (int)(12 * w.dpi);
    SetWindowRgn(w.h, CreateRoundRectRgn(0, 0, width + 1, height + 1, r * 2, r * 2), TRUE);
  }
  void clamp(HWND h) {
    RECT r;
    GetWindowRect(h, &r);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi);
    int x = std::clamp((int)r.left, (int)mi.rcWork.left,
                       std::max((int)mi.rcWork.left, (int)(mi.rcWork.right - (r.right - r.left))));
    int y = std::clamp((int)r.top, (int)mi.rcWork.top,
                       std::max((int)mi.rcWork.top, (int)(mi.rcWork.bottom - (r.bottom - r.top))));
    SetWindowPos(h, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  void resizeTimer() {
    int width = (clock.duration >= 3600000 ? TIMER_LONG_WIDTH : TIMER_SHORT_WIDTH) * timer.dpi,
        height = TIMER_HEIGHT * timer.dpi;
    SetWindowPos(timer.h, cfg.pin ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, width, height,
                 SWP_NOMOVE | SWP_NOACTIVATE);
    shape(timer, width, height);
    clamp(timer.h);
  }
  void setupIcon() {
    if (!icon)
      icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                              GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    if (!largeIcon)
      largeIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    for (HWND h : {timer.h, settings.h}) {
      if (h) {
        SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)icon);
        SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)largeIcon);
      }
    }
  }
  void tray(bool remove = false) {
    NOTIFYICONDATAW n{sizeof(n)};
    n.hWnd = timer.h;
    n.uID = 1;
    n.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    n.hIcon = icon;
    n.uCallbackMessage = TRAY;
    wcscpy_s(n.szTip, L"轻计时");
    Shell_NotifyIconW(remove ? NIM_DELETE : NIM_ADD, &n);
  }
  bool init() {
    theme();
    setupIcon();
    if (!icon || !largeIcon)
      return false;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = largeIcon;
    wc.hIconSm = icon;
    wc.lpszClassName = className.c_str();
    if (!RegisterClassExW(&wc))
      return false;
    cfg.load();
    clock.duration = clock.frozen = (uint64_t)cfg.seconds * 1000;
    fields[0] = cfg.seconds / 3600;
    fields[1] = cfg.seconds / 60 % 60;
    fields[2] = cfg.seconds % 60;
    if (!make(timer, TIMER_SHORT_WIDTH, TIMER_HEIGHT))
      return false;
    make(settings, 288, 204, timer.h);
    timer.glass = backdrop(timer.h, cfg.glass);
    settings.glass = backdrop(settings.h, cfg.glass);
    resizeTimer();
    POINT pos{};
    GetCursorPos(&pos);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(pos, MONITOR_DEFAULTTONEAREST), &mi);
    int x = cfg.x == INT_MIN ? mi.rcWork.right - int((TIMER_SHORT_WIDTH + 24) * timer.dpi) : cfg.x,
        y = cfg.y == INT_MIN ? mi.rcWork.top + int(36 * timer.dpi) : cfg.y;
    SetWindowPos(timer.h, cfg.pin ? HWND_TOPMOST : HWND_NOTOPMOST, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
    clamp(timer.h);
    setupIcon();
    tray();
    ShowWindow(timer.h, SW_SHOWNOACTIVATE);
    draw(timer);
    return true;
  }
  bool controls() const {
    return !dragging && (hover || (keyMode && GetForegroundWindow() == timer.h));
  }
  void cancelHover() {
    hoverArmed = false;
    TRACKMOUSEEVENT t{sizeof(t), TME_CANCEL | TME_HOVER, timer.h, 0};
    TrackMouseEvent(&t);
  }
  void armHover() {
    if (hover || hoverArmed || dragging || timerPointerDown || !IsWindowVisible(timer.h) ||
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ||
        (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ||
        (GetAsyncKeyState(VK_MBUTTON) & 0x8000))
      return;
    POINT p;
    GetCursorPos(&p);
    if (WindowFromPoint(p) != timer.h)
      return;
    // Windows restarts the dwell when the pointer leaves its hover tolerance rectangle.
    TRACKMOUSEEVENT t{sizeof(t), TME_HOVER | TME_LEAVE, timer.h, CONTROLS_HOVER_MS};
    hoverArmed = TrackMouseEvent(&t) != 0;
  }
  bool pastDragThreshold(POINT p) const {
    using F = int(WINAPI *)(int, UINT);
    static auto metrics = (F)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi");
    int dx = metrics ? metrics(SM_CXDRAG, dpiFor(timer.h)) : GetSystemMetrics(SM_CXDRAG);
    int dy = metrics ? metrics(SM_CYDRAG, dpiFor(timer.h)) : GetSystemMetrics(SM_CYDRAG);
    return abs(p.x - timerPressPoint.x) >= dx || abs(p.y - timerPressPoint.y) >= dy;
  }
  void cancelTimerPointer() {
    cancelHover();
    timerPointerDown = false;
    timerPressedId = 0;
    hover = keyMode = false;
    if (GetCapture() == timer.h)
      ReleaseCapture();
  }
  bool selected(int id) const {
    if (id >= 40 && id <= 43)
      return fields[0] * 3600 + fields[1] * 60 + fields[2] == PRESET_MINUTES[id - 40] * 60;
    return id == 20               ? cfg.breath
           : id == 21             ? cfg.sound
           : id == 23             ? cfg.music
                                  : false;
  }
  std::wstring display() {
    if (clock.completed)
      return L"00:00";
    auto secs = (clock.remaining(nowMs()) + 999) / 1000;
    wchar_t s[32];
    if (clock.duration >= 3600000)
      swprintf(s, 32, L"%02llu:%02llu:%02llu", secs / 3600, secs / 60 % 60, secs % 60);
    else
      swprintf(s, 32, L"%02llu:%02llu", secs / 60, secs % 60);
    return s;
  }
  void hit(Win &w, int id, int x, int y, int ww, int hh, const wchar_t *name) {
    w.hits.push_back({id, {x, y, x + ww, y + hh}, name});
  }
  Hit *byId(Win &w, int id) {
    for (auto &h : w.hits)
      if (h.id == id)
        return &h;
    return nullptr;
  }
  Hit *at(Win &w, int x, int y) {
    POINT p{(LONG)(x / w.dpi), (LONG)(y / w.dpi)};
    for (auto &h : w.hits)
      if (PtInRect(&h.r, p))
        return &h;
    return nullptr;
  }
  void focusBorder(Paint &p, Win &w) {
    if (!keyMode || !w.focus || (w.kind == 1 && ((w.focus >= 10 && w.focus <= 12) || w.focus == 30)))
      return;
    auto h = byId(w, w.focus);
    if (h)
      p.rounded(h->r.left - 2, h->r.top - 2, h->r.right - h->r.left + 4, h->r.bottom - h->r.top + 4,
                5, p.accent, true);
  }
  float musicPosition() const {
    if (reduced || !musicChanged)
      return cfg.music ? 1.f : 0.f;
    float t = std::min(1.f, float(nowMs() - musicChanged) / 180.f);
    t = t * t * (3 - 2 * t);
    return musicFrom + ((cfg.music ? 1.f : 0.f) - musicFrom) * t;
  }
  void draw(Win &w) {
    if (!w.h || !IsWindowVisible(w.h))
      return;
    RECT r;
    GetClientRect(w.h, &r);
    if (!w.surface.prepare(r.right, r.bottom))
      return;
    w.hits.clear();
    {
      Paint p(w.surface, w.dpi, dark);
      if (highContrast) {
        auto systemColor = [](int index) {
          auto c = GetSysColor(index);
          return Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
        };
        p.ink = p.muted = systemColor(COLOR_WINDOWTEXT);
        p.bg = systemColor(COLOR_WINDOW);
        p.accent = systemColor(COLOR_HIGHLIGHT);
        p.onAccent = systemColor(COLOR_HIGHLIGHTTEXT);
        p.track = systemColor(COLOR_BTNFACE);
      }
      float width = r.right / w.dpi, height = r.bottom / w.dpi;
      if (w.kind < 2 || w.kind == 4) {
        auto fill = p.bg;
        if (!w.glass || highContrast)
          fill = Color(255, p.bg.GetR(), p.bg.GetG(), p.bg.GetB());
        p.rounded(.5f, .5f, width - 1, height - 1, 9, fill);
        p.rounded(.5f, .5f, width - 1, height - 1, 9, p.edge, true);
      }
      if (w.kind == 0) {
        if (controls()) {
          int x = (int)width / 2;
          hit(w, 1, x - 46, 2, 32, 32, L"设置倒计时");
          hit(w, 2, x + 14, 2, 32, 32,
              clock.completed ? L"重新开始"
              : clock.running ? L"暂停"
                              : L"继续");
          p.icon(1, x - 30, height / 2, p.ink);
          p.icon(clock.completed ? 10 : clock.running ? 2 : 3, x + 30, height / 2, p.ink);
        } else {
          float size = !clock.completed && clock.duration >= 3600000 ? 28 : 32;
          if (clock.completed && completionEmphasis && !reduced)
            size *= 1.04f;
          auto left = clock.remaining(nowMs());
          if (cfg.breath && !reduced && !dragging && clock.running && left <= 5000) {
            float wave = (1 - cosf(float(left % 1000) / 1000 * 6.283185f)) * .5f;
            size *= 1 + .065f * wave;
          }
          p.text(display(), 0, 0, width, height, size, p.ink, true);
        }
        focusBorder(p, w);
      } else if (w.kind == 1) {
        p.text(L"倒计时", 18, 10, 200, 24, 13, p.ink, false, 0);
        hit(w, 9, 249, 8, 28, 28, L"关闭设置");
        p.icon(9, 263, 22, p.muted);
        const wchar_t *labels[] = {L"时", L"分", L"秒"};
        for (int i = 0; i < 3; i++) {
          int x = 18 + i * 86;
          wchar_t num[8];
          swprintf(num, 8, L"%02d", fields[i]);
          bool active = w.focus == 10 + i && GetFocus() == w.h;
          if (active) {
            p.rounded(x, 42, 76, 34, 4, p.track);
            p.rounded(x, 42, 76, 34, 4, p.accent, true);
            if (!editing)
              p.rounded(x + 21, 46, 34, 26, 2, p.accent);
          }
          p.text(num, x, 40, 76, 36, 25, active && !editing ? p.onAccent : p.ink, true);
          // A static caret gives input feedback without a permanent blink timer.
          if (active && editing)
            p.line(x + 55, 48, x + 55, 70, p.accent, 1);
          p.text(labels[i], x, 77, 76, 16, 10, p.muted);
          p.line(x, 76, x + 76, 76, p.track, 1);
          hit(w, 10 + i, x, 40, 76, 53, i == 0 ? L"小时" : i == 1 ? L"分钟" : L"秒");
        }
        for (int i = 0; i < 4; i++) {
          int x = 18 + i * 65;
          bool on = selected(40 + i), down = pressedId == 40 + i;
          float inset = down ? 1.f : 0.f;
          Color fill = down ? p.accent
                            : on ? (highContrast ? p.accent : rgb(dark ? 0xFFFFFF : 0x000000, 45))
                                 : p.track;
          p.rounded(x + inset, 103 + inset, 57 - 2 * inset, 24 - 2 * inset, 5, fill);
          if (on || down)
            p.rounded(x + inset, 103 + inset, 57 - 2 * inset, 24 - 2 * inset, 5, p.ink, true);
          p.text(std::to_wstring(PRESET_MINUTES[i]) + L"分", x, 103 + inset, 57, 24, 11,
                 down || (on && highContrast) ? p.onAccent : p.ink);
          hit(w, 40 + i, x, 103, 57, 24, (std::to_wstring(PRESET_MINUTES[i]) + L"分钟").c_str());
        }
        p.line(18, 140, 270, 140, p.track, 1);
        p.rounded(18, 154, 66, 28, 7,
                  pressedId == 22 ? rgb(dark ? 0xFFFFFF : 0x000000, 60) : p.track);
        p.icon(cfg.rest ? 13 : 12, 32, 168, p.ink);
        p.text(cfg.rest ? L"全屏" : L"浮窗", 44, 154, 28, 28, 11, p.ink);
        p.line(75, 165, 78, 168, p.muted, 1.2f);
        p.line(78, 168, 75, 171, p.muted, 1.2f);
        hit(w, 22, 18, 150, 66, 36, cfg.rest ? L"全屏休息，点击切换到浮窗提醒" : L"浮窗提醒，点击切换到全屏休息");
        {
          int x = 106, id = 23;
          bool on = cfg.music || musicPlaying;
          if (on || settingsHover == id || pressedId == id)
            p.rounded(x - 14, 154, 28, 28, 7,
                      pressedId == id ? rgb(dark ? 0xFFFFFF : 0x000000, 60) : p.track);
          auto state = p.g.Save();
          p.g.TranslateTransform((float)x, 168);
          float scale = .8f;
          if (id == 23 && !reduced)
            scale += .08f * sinf(musicPosition() * 3.141593f);
          p.g.ScaleTransform(scale, scale);
          if (musicPlaying) {
            p.rounded(-5, -7, 10, 10, 1, p.ink);
            float phase = reduced ? 0.f : float(nowMs() % 2000) / 2000 * 6.283185f;
            for (int bar = 0; bar < 3; ++bar) {
              float amplitude = reduced ? 2.f : 1 + 3 * (.5f + .5f * sinf(phase * 2 + bar));
              p.line(-6.f + bar * 6, 10-amplitude, -6.f + bar * 6, 10, p.ink, 2);
            }
          } else
            p.icon(11, 0, 0, on ? p.ink : p.muted);
          p.g.Restore(state);
          hit(w, id, 90, 150, 30, 36, musicPlaying ? L"停止音乐" : cfg.musicFile.empty() ? L"选择并播放本地歌曲" : L"播放歌曲");
        }
        p.line(125, 166, 128, 169, p.muted, 1.3f);
        p.line(128, 169, 131, 166, p.muted, 1.3f);
        hit(w, 24, 120, 150, 20, 36, L"更换本地歌曲");
        if (cfg.rest) {
          bool active = w.focus == 30 && GetFocus() == w.h;
          p.rounded(145, 154, 66, 28, 5, p.track);
          if (active) {
            p.rounded(145, 154, 66, 28, 5, p.accent, true);
            p.rounded(150, 158, 18, 20, 3, p.accent);
          }
          p.text(std::to_wstring(cfg.restSeconds), 148, 156, 22, 24, 16,
                 active ? p.onAccent : p.ink, true);
          p.text(L"秒", 170, 156, 18, 24, 11, p.muted);
          p.line(189, 158, 189, 178, p.edge, 1);
          p.line(192, 168, 207, 168, p.edge, 1);
          auto up = cfg.restSeconds < 5 ? p.ink : p.muted;
          auto down = cfg.restSeconds > 1 ? p.ink : p.muted;
          p.line(195, 164, 199, 160, up, 1.3f);
          p.line(199, 160, 203, 164, up, 1.3f);
          p.line(195, 172, 199, 176, down, 1.3f);
          p.line(199, 176, 203, 172, down, 1.3f);
          hit(w, 30, 145, 154, 44, 28, L"休息时长，1到5秒");
          hit(w, 32, 189, 148, 22, 20, L"增加休息时长");
          hit(w, 31, 189, 168, 22, 20, L"减少休息时长");
        }
        p.circle(252, 168, pressedId == 50 ? 14 : 16, p.accent);
        p.icon(3, 252, 168, p.onAccent);
        hit(w, 50, 234, 150, 36, 36, L"开始新的倒计时");
        if (settingError)
          p.text(L"至少 1 秒", 18, 194, 252, 20, 11, p.ink);
        focusBorder(p, w);
      } else if (w.kind == 4) {
        p.text(tipTitle, 10, 4, 224, 20, 11, p.ink, false, 0);
        p.text(tipDetail, 10, 24, 224, 16, 10, p.muted, false, 0);
      } else if (w.kind == 3) {
        auto ms = restEnd > nowMs() ? restEnd - nowMs() : 0;
        float phase = float(nowMs() - restStarted) / 4000 * 6.283185f;
        float wave = reduced ? 0 : (1 - cosf(phase)) * .5f;
        p.circle(110, 110, 62 + 8 * wave, rgb(dark ? 0xBBBBBB : 0x666666, 80), true, 1);
        p.circle(110, 110, 52, p.track);
        p.circle(110, 110, 52, p.edge, true, 1);
        Pen progress(p.ink, 2);
        progress.SetStartCap(LineCapRound);
        progress.SetEndCap(LineCapRound);
        float fraction = std::min(1.f, float(ms) / (cfg.restSeconds * 1000));
        if (fraction > 0)
          p.g.DrawArc(&progress, 58.f, 58.f, 104.f, 104.f, -90.f, 360 * fraction);
        p.text(std::to_wstring((ms + 999) / 1000), 35, 55, 150, 110, 42, p.ink, true);
        if (musicPlaying) {
          for (int i = 0; i < 5; ++i) {
            float amplitude = reduced ? 4 : 3 + 4 * (.5f + .5f * sinf(phase * 2 + i * .8f));
            p.line(98.f + i * 6, 183 - amplitude, 98.f + i * 6, 183 + amplitude, p.muted, 2);
          }
        }
      }
    }
    BYTE opacity = 255;
    if (!reduced && w.kind == 1 && settingsOpened)
      opacity = (BYTE)(255 * std::min(1.f, float(nowMs() - settingsOpened) / 180.f));
    if (!reduced && w.kind == 3 && restStarted)
      opacity = (BYTE)(255 * std::min(1.f, float(nowMs() - restStarted) / 240.f));
    w.surface.present(w.h, opacity);
    ++paints;
  }
  void notifyState() {
    NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, timer.h, OBJID_CLIENT, CHILDID_SELF);
  }
  void schedule() {
    KillTimer(timer.h, 1);
    scheduled = clock.delay(nowMs(), IsWindowVisible(timer.h), controls(),
                            cfg.breath && !reduced && !dragging);
    if (scheduled)
      SetTimer(timer.h, 1, scheduled, nullptr);
  }
  void advance() {
    ++wakes;
    auto now = nowMs();
    if (clock.advance(now)) {
      lastLatency = now - clock.deadline;
      ++completions;
      notifyState();
      if (cfg.rest)
        showRest();
      else {
        if (cfg.sound)
          playTone();
        if (!reduced && IsWindowVisible(timer.h) && !controls()) {
          completionEmphasis = true;
          SetTimer(timer.h, 7, 650, nullptr);
        }
      }
    }
    draw(timer);
    schedule();
  }
  void start(int seconds) {
    if (restEnd)
      endRest();
    if (musicPlaying)
      stopAudio();
    KillTimer(timer.h, 7);
    completionEmphasis = false;
    cfg.seconds = std::clamp(seconds, 1, 359999);
    cfg.save();
    settingsDirty = false;
    clock.start((uint64_t)cfg.seconds * 1000, nowMs());
    resizeTimer();
    notifyState();
    draw(timer);
    schedule();
  }
  void toggle() {
    if (clock.completed) {
      start(cfg.seconds);
      return;
    }
    clock.toggle(nowMs());
    notifyState();
    draw(timer);
    schedule();
  }
  void showSettings() {
    hideTip();
    settingsHover = pressedId = 0;
    fields[0] = cfg.seconds / 3600;
    fields[1] = cfg.seconds / 60 % 60;
    fields[2] = cfg.seconds % 60;
    settings.focus = 10;
    editing = false;
    settingError = false;
    RECT r;
    GetWindowRect(timer.h, &r);
    settings.dpi = timer.dpi;
    SetWindowPos(settings.h, HWND_TOPMOST, r.left, r.bottom + 8, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    sizeSettings();
    settingsOpened = nowMs();
    if (!reduced)
      SetTimer(timer.h, 8, 16, nullptr);
    ShowWindow(settings.h, SW_SHOW);
    SetForegroundWindow(settings.h);
    SetFocus(settings.h);
    draw(settings);
  }
  void sizeSettings() {
    int ww = 288 * settings.dpi, hh = 204 * settings.dpi;
    if (settingError)
      hh += 20 * settings.dpi;
    SetWindowPos(settings.h, HWND_TOPMOST, 0, 0, ww, hh, SWP_NOMOVE | SWP_NOACTIVATE);
    shape(settings, ww, hh);
    clamp(settings.h);
  }
  void setRestSeconds(int value) {
    cfg.restSeconds = std::clamp(value, 1, 5);
    settingsDirty = true;
    draw(settings);
    auto target = byId(settings, 30);
    if (target)
      NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, settings.h, OBJID_CLIENT,
                     (LONG)(target - settings.hits.data()) + 1);
  }
  void flushSettings() {
    if (settingsDirty) {
      cfg.save();
      settingsDirty = false;
    }
  }
  void hideTip() {
    KillTimer(timer.h, 5);
    KillTimer(timer.h, 6);
    if (tip.h)
      ShowWindow(tip.h, SW_HIDE);
  }
  void showTip(int id, bool transient = false) {
    hideTip();
    if (!IsWindowVisible(settings.h) || id < 20 || (id > 32 && id != 50))
      return;
    if (id == 20 || id == 21) {
      bool on = selected(id);
      tipTitle = std::wstring(id == 20 ? L"最后5秒呼吸" : L"到时声音") + (on ? L" · 已开启" : L" · 已关闭");
      tipDetail = on ? L"点击关闭" : L"点击开启";
    } else if (id == 22) {
      tipTitle = cfg.rest ? L"全屏休息 " + std::to_wstring(cfg.restSeconds) + L"秒" : L"呼吸提醒";
      tipDetail = cfg.rest ? L"点击切换为呼吸提醒" : L"最后5秒数字呼吸，点击切换全屏休息";
    } else if (id == 23) {
      tipTitle = audioFailed ? L"音乐无法播放" : musicPreview ? L"柔光 · 正在试听" : cfg.music ? L"休息音乐 · 已开启" : L"休息音乐 · 已关闭";
      tipDetail = audioFailed ? L"请检查声音输出设备" : musicPreview ? L"再次点击关闭音乐并停止试听" : cfg.music ? L"仅全屏休息时播放，点击关闭" : L"点击开启并试听5秒";
    } else if (id >= 30 && id <= 32 && cfg.rest) {
      tipTitle = L"休息时长：" + std::to_wstring(cfg.restSeconds) + L"秒（1–5秒）";
      tipDetail = id == 30 ? L"输入数字或使用键盘上下箭头" : id == 32 ? L"点击增加1秒" : L"点击减少1秒";
    } else if (id == 50) {
      tipTitle = L"开始倒计时";
      tipDetail = L"快捷键：Enter";
    } else
      return;
    if (!tip.h) {
      make(tip, 244, 46, settings.h);
      SetWindowLongPtrW(tip.h, GWL_EXSTYLE,
                       GetWindowLongPtrW(tip.h, GWL_EXSTYLE) | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT);
    }
    tip.dpi = settings.dpi;
    RECT r;
    GetWindowRect(settings.h, &r);
    int width = (int)(244 * tip.dpi), height = (int)(46 * tip.dpi);
    SetWindowPos(tip.h, HWND_TOPMOST, r.left + (int)(22 * tip.dpi),
                 r.top + (int)(98 * tip.dpi), width, height, SWP_NOACTIVATE);
    shape(tip, width, height);
    clamp(tip.h);
    ShowWindow(tip.h, SW_SHOWNOACTIVATE);
    draw(tip);
    if (transient)
      SetTimer(timer.h, 6, 1200, nullptr);
  }
  void closeSettings() {
    KillTimer(timer.h, 8);
    settingsOpened = musicChanged = 0;
    if (musicPreview)
      stopAudio();
    hideTip();
    settingsHover = pressedId = 0;
    if (GetCapture() == settings.h)
      ReleaseCapture();
    ShowWindow(settings.h, SW_HIDE);
    flushSettings();
  }
  void toggleVisibility() {
    bool visible = IsWindowVisible(timer.h) || IsWindowVisible(settings.h);
    closeSettings();
    cancelTimerPointer();
    ShowWindow(timer.h, visible ? SW_HIDE : SW_SHOWNOACTIVATE);
    if (!visible)
      draw(timer);
    schedule();
  }
  void action(Win &w, int id) {
    if (w.kind == 0) {
      if (id == 1)
        showSettings();
      if (id == 2)
        toggle();
      return;
    }
    if (id == 9) {
      closeSettings();
      return;
    }
    if (id >= 10 && id <= 12) {
      w.focus = id;
      editing = false;
      draw(w);
      return;
    }
    if (id >= 40 && id <= 43) {
      fields[0] = fields[2] = 0;
      fields[1] = PRESET_MINUTES[id - 40];
      settingError = false;
    }
    if (id == 20)
      cfg.breath = !cfg.breath;
    if (id == 21)
      cfg.sound = !cfg.sound;
    if (id == 22) {
      cfg.rest = !cfg.rest;
      cfg.breath = !cfg.rest;
    }
    if (id == 23) {
      musicFrom = musicPosition();
      cfg.music = !cfg.music;
      musicChanged = nowMs();
      if (!reduced)
        SetTimer(timer.h, 8, 16, nullptr);
      if (!cfg.music && musicPreview)
        stopAudio();
      if (cfg.music)
        playMusic(5, true);
      if (audioFailed)
        showTip(23, true);
    }
    if (id == 30) {
      w.focus = 30;
      draw(w);
      return;
    }
    if (id == 31 || id == 32)
      cfg.restSeconds = std::clamp(cfg.restSeconds + (id == 32 ? 1 : -1), 1, 5);
    if (id == 50) {
      int value = fields[0] * 3600 + fields[1] * 60 + fields[2];
      if (!value) {
        settingError = true;
        sizeSettings();
        draw(w);
        return;
      }
      closeSettings();
      start(value);
      return;
    }
    if (id >= 20 && id <= 32) {
      // Keep disk I/O out of button feedback. Save once on close, start, or exit.
      settingsDirty = true;
      draw(w);
      NotifyWinEvent(EVENT_OBJECT_STATECHANGE, w.h, OBJID_CLIENT, CHILDID_SELF);
      if ((id == 20 || id == 21 || id == 23) && keyMode)
        showTip(id);
      if (id == 22) {
        showTip(22, true);
        auto target = byId(w, 22);
        if (target)
          NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, w.h, OBJID_CLIENT,
                         (LONG)(target - w.hits.data()) + 1);
      }
      schedule();
      return;
    }
    sizeSettings();
    draw(w);
  }
  void playTone() { // A tiny generated PCM chime; no media engine or permanent audio thread.
    stopAudio();
    if (soundData.empty()) {
      const int count = 11025 / 2;
      soundData.resize(44 + count * 2);
      BYTE *b = soundData.data();
      auto put = [&](int off, DWORD v) { memcpy(b + off, &v, 4); };
      memcpy(b, "RIFF", 4);
      put(4, (DWORD)soundData.size() - 8);
      memcpy(b + 8, "WAVEfmt ", 8);
      put(16, 16);
      WORD fmt[2] = {1, 1};
      memcpy(b + 20, fmt, 4);
      put(24, 11025);
      put(28, 22050);
      WORD bits[2] = {2, 16};
      memcpy(b + 32, bits, 4);
      memcpy(b + 36, "data", 4);
      put(40, count * 2);
      for (int i = 0; i < count; i++) {
        double t = i / 11025.;
        double hz = t < .22 ? 660 : 880;
        double local = t < .22 ? t : t - .22;
        short v = (short)(sin(t * hz * 6.283185) * 2700 * std::min(1., local / .015) *
                          std::max(0., 1 - local / .26));
        memcpy(b + 44 + i * 2, &v, 2);
      }
    }
    PlaySoundW((LPCWSTR)soundData.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    SetTimer(timer.h, 4, 700, nullptr);
  }
  void stopAudio() {
    KillTimer(timer.h, 4);
    KillTimer(timer.h, 9);
    PlaySoundW(nullptr, nullptr, 0);
    musicPlaying = musicPreview = false;
    soundData.clear();
    soundData.shrink_to_fit();
  }
  bool playMusic(int seconds, bool preview = false) {
    stopAudio();
    soundData = relaxationWave(seconds);
    audioFailed = !PlaySoundW((LPCWSTR)soundData.data(), nullptr,
                             SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    if (audioFailed) {
      stopAudio();
      return false;
    }
    musicPlaying = true;
    musicPreview = preview;
    SetTimer(timer.h, 4, seconds * 1000, nullptr);
    if (preview && !reduced)
      SetTimer(timer.h, 9, 80, nullptr);
    return true;
  }
  static BOOL CALLBACK monitor(HMONITOR m, HDC, LPRECT, LPARAM l) {
    ((App *)l)->addCover(m);
    return TRUE;
  }
  void addCover(HMONITOR monitor) {
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(monitor, &mi);
    auto v = std::make_unique<Win>();
    v->kind = 2;
    make(*v, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
         timer.h);
    SetWindowLongPtrW(v->h, GWL_EXSTYLE, GetWindowLongPtrW(v->h, GWL_EXSTYLE) & ~WS_EX_LAYERED);
    v->glass = restBackdrop(v->h);
    SetWindowPos(v->h, HWND_TOPMOST, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_NOACTIVATE);
    HWND base = v->h;
    covers.push_back(std::move(v));
    ShowWindow(base, SW_SHOWNOACTIVATE);
    InvalidateRect(base, nullptr, TRUE);
    auto orb = std::make_unique<Win>();
    orb->kind = 3;
    make(*orb, 220, 220, base);
    orb->dpi = dpiFor(base) / 96.f;
    int size = 220 * orb->dpi;
    SetWindowPos(orb->h, HWND_TOPMOST, (mi.rcMonitor.left + mi.rcMonitor.right - size) / 2,
                 (mi.rcMonitor.top + mi.rcMonitor.bottom - size) / 2, size, size, SWP_NOACTIVATE);
    orbs.push_back(std::move(orb));
    ShowWindow(orbs.back()->h, SW_SHOWNOACTIVATE);
    draw(*orbs.back());
  }
  void showRest() {
    if (restEnd)
      return;
    previous = GetForegroundWindow();
    closeSettings();
    bool played = cfg.music && playMusic(cfg.restSeconds);
    if (!played && cfg.sound)
      playTone();
    restStarted = nowMs();
    restEnd = restStarted + cfg.restSeconds * 1000;
    EnumDisplayMonitors(nullptr, nullptr, monitor, (LPARAM)this);
    if (!covers.empty())
      SetForegroundWindow(covers[0]->h);
    RegisterHotKey(timer.h, 1, 0x4000, VK_ESCAPE);
    SetTimer(timer.h, 2, reduced ? 200 : 50, nullptr);
  }
  void endRest() {
    if (musicPlaying)
      stopAudio();
    UnregisterHotKey(timer.h, 1);
    KillTimer(timer.h, 2);
    restEnd = 0;
    for (auto &o : orbs)
      DestroyWindow(o->h);
    orbs.clear();
    for (auto &o : covers)
      DestroyWindow(o->h);
    covers.clear();
    if (previous && IsWindow(previous) && previous != settings.h)
      SetForegroundWindow(previous);
    previous = nullptr;
    draw(timer);
  }
  void menu() {
    HMENU m = CreatePopupMenu();
    HMENU preferences = CreatePopupMenu();
    AppendMenuW(preferences, MF_STRING, 101, L"倒计时…");
    AppendMenuW(preferences, MF_STRING | (cfg.sound ? MF_CHECKED : 0), 108, L"到时提示音");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)preferences, L"设置");
    AppendMenuW(m, MF_STRING | (clock.completed ? MF_GRAYED : 0), 102,
                clock.running ? L"暂停" : L"继续");
    AppendMenuW(m, MF_STRING, 103, L"重新开始");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (cfg.pin ? MF_CHECKED : 0), 104, L"置顶");
    AppendMenuW(m, MF_STRING | (cfg.glass ? MF_CHECKED : 0), 105, L"磨砂玻璃");
    AppendMenuW(m, MF_STRING, 106, IsWindowVisible(timer.h) ? L"隐藏浮窗" : L"显示浮窗");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 107, L"退出");
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(timer.h);
    int id = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, timer.h, nullptr);
    DestroyMenu(m);
    PostMessageW(timer.h, WM_NULL, 0, 0);
    if (id == 101)
      showSettings();
    if (id == 102)
      toggle();
    if (id == 103)
      start(cfg.seconds);
    if (id == 104) {
      cfg.pin = !cfg.pin;
      resizeTimer();
      cfg.save();
    }
    if (id == 105) {
      cfg.glass = !cfg.glass;
      cfg.save();
      timer.glass = backdrop(timer.h, cfg.glass);
      settings.glass = backdrop(settings.h, cfg.glass);
      draw(timer);
      draw(settings);
    }
    if (id == 106) {
      toggleVisibility();
    }
    if (id == 108) {
      cfg.sound = !cfg.sound;
      cfg.save();
    }
    if (id == 107)
      DestroyWindow(timer.h);
  }
  void inspect(const std::wstring &path) {
    PROCESS_MEMORY_COUNTERS_EX m{};
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&m, sizeof(m));
    RECT r;
    GetWindowRect(timer.h, &r);
    FILE *f = _wfopen(path.c_str(), L"wb");
    if (!f)
      return;
    std::ostringstream s;
    s << "{\"tipVisible\":" << (tip.h && IsWindowVisible(tip.h)) << ",\"pid\":" << GetCurrentProcessId() << ",\"running\":" << clock.running
      << ",\"completed\":" << clock.completed << ",\"remainingMs\":" << clock.remaining(nowMs())
      << ",\"completionEmphasis\":" << completionEmphasis
      << ",\"controls\":" << controls()
      << ",\"hoverArmed\":" << hoverArmed << ",\"pointerDown\":" << timerPointerDown
      << ",\"dragging\":" << dragging
      << ",\"settingsVisible\":" << (IsWindowVisible(settings.h) != 0)
      << ",\"restActive\":" << (restEnd != 0) << ",\"covers\":" << covers.size()
      << ",\"restSeconds\":" << cfg.restSeconds << ",\"breath\":" << cfg.breath
      << ",\"sound\":" << cfg.sound
      << ",\"music\":" << cfg.music << ",\"musicPlaying\":" << musicPlaying
      << ",\"musicPreview\":" << musicPreview << ",\"audioFailed\":" << audioFailed
      << ",\"reducedMotion\":" << reduced
      << ",\"restMode\":" << cfg.rest << ",\"glassActive\":" << timer.glass
      << ",\"dpi\":" << timer.dpi << ",\"visible\":" << (IsWindowVisible(timer.h) != 0)
      << ",\"scheduledMs\":" << scheduled << ",\"paintCount\":" << paints
      << ",\"wakeCount\":" << wakes << ",\"completionCount\":" << completions
      << ",\"completionLatencyMs\":" << lastLatency << ",\"workingSet\":" << m.WorkingSetSize
      << ",\"privateBytes\":" << m.PrivateUsage
      << ",\"gdi\":" << GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS)
      << ",\"user\":" << GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) << ",\"rect\":["
      << r.left << "," << r.top << "," << r.right << "," << r.bottom << "],\"settingsRect\":[";
    GetWindowRect(settings.h, &r);
    s << r.left << "," << r.top << "," << r.right << "," << r.bottom << "]}";
    auto data = s.str();
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
  }
  void command(const std::wstring &cmd) {
    std::wistringstream in(cmd);
    std::wstring op;
    in >> op;
    if (op == L"show") {
      ShowWindow(timer.h, SW_SHOWNOACTIVATE);
      draw(timer);
      schedule();
    } else if (op == L"hide") {
      ShowWindow(timer.h, SW_HIDE);
      schedule();
    } else if (op == L"toggle-visible")
      toggleVisibility();
    else if (op == L"settings")
      showSettings();
    else if (op == L"close-settings")
      closeSettings();
    else if (op == L"toggle")
      toggle();
    else if (op == L"start") {
      int sec = cfg.seconds;
      in >> sec;
      start(sec);
    } else if (op == L"quit")
      DestroyWindow(timer.h);
    else if (op == L"inspect") {
      std::wstring path;
      getline(in >> std::ws, path);
      inspect(path);
    } else if (test && op == L"rest") {
      int v = 0;
      in >> v;
      cfg.rest = v != 0;
      cfg.breath = !cfg.rest;
      in >> cfg.restSeconds;
      cfg.restSeconds = std::clamp(cfg.restSeconds, 1, 5);
      cfg.save();
    } else if (test && op == L"sound") {
      int v = 0;
      in >> v;
      cfg.sound = v != 0;
    } else if (test && op == L"reduced") {
      int v = 0;
      in >> v;
      reduced = v != 0;
      draw(settings);
    } else if (test && op == L"glass") {
      int v = 0;
      in >> v;
      cfg.glass = v != 0;
      timer.glass = backdrop(timer.h, cfg.glass);
      draw(timer);
    } else if (test && op == L"cancel-rest")
      endRest();
    else if (test && op == L"move") {
      int x, y;
      in >> x >> y;
      SetWindowPos(timer.h, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      clamp(timer.h);
    } else if (test && op == L"theme") {
      int d;
      in >> d;
      dark = d;
      draw(timer);
      draw(settings);
    }
  }
  LRESULT event(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Win *w = window(h);
    if (msg == WM_COPYDATA && h == timer.h) {
      auto p = (COPYDATASTRUCT *)lp;
      if (p && p->dwData == PROTOCOL && p->cbData >= 2 && p->cbData <= 32768 && p->lpData &&
          ((wchar_t *)p->lpData)[p->cbData / 2 - 1] == 0) {
        command((wchar_t *)p->lpData);
        return 1;
      }
      return 0;
    }
    if (msg == taskbarMessage && h == timer.h) {
      tray();
      return 0;
    }
    if (!w)
      return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
    case WM_GETOBJECT:
      if ((LONG)lp == OBJID_CLIENT) {
        auto a = new Accessible(h);
        auto result = LresultFromObject(IID_IAccessible, wp, a);
        a->Release();
        return result;
      }
      break;
    case WM_HOTKEY:
      if (wp == 1 && restEnd)
        endRest();
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      if (w->kind == 2) {
        RECT r;
        GetClientRect(h, &r);
        if (!w->glass) {
          Graphics g(dc);
          LinearGradientBrush b(Point(0, 0), Point(r.right, r.bottom),
                                rgb(dark ? 0x222222 : 0xDEDEDE), rgb(dark ? 0x333333 : 0xEEEEEE));
          g.FillRectangle(&b, 0, 0, (INT)r.right, (INT)r.bottom);
        }
      } else
        draw(*w);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_TIMER:
      if (h == timer.h) {
        if (wp == 9) {
          if (!musicPreview || reduced || !IsWindowVisible(settings.h))
            KillTimer(h, 9);
          draw(settings);
        }
        if (wp == 8) {
          auto now = nowMs();
          if (reduced || !IsWindowVisible(settings.h) ||
              ((!settingsOpened || now - settingsOpened >= 180) &&
               (!musicChanged || now - musicChanged >= 180))) {
            KillTimer(h, 8);
            settingsOpened = musicChanged = 0;
          }
          draw(settings);
        }
        if (wp == 7) {
          KillTimer(h, 7);
          completionEmphasis = false;
          if (IsWindowVisible(h))
            draw(timer);
        }
        if (wp == 5) {
          KillTimer(h, 5);
          showTip(settingsHover);
        }
        if (wp == 6)
          hideTip();
        if (wp == 1) {
          KillTimer(h, 1);
          advance();
        }
        if (wp == 2) {
          if (nowMs() >= restEnd)
            endRest();
          else
            for (auto &o : orbs)
              draw(*o);
        }
        if (wp == 4) {
          bool preview = musicPreview;
          stopAudio();
          if (preview)
            draw(settings);
        }
      }
      return 0;
    case WM_NCHITTEST:
      if (w->kind == 4)
        return HTTRANSPARENT;
      break;
    case WM_MOUSEACTIVATE:
      if (w->kind == 4)
        return MA_NOACTIVATE;
      break;
    case WM_SETCURSOR:
      if (w->kind == 1 && LOWORD(lp) == HTCLIENT) {
        POINT pos;
        GetCursorPos(&pos);
        ScreenToClient(h, &pos);
        auto target = at(*w, pos.x, pos.y);
        SetCursor(LoadCursorW(nullptr, target && ((target->id >= 10 && target->id <= 12) || target->id == 30) ? IDC_IBEAM
                                     : target ? IDC_HAND : IDC_ARROW));
        return TRUE;
      }
      break;
    case WM_MOUSEMOVE:
      if (w->kind == 0) {
        if (timerPointerDown) {
          POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
          ClientToScreen(h, &p);
          if (pastDragThreshold(p)) {
            // Decide drag intent before releasing capture; this press can never click.
            cancelTimerPointer();
            dragging = true;
            draw(timer);
            schedule();
            SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(p.x, p.y));
            dragging = false;
            armHover();
            draw(timer);
            schedule();
          }
        } else {
          armHover();
        }
        return 0;
      }
      if (w->kind == 1) {
        auto target = at(*w, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        int id = target ? target->id : 0;
        if (id != settingsHover) {
          settingsHover = id;
          hideTip();
          draw(settings);
          if (id >= 20 && (id <= 32 || id == 50))
            SetTimer(timer.h, 5, 450, nullptr);
        }
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
      }
      return 0;
    case WM_MOUSEHOVER:
      if (w->kind == 0 && hoverArmed) {
        hoverArmed = false;
        POINT p;
        GetCursorPos(&p);
        if (!timerPointerDown && !dragging && IsWindowVisible(h) && WindowFromPoint(p) == h &&
            !(GetAsyncKeyState(VK_LBUTTON) & 0x8000) &&
            !(GetAsyncKeyState(VK_RBUTTON) & 0x8000) &&
            !(GetAsyncKeyState(VK_MBUTTON) & 0x8000)) {
          hover = true;
          keyMode = false;
          draw(timer);
          schedule();
        }
      }
      return 0;
    case WM_MOUSELEAVE:
      if (w->kind == 1) {
        settingsHover = 0;
        hideTip();
        draw(settings);
      }
      if (w->kind == 0) {
        cancelHover();
        hover = false;
        draw(timer);
        schedule();
      }
      return 0;
    case WM_LBUTTONDOWN: {
      if (w->kind == 0) {
        auto hit = controls() ? at(*w, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)) : nullptr;
        timerPressedId = hit ? hit->id : 0;
        // Preserve keyboard-revealed buttons through this pointer press.
        hover = controls();
        keyMode = false;
        cancelHover();
        timerPointerDown = true;
        timerPressPoint = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(h, &timerPressPoint);
        SetCapture(h);
        if (timerPressedId)
          w->focus = timerPressedId;
        SetForegroundWindow(h);
        SetFocus(h);
        return 0;
      }
      keyMode = false;
      if (w->kind >= 2)
        return 0;
      auto hit = at(*w, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      if (hit) {
        const int id = hit->id; // Focus events can redraw and rebuild hit targets.
        w->focus = id;
        if (w->kind == 1) {
          hideTip();
          pressedId = id;
          SetCapture(h);
        }
        SetForegroundWindow(h);
        SetFocus(h);
        action(*w, id);
      } else {
        ReleaseCapture();
        SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
      }
      return 0;
    }
    case WM_LBUTTONUP:
      if (w->kind == 0 && timerPointerDown) {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        auto hit = controls() ? at(*w, p.x, p.y) : nullptr;
        ClientToScreen(h, &p);
        int id = hit && hit->id == timerPressedId && !pastDragThreshold(p) ? hit->id : 0;
        timerPointerDown = false;
        timerPressedId = 0;
        ReleaseCapture();
        if (id)
          action(*w, id);
        armHover();
        return 0;
      }
      if (w->kind == 1) {
        pressedId = 0;
        ReleaseCapture();
        draw(settings);
      }
      return 0;
    case WM_CAPTURECHANGED:
      if (w->kind == 0 && timerPointerDown) {
        cancelTimerPointer();
        draw(timer);
        schedule();
      }
      if (w->kind == 1 && pressedId) {
        pressedId = 0;
        draw(settings);
      }
      return 0;
    case WM_CANCELMODE:
      if (w->kind == 0) {
        cancelTimerPointer();
        draw(timer);
        schedule();
      }
      break;
    case WM_SHOWWINDOW:
      if (w->kind == 0 && !wp) {
        KillTimer(h, 7);
        completionEmphasis = false;
        cancelTimerPointer();
      }
      break;
    case WM_RBUTTONUP:
      if (w->kind < 2)
        menu();
      return 0;
    case WM_ENTERSIZEMOVE:
      dragging = true;
      if (w->kind == 0) {
        cancelTimerPointer();
        draw(timer);
      }
      schedule();
      return 0;
    case WM_EXITSIZEMOVE:
      dragging = false;
      if (w->kind == 0) {
        clamp(h);
        RECT r;
        GetWindowRect(h, &r);
        cfg.x = r.left;
        cfg.y = r.top;
        cfg.save();
      }
      schedule();
      return 0;
    case WM_CHAR:
      if (w->kind == 1 && w->focus == 30 && cfg.rest) {
        if (wp >= L'1' && wp <= L'5')
          setRestSeconds((int)(wp - L'0'));
        return 0;
      }
      if (w->kind == 1 && w->focus >= 10 && w->focus <= 12) {
        int &n = fields[w->focus - 10];
        if (wp >= L'0' && wp <= L'9') {
          if (!editing)
            n = 0;
          n = (n * 10 + (int)wp - L'0') % 100;
          n = std::min(n, w->focus == 10 ? 99 : 59);
          editing = true;
          settingError = false;
          draw(*w);
          NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, h, OBJID_CLIENT, CHILDID_SELF);
        }
        if (wp == 8) {
          n = editing ? n / 10 : 0;
          editing = true;
          draw(*w);
        }
      }
      return 0;
    case WM_KEYDOWN:
      keyMode = true;
      if (w->kind == 1 && w->focus == 30 && cfg.rest && (wp == VK_UP || wp == VK_DOWN)) {
        setRestSeconds(cfg.restSeconds + (wp == VK_UP ? 1 : -1));
        return 0;
      }
      if (w->kind == 1 && w->focus >= 10 && w->focus <= 12) {
        if (wp == L'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
          editing = false;
          draw(*w);
          return 0;
        }
        if (wp == VK_DELETE) {
          fields[w->focus - 10] = 0;
          editing = true;
          draw(*w);
          return 0;
        }
      }
      if (wp == VK_ESCAPE) {
        if (restEnd)
          endRest();
        else if (w->kind == 1)
          closeSettings();
        else {
          keyMode = false;
          draw(timer);
          schedule();
        }
        return 0;
      }
      if (wp == VK_TAB) {
        if (w->kind == 0) {
          w->focus = w->focus == 1 ? 2 : 1;
          draw(*w);
          schedule();
        } else if (w->kind == 1) {
          std::vector<int> ids;
          for (auto &v : w->hits)
            ids.push_back(v.id);
          auto it = std::find(ids.begin(), ids.end(), w->focus);
          int at = it == ids.end() ? 0 : (int)(it - ids.begin());
          int delta = GetKeyState(VK_SHIFT) & 0x8000 ? -1 : 1;
          w->focus = ids[(at + delta + (int)ids.size()) % ids.size()];
          editing = false;
          draw(*w);
          showTip(w->focus);
          NotifyWinEvent(EVENT_OBJECT_FOCUS, h, OBJID_CLIENT,
                         (LONG)((std::find(ids.begin(), ids.end(), w->focus) - ids.begin()) + 1));
        }
        return 0;
      }
      if (wp == VK_RETURN || wp == VK_SPACE) {
        if (w->kind == 0) {
          if (wp == VK_RETURN && w->focus == 1)
            showSettings();
          else
            toggle();
        } else if (w->kind == 1) {
          action(*w, ((w->focus >= 10 && w->focus <= 12) || w->focus == 30) ? 50 : w->focus);
        }
        return 0;
      }
      if (w->kind == 1 && w->focus >= 10 && w->focus <= 12 && (wp == VK_UP || wp == VK_DOWN)) {
        auto &n = fields[w->focus - 10];
        n = std::clamp(n + (wp == VK_UP ? 1 : -1), 0, w->focus == 10 ? 99 : 59);
        draw(*w);
        return 0;
      }
      break;
    case WM_ACTIVATE:
      if (LOWORD(wp) == WA_INACTIVE) {
        if (w->kind == 1)
          closeSettings();
        if (w->kind == 0) {
          keyMode = false;
          draw(timer);
          schedule();
        }
      } else if (!HIWORD(wp) && w->kind < 2) {
        SetFocus(h);
        draw(*w);
      }
      return 0;
    case WM_DPICHANGED: {
      w->dpi = HIWORD(wp) / 96.f;
      auto r = (RECT *)lp;
      SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      if (w->kind == 0)
        resizeTimer();
      if (w->kind == 1)
        sizeSettings();
      draw(*w);
      return 0;
    }
    case WM_DISPLAYCHANGE:
      if (restEnd)
        endRest();
      clamp(timer.h);
      draw(timer);
      return 0;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
      theme();
      timer.glass = backdrop(timer.h, cfg.glass);
      settings.glass = backdrop(settings.h, cfg.glass);
      draw(timer);
      draw(settings);
      schedule();
      return 0;
    case WM_POWERBROADCAST:
      if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
        if (restEnd)
          endRest();
        advance();
      }
      return TRUE;
    case TRAY:
      if (lp == WM_LBUTTONUP) {
        toggleVisibility();
      }
      if (lp == WM_RBUTTONUP)
        menu();
      return 0;
    case WM_CLOSE:
      if (w->kind == 1)
        closeSettings();
      else if (w->kind >= 2)
        endRest();
      else
        DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      if (h == timer.h) {
        hideTip();
        flushSettings();
        tray(true);
        KillTimer(h, 1);
        KillTimer(h, 2);
        endRest();
        PlaySoundW(nullptr, nullptr, 0);
        if (IsWindow(settings.h))
          DestroyWindow(settings.h);
        PostQuitMessage(0);
      }
      return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
  }
};

static Hit *accHit(HWND h, VARIANT v) {
  if (!app || v.vt != VT_I4 || v.lVal <= 0)
    return nullptr;
  auto w = app->window(h);
  return w && v.lVal <= (long)w->hits.size() ? &w->hits[v.lVal - 1] : nullptr;
}
HRESULT Accessible::get_accChildCount(long *p) {
  auto w = app->window(hwnd);
  *p = w ? (long)w->hits.size() : 0;
  return S_OK;
}
HRESULT Accessible::get_accName(VARIANT v, BSTR *p) {
  if (!p)
    return E_POINTER;
  auto h = accHit(hwnd, v);
  std::wstring name =
      h ? h->name
      : hwnd == app->timer.h
          ? (app->clock.completed ? L"时间到，00:00"
                                  : (app->clock.running ? L"剩余 " : L"已暂停 ") + app->display())
          : L"轻计时设置";
  *p = SysAllocString(name.c_str());
  return S_OK;
}
HRESULT Accessible::get_accValue(VARIANT v, BSTR *p) {
  *p = nullptr;
  auto h = accHit(hwnd, v);
  if (hwnd == app->settings.h && h && h->id >= 10 && h->id <= 12) {
    *p = SysAllocString(std::to_wstring(app->fields[h->id - 10]).c_str());
    return S_OK;
  }
  if (hwnd == app->settings.h && h && h->id == 30) {
    *p = SysAllocString(std::to_wstring(app->cfg.restSeconds).c_str());
    return S_OK;
  }
  return S_FALSE;
}
HRESULT Accessible::get_accRole(VARIANT v, VARIANT *p) {
  VariantInit(p);
  p->vt = VT_I4;
  auto h = accHit(hwnd, v);
  int id = h ? h->id : 0;
  p->lVal = !id                                              ? ROLE_SYSTEM_GROUPING
            : (id >= 10 && id <= 12) || id == 30              ? ROLE_SYSTEM_SPINBUTTON
            : id == 20 || id == 21 || id == 23               ? ROLE_SYSTEM_CHECKBUTTON
            : id >= 40 && id <= 43                           ? ROLE_SYSTEM_RADIOBUTTON
                                                             : ROLE_SYSTEM_PUSHBUTTON;
  return S_OK;
}
HRESULT Accessible::get_accState(VARIANT v, VARIANT *p) {
  VariantInit(p);
  p->vt = VT_I4;
  p->lVal = STATE_SYSTEM_FOCUSABLE;
  auto w = app->window(hwnd);
  auto h = accHit(hwnd, v);
  if (w && h && GetForegroundWindow() == hwnd && w->focus == h->id)
    p->lVal |= STATE_SYSTEM_FOCUSED;
  if (h && app->selected(h->id))
    p->lVal |= STATE_SYSTEM_CHECKED;
  return S_OK;
}
HRESULT Accessible::get_accFocus(VARIANT *p) {
  VariantInit(p);
  auto w = app->window(hwnd);
  if (w && GetForegroundWindow() == hwnd) {
    p->vt = VT_I4;
    p->lVal = 0;
    for (size_t i = 0; i < w->hits.size(); i++)
      if (w->hits[i].id == w->focus)
        p->lVal = (LONG)i + 1;
    return S_OK;
  }
  return S_FALSE;
}
HRESULT Accessible::accSelect(long, VARIANT v) {
  auto w = app->window(hwnd);
  auto h = accHit(hwnd, v);
  if (!w || !h)
    return E_INVALIDARG;
  w->focus = h->id;
  app->keyMode = true;
  SetForegroundWindow(hwnd);
  app->draw(*w);
  if (hwnd == app->settings.h)
    app->showTip(w->focus);
  return S_OK;
}
HRESULT Accessible::accLocation(long *x, long *y, long *width, long *height, VARIANT v) {
  RECT r;
  GetWindowRect(hwnd, &r);
  auto w = app->window(hwnd);
  auto h = accHit(hwnd, v);
  if (h && w) {
    *x = r.left + (LONG)(h->r.left * w->dpi);
    *y = r.top + (LONG)(h->r.top * w->dpi);
    *width = (h->r.right - h->r.left) * w->dpi;
    *height = (h->r.bottom - h->r.top) * w->dpi;
  } else {
    *x = r.left;
    *y = r.top;
    *width = r.right - r.left;
    *height = r.bottom - r.top;
  }
  return S_OK;
}
HRESULT Accessible::accHitTest(long x, long y, VARIANT *p) {
  VariantInit(p);
  POINT pos{x, y};
  ScreenToClient(hwnd, &pos);
  auto w = app->window(hwnd);
  auto h = w ? app->at(*w, pos.x, pos.y) : nullptr;
  p->vt = VT_I4;
  p->lVal = CHILDID_SELF;
  if (h)
    p->lVal = (LONG)(h - w->hits.data()) + 1;
  return S_OK;
}
HRESULT Accessible::accDoDefaultAction(VARIANT v) {
  auto w = app->window(hwnd);
  auto h = accHit(hwnd, v);
  if (!w || !h)
    return E_INVALIDARG;
  app->action(*w, h->id);
  return S_OK;
}
HRESULT Accessible::put_accValue(VARIANT v, BSTR s) {
  auto h = accHit(hwnd, v);
  if (hwnd == app->settings.h && h && h->id == 30 && s) {
    if (SysStringLen(s) != 1 || s[0] < L'1' || s[0] > L'5')
      return E_INVALIDARG;
    app->setRestSeconds(s[0] - L'0');
    return S_OK;
  }
  if (hwnd != app->settings.h || !h || h->id < 10 || h->id > 12 || !s)
    return E_INVALIDARG;
  wchar_t *end = nullptr;
  long n = wcstol(s, &end, 10);
  if (!end || *end || n < 0 || n > (h->id == 10 ? 99 : 59))
    return E_INVALIDARG;
  app->fields[h->id - 10] = (int)n;
  app->draw(app->settings);
  return S_OK;
}

static int selftest() {
  Countdown c;
  c.start(10000, 500);
  if (c.remaining(5500) != 5000)
    return 1;
  c.toggle(5500);
  if (c.remaining(20000) != 5000)
    return 2;
  c.toggle(25000);
  if (c.remaining(26000) != 4000)
    return 3;
  if (c.advance(29999) || !c.advance(30000) || c.advance(31000))
    return 4;
  c.start(1000, 0);
  if (c.delay(0, false, false, true) != 1000)
    return 5;
  if (c.delay(0, true, false, true) != 50)
    return 6;
  c.toggle(10);
  if (c.delay(20, true, false, true))
    return 7;
  c.start(359999000, 100);
  if (c.remaining(101) != 359998999)
    return 8;
  c.start(1000, 100);
  if (!c.advance(9999999))
    return 9;
  return 0;
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  int count;
  auto args = CommandLineToArgvW(GetCommandLineW(), &count);
  bool test = false;
  std::wstring command, config;
  for (int i = 1; i < count; i++) {
    std::wstring a = args[i];
    if (a == L"--self-test") {
      LocalFree(args);
      return selftest();
    }
    if (a == L"--test")
      test = true;
    else if (a == L"--config" && i + 1 < count)
      config = args[++i];
    else if (a == L"--command" && i + 1 < count)
      command = args[++i];
  }
  LocalFree(args);
  const wchar_t *cls = test ? L"LightTimer.Native.Test" : L"LightTimer.Native";
  HWND existing = FindWindowW(cls, L"轻计时");
  if (existing) {
    auto text = command.empty() ? L"toggle-visible" : command.c_str();
    COPYDATASTRUCT cd{PROTOCOL, (DWORD)((wcslen(text) + 1) * 2), (void *)text};
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(existing, WM_COPYDATA, 0, (LPARAM)&cd, SMTO_ABORTIFHUNG, 3000,
                               &result)
               ? 0
               : 2;
  }
  if (!command.empty())
    return 3;
  HANDLE mutex = CreateMutexW(
      nullptr, FALSE, test ? L"Local\\LightTimer.Native.Test" : L"Local\\LightTimer.Native");
  if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
    if (mutex)
      CloseHandle(mutex);
    return 4;
  }
  dpiAware();
  OleInitialize(nullptr);
  GdiplusStartupInput gi;
  ULONG_PTR token;
  if (GdiplusStartup(&token, &gi, nullptr) != Ok) {
    CloseHandle(mutex);
    return 5;
  }
  {
    App a(instance, test);
    app = &a;
    if (config.empty()) {
      wchar_t path[MAX_PATH];
      GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
      config = std::wstring(path) + L"\\LightTimer";
      CreateDirectoryW(config.c_str(), nullptr);
      config += test ? L"\\test.ini" : L"\\settings.ini";
    }
    a.cfg.path = config;
    if (!a.init()) {
      app = nullptr;
      GdiplusShutdown(token);
      OleUninitialize();
      CloseHandle(mutex);
      return 6;
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    app = nullptr;
  }
  GdiplusShutdown(token);
  OleUninitialize();
  CloseHandle(mutex);
  return 0;
}
