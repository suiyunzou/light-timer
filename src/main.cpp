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
enum TimeView { CountdownView, StopwatchView, WallClockView };
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
  bool breath = true, sound = true, rest = false, pin = true, glass = true, music = false,
       personal = false;
  int seconds = 1500, restSeconds = 3, personalTheme = 0, view = CountdownView, x = INT_MIN, y = INT_MIN;
  std::wstring path, musicFile;
  void load() {
    seconds =
        std::clamp((int)GetPrivateProfileIntW(L"timer", L"seconds", 1500, path.c_str()), 1, 359999);
    restSeconds =
        std::clamp((int)GetPrivateProfileIntW(L"timer", L"restSeconds", 3, path.c_str()), 1, 5);
    breath = GetPrivateProfileIntW(L"timer", L"breath", 1, path.c_str());
    sound = GetPrivateProfileIntW(L"timer", L"sound", 1, path.c_str());
    music = GetPrivateProfileIntW(L"timer", L"music", 0, path.c_str());
    personal = GetPrivateProfileIntW(L"timer", L"personal", 0, path.c_str());
    personalTheme = std::clamp((int)GetPrivateProfileIntW(L"timer", L"personalTheme", 0, path.c_str()), 0, 2);
    view = std::clamp((int)GetPrivateProfileIntW(L"timer", L"view", 0, path.c_str()), 0, 2);
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
                                                               {L"personal", personal},
                                                               {L"personalTheme", personalTheme},
                                                               {L"view", view},
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
    std::wstring text = L"[timer]\r\n";
    for (size_t i=0; i+1<section.size(); ++i)
      if (section[i]) text += section[i];
      else text += L"\r\n";
    // Preserve Unicode song paths, including characters outside the system code page.
    auto temporary = path + L".tmp";
    if (FILE *f = _wfopen(temporary.c_str(), L"wb")) {
      const wchar_t bom = 0xFEFF;
      bool ok = fwrite(&bom, sizeof(bom), 1, f) == 1 &&
                fwrite(text.data(), sizeof(wchar_t), text.size(), f) == text.size();
      ok = fclose(f) == 0 && ok;
      if (ok) MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
      else DeleteFileW(temporary.c_str());
    }
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
  Stopwatch stopwatch;
  int timerHover = 0;
  std::vector<std::unique_ptr<Win>> covers, orbs;
  bool dark = true, hover = false, keyMode = false, dragging = false, reduced = false,
       highContrast = false, test = false, editing = false, settingError = false,
       settingsDirty = false;
  int fields[3]{0, 25, 0};
  int reelFrom[3]{}, reelDirection[3]{}, wheelRemainder = 0, wheelField = -1;
  uint64_t reelStarted[3]{};
  uint64_t restEnd = 0, paints = 0, wakes = 0, completions = 0, lastLatency = 0;
  HWND previous = nullptr;
  UINT taskbarMessage;
  std::wstring className;
  std::vector<BYTE> soundData;
  FileAudio fileAudio;
  bool musicPlaying = false, musicPreview = false, audioFailed = false;
  uint64_t settingsOpened = 0, restStarted = 0;
  HICON icon = nullptr, largeIcon = nullptr;
  HBRUSH viewMenuBrush = nullptr;
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
    if (viewMenuBrush)
      DeleteObject(viewMenuBrush);
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
  bool windowPinned() const { return !cfg.personal || cfg.pin; }
  bool windowGlass() const { return cfg.personal && cfg.glass; }
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
    if (!windowGlass() || !transparentAllowed || highContrast)
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
                                           : L"轻计时 · 到时提醒",
                             WS_POPUP, 0, 0, width, height, owner, nullptr, instance, nullptr);
    w.h = h;
    w.dpi = dpiFor(h) / 96.f;
    return h;
  }
  void shape(Win &w, int width, int height) {
    if (w.kind == 2)
      return;
    int r = (int)((w.kind < 2 ? (cfg.personal ? 18 : 8) : 9) * w.dpi);
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
  bool longTime() const {
    return cfg.view == CountdownView ? clock.duration >= 3600000
           : cfg.view == StopwatchView && stopwatch.elapsed(nowMs()) >= 3600000;
  }
  void resizeTimer() {
    int width = (longTime() ? TIMER_LONG_WIDTH : TIMER_SHORT_WIDTH) * timer.dpi,
        height = TIMER_HEIGHT * timer.dpi;
    SetWindowPos(timer.h, windowPinned() ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, width, height,
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
    wcscpy_s(n.szTip, cfg.personal ? L"轻计时 · 个人模式" : L"轻计时 · 工作模式");
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
    make(settings, 288, 156, timer.h);
    timer.glass = backdrop(timer.h, windowGlass());
    settings.glass = backdrop(settings.h, windowGlass());
    resizeTimer();
    POINT pos{};
    GetCursorPos(&pos);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(pos, MONITOR_DEFAULTTONEAREST), &mi);
    int x = cfg.x == INT_MIN ? mi.rcWork.right - int((TIMER_SHORT_WIDTH + 24) * timer.dpi) : cfg.x,
        y = cfg.y == INT_MIN ? mi.rcWork.top + int(36 * timer.dpi) : cfg.y;
    SetWindowPos(timer.h, windowPinned() ? HWND_TOPMOST : HWND_NOTOPMOST, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
    clamp(timer.h);
    setupIcon();
    tray();
    ShowWindow(timer.h, SW_SHOWNOACTIVATE);
    draw(timer);
    schedule();
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
    hideTip();
    timerHover = 0;
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
    return false;
  }
  std::wstring display() {
    wchar_t s[32];
    if (cfg.view == WallClockView) {
      SYSTEMTIME local; GetLocalTime(&local);
      swprintf(s, 32, L"%02u:%02u", local.wHour, local.wMinute);
      return s;
    }
    if (cfg.view == CountdownView && clock.completed)
      return L"00:00";
    auto secs = cfg.view == StopwatchView ? stopwatch.elapsed(nowMs()) / 1000
                                         : (clock.remaining(nowMs()) + 999) / 1000;
    if (longTime())
      swprintf(s, 32, L"%02llu:%02llu:%02llu", secs / 3600, secs / 60 % 60, secs % 60);
    else
      swprintf(s, 32, L"%02llu:%02llu", secs / 60, secs % 60);
    return s;
  }
  const wchar_t *viewName() const {
    return cfg.view == CountdownView ? L"倒计时" : cfg.view == StopwatchView ? L"正向计时" : L"当前时间";
  }
  bool timerActionEnabled(int id) const {
    if (cfg.view != StopwatchView) return true;
    return id == 5 ? !stopwatch.running : id != 2 || stopwatch.elapsed(nowMs()) < Stopwatch::maximum;
  }
  std::wstring accessibleTime() {
    if (cfg.view == WallClockView) return L"当前时间，" + display();
    if (cfg.view == StopwatchView)
      return std::wstring(stopwatch.running ? L"正向计时，已用 " : L"正向计时，已暂停 ") + display();
    return clock.completed ? L"时间到，00:00" : std::wstring(clock.running ? L"剩余 " : L"已暂停 ") + display();
  }
  void setView(int view) {
    if (view < CountdownView || view > WallClockView) return;
    closeSettings();
    cfg.view = view;
    cfg.save();
    timer.focus = 4;
    resizeTimer();
    advance();
    notifyState();
  }
  HMENU viewMenu() {
    HMENU m = CreatePopupMenu();
    auto down = std::wstring(L"倒计时") + (clock.running ? L" · 运行中" : clock.completed ? L" · 时间到" : L" · 已暂停");
    auto up = std::wstring(L"正向计时") + (stopwatch.running ? L" · 运行中" : stopwatch.frozen ? L" · 已暂停" : L" · 未开始");
    AppendMenuW(m, MF_STRING, 130, down.c_str());
    AppendMenuW(m, MF_STRING, 131, up.c_str());
    AppendMenuW(m, MF_STRING, 132, L"当前时间");
    CheckMenuRadioItem(m, 130, 132, 130 + cfg.view, MF_BYCOMMAND);
    // Keep native menu navigation and text names, but paint the three choices ourselves.
    for (UINT id = 130; id <= 132; ++id) {
      MENUITEMINFOW item{sizeof(item)};
      item.fMask = MIIM_FTYPE;
      item.fType = MFT_OWNERDRAW | MFT_RADIOCHECK;
      SetMenuItemInfoW(m, id, FALSE, &item);
    }
    Surface sample;
    sample.prepare(1, 1);
    Paint p(sample, 1, dark);
    palette(p, true);
    if (viewMenuBrush) DeleteObject(viewMenuBrush);
    viewMenuBrush = CreateSolidBrush(RGB(p.bg.GetR(), p.bg.GetG(), p.bg.GetB()));
    MENUINFO info{sizeof(info)};
    info.fMask = MIM_BACKGROUND | MIM_STYLE;
    info.hbrBack = viewMenuBrush;
    info.dwStyle = MNS_NOCHECK;
    SetMenuInfo(m, &info);
    return m;
  }
  void drawViewMenu(const DRAWITEMSTRUCT &item) {
    Surface surface;
    if (!surface.prepare(item.rcItem.right - item.rcItem.left, item.rcItem.bottom - item.rcItem.top))
      return;
    {
      Paint p(surface, timer.dpi, dark);
      palette(p, true);
      const float width = surface.w / timer.dpi, height = surface.h / timer.dpi;
      p.g.Clear(Color(255, p.bg.GetR(), p.bg.GetG(), p.bg.GetB()));
      const int view = item.itemID - 130;
      const bool selected = view == cfg.view, hot = item.itemState & ODS_SELECTED;
      Color ink = p.ink, muted = p.muted;
      if (highContrast && hot) {
        p.rounded(3, 2, width - 6, height - 4, 5, p.accent);
        ink = muted = p.onAccent;
      } else {
        if (selected || hot) p.rounded(3, 2, width - 6, height - 4, 5, p.track);
        if (hot) p.rounded(3.5f, 2.5f, width - 7, height - 5, 5, p.muted, true);
      }
      const wchar_t *name = view == CountdownView ? L"倒计时" : view == StopwatchView ? L"正向计时" : L"当前时间";
      const wchar_t *status = view == CountdownView
          ? (clock.running ? L"运行中" : clock.completed ? L"时间到" : L"已暂停")
          : view == StopwatchView ? (stopwatch.running ? L"运行中" : stopwatch.frozen ? L"已暂停" : L"未开始")
          : L"本地时间";
      p.text(name, 12, 0, 76, height, 13, ink, false, 0);
      p.text(status, 90, 0, width - 120, height, 11, muted, false, 2);
      if (selected) {
        p.line(width - 23, height / 2, width - 20, height / 2 + 3, ink, 1.4f);
        p.line(width - 20, height / 2 + 3, width - 15, height / 2 - 3, ink, 1.4f);
      }
    }
    BitBlt(item.hDC, item.rcItem.left, item.rcItem.top, surface.w, surface.h,
           surface.dc, 0, 0, SRCCOPY);
  }
  void chooseView() {
    hideTip();
    HMENU m = viewMenu();
    RECT r; GetWindowRect(timer.h, &r);
    SetForegroundWindow(timer.h);
    int id = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, r.left, r.bottom + 2,
                           0, timer.h, nullptr);
    DestroyMenu(m);
    PostMessageW(timer.h, WM_NULL, 0, 0);
    if (id >= 130 && id <= 132) setView(id - 130);
    else { draw(timer); schedule(); }
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
    if (!keyMode || !w.focus || (w.kind == 1 && ((w.focus >= 10 && w.focus <= 12))))
      return;
    auto h = byId(w, w.focus);
    if (h)
      p.rounded(h->r.left - 2, h->r.top - 2, h->r.right - h->r.left + 4, h->r.bottom - h->r.top + 4,
                5, p.accent, true);
  }
  void palette(Paint &p, bool personalSurface) {
    if (cfg.personal && personalSurface) {
      // Personal themes color the entire surface; work keeps the neutral palette.
      constexpr unsigned backgrounds[] = {0xDCECE0, 0xF4E2CF, 0xE8E0F3};
      constexpr unsigned darkBackgrounds[] = {0x203B2F, 0x443222, 0x352A48};
      constexpr unsigned accents[] = {0x29563E, 0x774722, 0x604183};
      constexpr unsigned darkAccents[] = {0xBCE5C7, 0xF2D0A9, 0xDCC8F4};
      auto i = cfg.personalTheme;
      p.bg = rgb(dark ? darkBackgrounds[i] : backgrounds[i], 238);
      p.ink = p.accent = rgb(dark ? darkAccents[i] : accents[i]);
      p.muted = rgb(dark ? darkAccents[i] : accents[i], 220);
      p.onAccent = rgb(dark ? darkBackgrounds[i] : 0xFFFFFF);
      p.track = rgb(dark ? darkAccents[i] : accents[i], 23);
      p.edge = rgb(dark ? darkAccents[i] : accents[i], 48);
    }
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
      palette(p, w.kind < 2);
      float width = r.right / w.dpi, height = r.bottom / w.dpi;
      if (w.kind < 2 || w.kind == 4) {
        auto fill = p.bg;
        if (!w.glass || highContrast)
          fill = Color(255, p.bg.GetR(), p.bg.GetG(), p.bg.GetB());
        float radius = w.kind < 2 ? (cfg.personal ? 18.f : 8.f) : 9.f;
        p.rounded(.5f, .5f, width - 1, height - 1, radius, fill);
        p.rounded(.5f, .5f, width - 1, height - 1, radius, p.edge, true);
      }
      if (w.kind == 0) {
        const bool reveal = controls();
        const bool buttons = reveal && cfg.view != WallClockView;
        auto buttonFill = [&](int id, float x, float ww) {
          if (timerPressedId == id || timerHover == id)
            p.rounded(x, 4, ww, 28, 5, timerPressedId == id ? p.edge : p.track);
        };
        if (reveal) {
          hit(w, 4, 2, 2, 28, 32, (std::wstring(L"切换时间显示，当前：") + viewName()).c_str());
          buttonFill(4, 2, 28);
          if (cfg.view == WallClockView) {
            p.circle(14, 18, 6, p.muted, true, 1.3f);
            p.line(14, 18, 14, 14, p.muted, 1.3f);
            p.line(14, 18, 17, 20, p.muted, 1.3f);
          } else {
            int dy = cfg.view == CountdownView ? 1 : -1;
            p.line(14, 18 - 6 * dy, 14, 18 + 6 * dy, p.muted, 1.4f);
            p.line(10, 18 + 2 * dy, 14, 18 + 6 * dy, p.muted, 1.4f);
            p.line(18, 18 + 2 * dy, 14, 18 + 6 * dy, p.muted, 1.4f);
          }
          p.line(23, 17, 25, 19, p.muted, 1);
          p.line(25, 19, 27, 17, p.muted, 1);
        }
        if (buttons) {
          int cell = (int)(width - 32) / (musicPlaying ? 3 : 2);
          int toggleX = 32 + cell / 2, otherX = 32 + cell + cell / 2;
          bool up = cfg.view == StopwatchView;
          bool running = up ? stopwatch.running : clock.running;
          bool restart = !up && clock.completed;
          hit(w, 2, 32, 2, cell, 32, !timerActionEnabled(2) ? L"已达上限，请归零" : restart ? L"重新开始" : running ? L"暂停"
              : up && !stopwatch.frozen ? L"开始正向计时" : L"继续");
          hit(w, up ? 5 : 1, 32 + cell, 2, cell, 32, up ? L"归零" : L"设置倒计时");
          buttonFill(2, 32, cell);
          if (!up || !running) buttonFill(up ? 5 : 1, 32 + cell, cell);
          p.icon(up ? 10 : 1, otherX, height / 2,
                 up && running ? Color(100, p.ink.GetR(), p.ink.GetG(), p.ink.GetB()) : p.ink);
          p.icon(restart ? 10 : running ? 2 : 3, toggleX, height / 2,
                 timerActionEnabled(2) ? p.ink : Color(100, p.ink.GetR(), p.ink.GetG(), p.ink.GetB()));
        } else {
          const auto text = display();
          const float textX = reveal ? 30.f : 0.f;
          const float available = width - (reveal ? 32 : 0) - (musicPlaying ? 26 : 0);
          // Fit actual digit count without changing either window size.
          float size = std::min(28.f, available / (text.size() * .56f));
          float emphasis = 1;
          if (cfg.view == CountdownView && clock.completed && completionEmphasis && !reduced)
            emphasis = 1.04f;
          auto left = clock.remaining(nowMs());
          if (cfg.view == CountdownView && cfg.breath && !reduced && !dragging && clock.running && left <= 5000) {
            float wave = (1 - cosf(float(left % 1000) / 1000 * 6.283185f)) * .5f;
            emphasis = 1 + .035f * wave;
          }
          p.fittedTime(text, textX, 0, available, height, size, p.ink, emphasis);
        }
        if (musicPlaying) {
          float x = buttons ? 32 + 2.5f * ((int)(width - 32) / 3) : width - 14;
          p.rounded(x-11, height/2-11, 22, 22, 5, p.track);
          p.rounded(x-4, height/2-4, 8, 8, 1, p.ink);
          hit(w, 3, x-12, 2, 24, 32, L"停止音乐");
        }
        focusBorder(p, w);
      } else if (w.kind == 1) {
        p.text(L"倒计时", cfg.personal ? 94 : 18, 10, 100, 24, 13, p.ink, false,
               cfg.personal ? 1 : 0);
        hit(w, 9, 249, 8, 28, 28, L"关闭设置");
        p.icon(9, 263, 22, p.muted);
        p.rounded(12, 38, 264, 58, cfg.personal ? 14 : 8,
                  cfg.personal && !highContrast ? rgb(0xFFFFFF, dark ? 15 : 115) : p.track);
        const wchar_t *labels[] = {L"时", L"分", L"秒"};
        for (int i = 0; i < 3; i++) {
          int x = 18 + i * 86;
          wchar_t num[8];
          swprintf(num, 8, L"%02d", fields[i]);
          bool active = w.focus == 10 + i && GetFocus() == w.h;
          bool rolling = !reduced && reelStarted[i] && nowMs() - reelStarted[i] < reelDuration();
          if (active) {
            p.rounded(x + 10, 42, 56, 33, 5,
                      Color(dark ? 30 : 18, p.accent.GetR(), p.accent.GetG(), p.accent.GetB()));
            if (keyMode || highContrast)
              p.rounded(x + 10, 42, 56, 33, 5, p.ink, true);
          }
          if (rolling) {
            float t = std::min(1.f, float(nowMs() - reelStarted[i]) / reelDuration());
            float ease = 1.f - (1.f-t)*(1.f-t)*(1.f-t);
            float offset = ease * 36.f;
            wchar_t old[8]; swprintf(old, 8, L"%02d", reelFrom[i]);
            auto clip = p.g.Save();
            p.g.SetClip(RectF((float)x, 41, 76, 36));
            // Downward wheel input increases the value; digits follow that direction.
            p.text(old, x, 40 + reelDirection[i]*offset, 76, 36, 28,
                   Color((int)(255*(1-ease)), p.ink.GetR(), p.ink.GetG(), p.ink.GetB()), true);
            p.text(num, x, 40 - reelDirection[i]*(36-offset), 76, 36, 28,
                   Color((int)(255*ease), p.ink.GetR(), p.ink.GetG(), p.ink.GetB()), true);
            p.g.Restore(clip);
          } else {
            p.text(num, x, 40, 76, 36, 28, p.ink, true);
          }
          if (active && editing)
            p.line(x + 59, 46, x + 59, 72, p.accent, 1);
          p.text(labels[i], x, 77, 76, 16, 10, p.muted);
          hit(w, 10 + i, x, 40, 76, 53, i == 0 ? L"小时" : i == 1 ? L"分钟" : L"秒");
        }
        for (float x : {99.f, 185.f}) {
          p.circle(x, 54, 1.4f, p.muted);
          p.circle(x, 65, 1.4f, p.muted);
        }
        for (int i = 0; i < 4; i++) {
          int x = 18 + i * 51;
          bool on = selected(40 + i), down = pressedId == 40 + i;
          float inset = down ? 1.f : 0.f;
          Color fill = down ? p.accent
                            : on ? (highContrast ? p.accent : rgb(dark ? 0xFFFFFF : 0x000000, 45))
                                 : p.track;
          if (cfg.personal && on) fill = p.accent;
          p.rounded(x + inset, 108 + inset, 45 - 2 * inset, 28 - 2 * inset, cfg.personal ? 14 : 5, fill);
          if (on || down)
            p.rounded(x + inset, 108 + inset, 45 - 2 * inset, 28 - 2 * inset, cfg.personal ? 14 : 5, p.ink, true);
          p.text(std::to_wstring(PRESET_MINUTES[i]) + L"分", x, 108 + inset, 45, 28, 11,
                 down || (on && (highContrast || cfg.personal)) ? p.onAccent : p.ink);
          hit(w, 40 + i, x, 104, 45, 36, (std::to_wstring(PRESET_MINUTES[i]) + L"分钟").c_str());
        }
        if (cfg.personal)
          p.rounded(pressedId == 50 ? 236 : 234, pressedId == 50 ? 108 : 106,
                    pressedId == 50 ? 32 : 36, pressedId == 50 ? 28 : 32, 16, p.accent);
        else
          p.circle(252, 122, pressedId == 50 ? 14 : 16, p.accent);
        p.icon(3, 252, 122, p.onAccent);
        hit(w, 50, 234, 104, 36, 36, L"开始新的倒计时");
        if (settingError)
          p.text(L"至少 1 秒", 18, 146, 252, 20, 11, p.ink);
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
    const auto now = nowMs();
    const bool visible = IsWindowVisible(timer.h);
    scheduled = clock.delay(now, visible && cfg.view == CountdownView, controls(),
                            cfg.breath && !reduced && !dragging);
    auto sooner = [&](unsigned delay) {
      if (delay && (!scheduled || delay < scheduled)) scheduled = delay;
    };
    if (stopwatch.running) {
      auto elapsed = stopwatch.elapsed(now);
      sooner((unsigned)std::max<uint64_t>(1, Stopwatch::maximum - elapsed));
      if (visible && cfg.view == StopwatchView && !controls())
        sooner((unsigned)(1000 - elapsed % 1000));
    }
    if (visible && cfg.view == WallClockView) {
      SYSTEMTIME local; GetLocalTime(&local);
      sooner(60000 - local.wSecond * 1000 - local.wMilliseconds);
    }
    if (scheduled)
      SetTimer(timer.h, 1, scheduled, nullptr);
  }
  void advance() {
    ++wakes;
    auto now = nowMs();
    stopwatch.advance(now);
    if (clock.advance(now)) {
      lastLatency = now - clock.deadline;
      ++completions;
      notifyState();
      bool played = cfg.personal && cfg.music && playMusic();
      if (!played && cfg.sound) playTone();
      if (cfg.rest)
        showRest();
      else {
        // Surface a background completion so a silent floating reminder remains visible.
        if (cfg.view != CountdownView) {
          cfg.view = CountdownView;
          cfg.save();
          timer.focus = 4;
          notifyState();
        }
        if (!reduced && IsWindowVisible(timer.h) && !controls()) {
          completionEmphasis = true;
          SetTimer(timer.h, 7, 650, nullptr);
        }
      }
    }
    RECT bounds; GetWindowRect(timer.h, &bounds);
    if (bounds.right - bounds.left != (int)((longTime() ? TIMER_LONG_WIDTH : TIMER_SHORT_WIDTH) * timer.dpi))
      resizeTimer();
    draw(timer);
    schedule();
  }
  void start(int seconds) {
    if (restEnd)
      endRest();
    KillTimer(timer.h, 7);
    completionEmphasis = false;
    stopwatch.pause(nowMs());
    cfg.view = CountdownView;
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
    if (cfg.view == WallClockView) return;
    if (cfg.view == StopwatchView) {
      if (!stopwatch.running && stopwatch.frozen == Stopwatch::maximum) return;
      if (!stopwatch.running) {
        advance();
        if (clock.running) clock.toggle(nowMs());
        if (restEnd) endRest();
        cfg.view = StopwatchView;
        cfg.save();
      }
      stopwatch.toggle(nowMs());
      resizeTimer();
      notifyState(); draw(timer); schedule();
      return;
    }
    if (clock.completed) {
      start(cfg.seconds);
      return;
    }
    if (!clock.running) stopwatch.pause(nowMs());
    clock.toggle(nowMs());
    notifyState();
    draw(timer);
    schedule();
  }
  void showSettings() {
    resetReels();
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
    int ww = 288 * settings.dpi, hh = 156 * settings.dpi;
    if (settingError)
      hh += 20 * settings.dpi;
    SetWindowPos(settings.h, HWND_TOPMOST, 0, 0, ww, hh, SWP_NOMOVE | SWP_NOACTIVATE);
    shape(settings, ww, hh);
    clamp(settings.h);
  }
  unsigned reelDuration() const { return cfg.personal ? 220 : 160; }
  void resetReels() {
    for (auto &start : reelStarted) start = 0;
    wheelRemainder = 0;
    wheelField = -1;
  }
  void adjustField(int index, int steps) {
    int value = std::clamp(fields[index] + steps, 0, index == 0 ? 99 : 59);
    if (value == fields[index]) return;
    reelFrom[index] = fields[index];
    reelDirection[index] = value > fields[index] ? 1 : -1;
    fields[index] = value;
    reelStarted[index] = reduced ? 0 : nowMs();
    settings.focus = 10 + index;
    editing = false;
    if (settingError) { settingError = false; sizeSettings(); }
    if (!reduced) SetTimer(timer.h, 8, 16, nullptr);
    draw(settings);
    auto target = byId(settings, 10 + index);
    if (target) NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, settings.h, OBJID_CLIENT,
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
    KillTimer(timer.h, 9);
    if (tip.h)
      ShowWindow(tip.h, SW_HIDE);
  }
  void showTip(int id) {
    hideTip();
    if (!IsWindowVisible(settings.h) || id != 50) return;
    tipTitle = L"开始倒计时";
    tipDetail = L"快捷键：Enter";
    if (!tip.h) {
      make(tip, 244, 46, settings.h);
      SetWindowLongPtrW(tip.h, GWL_EXSTYLE,
                       GetWindowLongPtrW(tip.h, GWL_EXSTYLE) | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT);
    }
    tip.dpi = settings.dpi;
    RECT r;
    GetWindowRect(settings.h, &r);
    int width = (int)(244 * tip.dpi), height = (int)(46 * tip.dpi);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(settings.h, MONITOR_DEFAULTTONEAREST), &mi);
    int y = r.bottom + (int)(6 * tip.dpi);
    if (y + height > mi.rcWork.bottom) y = r.top - height - (int)(6 * tip.dpi);
    SetWindowPos(tip.h, HWND_TOPMOST, r.left + (int)(18 * tip.dpi),
                 y, width, height, SWP_NOACTIVATE);
    shape(tip, width, height);
    clamp(tip.h);
    ShowWindow(tip.h, SW_SHOWNOACTIVATE);
    draw(tip);
  }
  void updateTimerHover(POINT point) {
    auto target = at(timer, point.x, point.y);
    int id = target ? target->id : 0;
    if (id == timerHover) return;
    timerHover = id;
    hideTip();
    if (id) SetTimer(timer.h, 9, 650, nullptr);
    draw(timer);
  }
  void showTimerTip() {
    KillTimer(timer.h, 9);
    if (timerPointerDown || dragging || !IsWindowVisible(timer.h)) return;
    POINT point; GetCursorPos(&point);
    if (WindowFromPoint(point) != timer.h) return;
    ScreenToClient(timer.h, &point);
    auto target = at(timer, point.x, point.y);
    if (!target || target->id != timerHover) return;
    tipTitle = target->name;
    tipDetail = target->id == 4 ? L"选择倒计时、正向计时或当前时间"
                : target->id == 5 && stopwatch.running ? L"请先暂停，再归零"
                : target->id == 5 ? L"清除本轮已用时间"
                : target->id == 2 && !stopwatch.running && cfg.view == StopwatchView && clock.running
                    ? L"开始后将暂停倒计时" : L"点击执行；按住移动可拖动浮窗";
    if (!tip.h) {
      make(tip, 244, 46, timer.h);
      SetWindowLongPtrW(tip.h, GWL_EXSTYLE,
                       GetWindowLongPtrW(tip.h, GWL_EXSTYLE) | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT);
    }
    tip.dpi = timer.dpi;
    RECT r; GetWindowRect(timer.h, &r);
    SetWindowPos(tip.h, HWND_TOPMOST, r.left, r.bottom + (int)(6 * tip.dpi),
                 (int)(244 * tip.dpi), (int)(46 * tip.dpi), SWP_NOACTIVATE);
    shape(tip, (int)(244 * tip.dpi), (int)(46 * tip.dpi));
    clamp(tip.h);
    ShowWindow(tip.h, SW_SHOWNOACTIVATE);
    draw(tip);
  }
  void closeSettings() {
    resetReels();
    KillTimer(timer.h, 8);
    settingsOpened = 0;
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
    if (!visible) {
      resizeTimer();
      SetForegroundWindow(timer.h);
      draw(timer);
    }
    schedule();
  }
  void action(Win &w, int id) {
    hideTip();
    if (w.kind == 0) {
      if (!timerActionEnabled(id)) return;
      if (id == 1)
        showSettings();
      if (id == 2)
        toggle();
      if (id == 3) { stopAudio(); draw(timer); }
      if (id == 4) chooseView();
      if (id == 5 && cfg.view == StopwatchView && !stopwatch.running) {
        stopwatch.reset(); resizeTimer(); notifyState(); draw(timer); schedule();
      }
      return;
    }
    if (id == 9) {
      closeSettings();
      return;
    }
    if (id >= 10 && id <= 12) {
      resetReels();
      w.focus = id;
      editing = false;
      draw(w);
      return;
    }
    if (id >= 40 && id <= 43) {
      resetReels();
      fields[0] = fields[2] = 0;
      fields[1] = PRESET_MINUTES[id - 40];
      settingError = false;
    }
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
    sizeSettings();
    draw(w);
  }
  void playTone() { // Short notification, independent of the selected song.
    if (musicPlaying) return;
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
    fileAudio.close();
    PlaySoundW(nullptr, nullptr, 0);
    musicPlaying = musicPreview = false;
    soundData.clear();
    soundData.shrink_to_fit();
  }
  std::wstring songName() const {
    if (cfg.musicFile.empty()) return L"未选择歌曲";
    return cfg.musicFile.substr(cfg.musicFile.find_last_of(L"\\/") + 1);
  }
  void audioError() {
    MessageBoxW(IsWindowVisible(settings.h) ? settings.h : timer.h,
                L"无法播放这首歌曲。请检查文件是否仍在原位置，或选择可播放的 MP3 / WAV 文件。",
                L"轻计时", MB_OK | MB_ICONINFORMATION);
  }
  bool playMusic(bool preview = false) {
    if (!cfg.personal || cfg.musicFile.empty()) return false;
    if (musicPlaying && !preview) return true;
    stopAudio();
    audioFailed = !fileAudio.play(cfg.musicFile, timer.h);
    if (audioFailed) return false;
    musicPlaying = true;
    musicPreview = preview;
    draw(timer);
    return true;
  }
  void chooseMusic() {
    hideTip();
    wchar_t file[32768]{};
    if (cfg.musicFile.size() < 32768) wcscpy(file, cfg.musicFile.c_str());
    OPENFILENAMEW picker{sizeof(picker)};
    picker.hwndOwner = IsWindowVisible(settings.h) ? settings.h : timer.h;
    picker.lpstrFilter = L"音乐文件 (MP3 / WAV)\0*.mp3;*.wav\0MP3\0*.mp3\0WAV\0*.wav\0\0";
    picker.lpstrFile = file;
    picker.nMaxFile = 32768;
    picker.lpstrTitle = L"选择个人模式的歌曲";
    picker.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&picker)) return;
    stopAudio();
    cfg.musicFile = file;
    cfg.music = true;
    audioFailed = false;
    cfg.save();
    draw(timer);
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
    restStarted = nowMs();
    restEnd = restStarted + cfg.restSeconds * 1000;
    EnumDisplayMonitors(nullptr, nullptr, monitor, (LPARAM)this);
    if (!covers.empty())
      SetForegroundWindow(covers[0]->h);
    RegisterHotKey(timer.h, 1, 0x4000, VK_ESCAPE);
    SetTimer(timer.h, 2, reduced ? 200 : 50, nullptr);
  }
  void endRest() {
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
  static INT_PTR CALLBACK reminderDialog(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto app = reinterpret_cast<App *>(GetWindowLongPtrW(h, DWLP_USER));
    if (msg == WM_INITDIALOG) {
      app = reinterpret_cast<App *>(lp);
      SetWindowLongPtrW(h, DWLP_USER, lp);
      for (int i = 1; i <= 5; ++i) {
        auto label = std::to_wstring(i) + L" 秒";
        SendDlgItemMessageW(h, 1001, CB_ADDSTRING, 0, (LPARAM)label.c_str());
      }
      SendDlgItemMessageW(h, 1001, CB_SETCURSEL, app->cfg.restSeconds - 1, 0);
      RECT owner, dialog;
      GetWindowRect(GetParent(h), &owner);
      GetWindowRect(h, &dialog);
      SetWindowPos(h, HWND_TOP, owner.left + (owner.right-owner.left-dialog.right+dialog.left)/2,
                   owner.bottom + 8, 0, 0, SWP_NOSIZE);
      app->clamp(h);
      return TRUE;
    }
    if (msg == WM_COMMAND && app) {
      if (LOWORD(wp) == IDOK) {
        int choice = (int)SendDlgItemMessageW(h, 1001, CB_GETCURSEL, 0, 0);
        if (choice < 0 || choice > 4) return TRUE;
        app->cfg.restSeconds = choice + 1;
        app->cfg.save();
        EndDialog(h, IDOK);
        return TRUE;
      }
      if (LOWORD(wp) == IDCANCEL) {
        EndDialog(h, IDCANCEL);
        return TRUE;
      }
    }
    return FALSE;
  }
  void menu() {
    HMENU m = CreatePopupMenu();
    HMENU modes = CreatePopupMenu();
    AppendMenuW(modes, MF_STRING, 113, L"工作");
    AppendMenuW(modes, MF_STRING, 114, L"个人");
    CheckMenuRadioItem(modes, 113, 114, cfg.personal ? 114 : 113, MF_BYCOMMAND);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)modes, cfg.personal ? L"使用模式 · 个人" : L"使用模式 · 工作");

    HMENU sounds = CreatePopupMenu();
    AppendMenuW(sounds, MF_STRING, 118, L"浮窗提醒");
    AppendMenuW(sounds, MF_STRING, 119, L"全屏倒数");
    CheckMenuRadioItem(sounds, 118, 119, cfg.rest ? 119 : 118, MF_BYCOMMAND);
    auto duration = L"全屏时长：" + std::to_wstring(cfg.restSeconds) + L" 秒…";
    AppendMenuW(sounds, MF_STRING | (cfg.rest ? 0 : MF_GRAYED), 120, duration.c_str());
    AppendMenuW(sounds, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sounds, MF_STRING | (cfg.sound ? MF_CHECKED : 0), 108, L"提示音");
    AppendMenuW(sounds, MF_SEPARATOR, 0, nullptr);
    std::wstring title = audioFailed ? L"歌曲无法播放，请重新选择" : songName();
    // Ampersands in filenames are text, not menu keyboard accelerators.
    for (size_t i=0; (i=title.find(L'&', i)) != std::wstring::npos; i+=2) title.insert(i, 1, L'&');
    if (!cfg.personal) title = L"歌曲仅限个人模式";
    AppendMenuW(sounds, MF_STRING | MF_GRAYED, 0, title.c_str());
    AppendMenuW(sounds, MF_STRING | (cfg.personal ? 0 : MF_GRAYED), 109, L"选择歌曲…");
    UINT songDisabled = (!cfg.personal || cfg.musicFile.empty()) ? MF_GRAYED : 0;
    AppendMenuW(sounds, MF_STRING | songDisabled,
                110, musicPlaying ? L"停止播放" : L"试听整首歌曲");
    AppendMenuW(sounds, MF_STRING | songDisabled | (cfg.music ? MF_CHECKED : 0), 111, L"到时播放歌曲");
    AppendMenuW(sounds, MF_STRING | songDisabled, 112, L"清除歌曲");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)sounds, L"提醒");

    HMENU appearance = CreatePopupMenu();
    AppendMenuW(appearance, MF_STRING | (windowPinned() ? MF_CHECKED : 0), 104, L"浮窗置顶");
    AppendMenuW(appearance, MF_STRING | (windowGlass() ? MF_CHECKED : 0), 105, L"背景磨砂");
    AppendMenuW(appearance, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(appearance, MF_STRING | MF_GRAYED, 0, L"个人配色");
    AppendMenuW(appearance, MF_STRING, 115, L"雾绿");
    AppendMenuW(appearance, MF_STRING, 116, L"暖杏");
    AppendMenuW(appearance, MF_STRING, 117, L"雾紫");
    CheckMenuRadioItem(appearance, 115, 117, 115 + cfg.personalTheme, MF_BYCOMMAND);
    AppendMenuW(m, MF_POPUP | (cfg.personal ? 0 : MF_GRAYED), (UINT_PTR)appearance, L"窗口外观");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)viewMenu(), (std::wstring(L"时间显示 · ") + viewName()).c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 107, L"退出");
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(timer.h);
    int id = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, timer.h, nullptr);
    DestroyMenu(m);
    PostMessageW(timer.h, WM_NULL, 0, 0);
    if (id >= 130 && id <= 132) setView(id - 130);
    if (id == 113 || id == 114) {
      cfg.personal = id == 114;
      if (!cfg.personal) stopAudio();
      cfg.save();
      hideTip();
      timer.glass = backdrop(timer.h, windowGlass());
      settings.glass = backdrop(settings.h, windowGlass());
      resizeTimer();
      RECT settingsRect;
      GetClientRect(settings.h, &settingsRect);
      shape(settings, settingsRect.right, settingsRect.bottom);
      draw(timer);
      draw(settings);
      notifyState();
      NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, settings.h, OBJID_CLIENT, CHILDID_SELF);
      NOTIFYICONDATAW n{sizeof(n)};
      n.hWnd = timer.h; n.uID = 1; n.uFlags = NIF_TIP;
      wcscpy_s(n.szTip, cfg.personal ? L"轻计时 · 个人模式" : L"轻计时 · 工作模式");
      Shell_NotifyIconW(NIM_MODIFY, &n);
    }
    if (cfg.personal && id >= 115 && id <= 117) {
      cfg.personalTheme = id - 115;
      cfg.save();
      draw(timer);
      draw(settings);
    }
    if (cfg.personal && id == 104) {
      cfg.pin = !cfg.pin;
      resizeTimer();
      cfg.save();
    }
    if (cfg.personal && id == 105) {
      cfg.glass = !cfg.glass;
      cfg.save();
      timer.glass = backdrop(timer.h, windowGlass());
      settings.glass = backdrop(settings.h, windowGlass());
      draw(timer);
      draw(settings);
    }
    if (id == 118 || id == 119) {
      cfg.rest = id == 119;
      cfg.breath = !cfg.rest;
      cfg.save();
      draw(timer);
      schedule();
    }
    if (id == 120 && cfg.rest) {
      hideTip();
      DialogBoxParamW(instance, MAKEINTRESOURCEW(102), timer.h, reminderDialog, (LPARAM)this);
    }
    if (id == 108) {
      cfg.sound = !cfg.sound;
      cfg.save();
    }
    if (cfg.personal && id == 109) chooseMusic();
    if (cfg.personal && id == 110) {
      if (musicPlaying) stopAudio();
      else if (cfg.personal && !playMusic(true)) audioError();
      draw(timer);
    }
    if (cfg.personal && id == 111 && !cfg.musicFile.empty()) {
      cfg.music = !cfg.music;
      cfg.save();
    }
    if (cfg.personal && id == 112) {
      stopAudio();
      cfg.musicFile.clear();
      cfg.music = false;
      audioFailed = false;
      cfg.save();
      draw(timer);
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
    auto displayed = display();
    s << "{\"tipVisible\":" << (tip.h && IsWindowVisible(tip.h)) << ",\"pid\":" << GetCurrentProcessId() << ",\"running\":" << clock.running
      << ",\"completed\":" << clock.completed << ",\"remainingMs\":" << clock.remaining(nowMs())
      << ",\"view\":" << cfg.view << ",\"elapsedMs\":" << stopwatch.elapsed(nowMs())
      << ",\"stopwatchRunning\":" << stopwatch.running
      << ",\"display\":\"" << std::string(displayed.begin(), displayed.end()) << "\""
      << ",\"completionEmphasis\":" << completionEmphasis
      << ",\"controls\":" << controls()
      << ",\"hoverArmed\":" << hoverArmed << ",\"pointerDown\":" << timerPointerDown
      << ",\"dragging\":" << dragging
      << ",\"settingsVisible\":" << (IsWindowVisible(settings.h) != 0)
      << ",\"restActive\":" << (restEnd != 0) << ",\"covers\":" << covers.size()
      << ",\"restSeconds\":" << cfg.restSeconds << ",\"breath\":" << cfg.breath
      << ",\"sound\":" << cfg.sound << ",\"personal\":" << cfg.personal
      << ",\"hasSong\":" << !cfg.musicFile.empty()
      << ",\"audioPositionMs\":" << fileAudio.status(MCI_STATUS_POSITION)
      << ",\"audioLengthMs\":" << fileAudio.status(MCI_STATUS_LENGTH)
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
    else if (op == L"view") {
      int view; if (in >> view) setView(view);
    }
    else if (test && op == L"elapsed") {
      uint64_t ms;
      if (in >> ms) { stopwatch.pause(nowMs()); stopwatch.frozen = std::min(ms, Stopwatch::maximum); resizeTimer(); draw(timer); schedule(); }
    }
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
      timer.glass = backdrop(timer.h, windowGlass());
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
    case WM_MEASUREITEM: {
      auto item = reinterpret_cast<MEASUREITEMSTRUCT *>(lp);
      if (item && item->CtlType == ODT_MENU && item->itemID >= 130 && item->itemID <= 132) {
        item->itemWidth = (UINT)lroundf(184 * timer.dpi);
        item->itemHeight = (UINT)lroundf(36 * timer.dpi);
        return TRUE;
      }
      break;
    }
    case WM_DRAWITEM: {
      auto item = reinterpret_cast<DRAWITEMSTRUCT *>(lp);
      if (item && item->CtlType == ODT_MENU && item->itemID >= 130 && item->itemID <= 132) {
        drawViewMenu(*item);
        return TRUE;
      }
      break;
    }
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
    case MM_MCINOTIFY:
      if (fileAudio.ended(wp, lp)) {
        audioFailed = wp == MCI_NOTIFY_FAILURE;
        stopAudio();
        draw(timer);
      }
      return 0;
    case WM_TIMER:
      if (h == timer.h) {
        if (wp == 9) showTimerTip();
        if (wp == 8) {
          auto now = nowMs();
          if (settingsOpened && now - settingsOpened >= 180) settingsOpened = 0;
          bool rolling = false;
          for (auto &start : reelStarted) {
            if (start && (reduced || now - start >= reelDuration())) start = 0;
            rolling = rolling || start != 0;
          }
          if (reduced || !IsWindowVisible(settings.h) || (!settingsOpened && !rolling)) {
            KillTimer(h, 8);
            settingsOpened = 0;
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
          stopAudio();
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
      if (w->kind < 2 && LOWORD(lp) == HTCLIENT) {
        POINT pos;
        GetCursorPos(&pos);
        ScreenToClient(h, &pos);
        auto target = at(*w, pos.x, pos.y);
        SetCursor(LoadCursorW(nullptr, target && ((target->id >= 10 && target->id <= 12)) ? IDC_IBEAM
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
          updateTimerHover({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
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
          ScreenToClient(h, &p);
          updateTimerHover(p);
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
        hideTip();
        timerHover = 0;
        cancelHover();
        hover = false;
        draw(timer);
        schedule();
      }
      return 0;
    case WM_LBUTTONDOWN: {
      if (w->kind == 0) {
        auto hit = at(*w, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        hideTip();
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
        draw(timer);
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
        auto hit = at(*w, p.x, p.y);
        ClientToScreen(h, &p);
        int id = hit && hit->id == timerPressedId && !pastDragThreshold(p) ? hit->id : 0;
        timerPointerDown = false;
        timerPressedId = 0;
        ReleaseCapture();
        if (id)
          action(*w, id);
        draw(timer);
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
    case WM_MOUSEWHEEL: {
      if (w->kind != 1) break;
      POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(h, &point);
      auto target = at(*w, point.x, point.y);
      if (!target || target->id < 10 || target->id > 12) {
        wheelRemainder = 0; wheelField = -1;
        return 0;
      }
      hideTip();
      int index = target->id - 10;
      if (wheelField != index) { wheelRemainder = 0; wheelField = index; }
      wheelRemainder += GET_WHEEL_DELTA_WPARAM(wp);
      int steps = wheelRemainder / WHEEL_DELTA;
      wheelRemainder %= WHEEL_DELTA;
      if (steps) adjustField(index, -steps);
      return 0;
    }
    case WM_CHAR:
      if (w->kind == 1 && w->focus >= 10 && w->focus <= 12) {
        resetReels();
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
      hideTip();
      keyMode = true;
      if (w->kind == 1 && w->focus >= 10 && w->focus <= 12) {
        if (wp == L'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
          editing = false;
          draw(*w);
          return 0;
        }
        if (wp == VK_DELETE) {
          resetReels();
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
          draw(*w);
          std::vector<int> ids;
          for (auto &hit : w->hits)
            if (timerActionEnabled(hit.id)) ids.push_back(hit.id);
          if (ids.empty()) return 0;
          auto it = std::find(ids.begin(), ids.end(), w->focus);
          int index = it == ids.end() ? -1 : (int)(it - ids.begin());
          int delta = GetKeyState(VK_SHIFT) & 0x8000 ? -1 : 1;
          w->focus = ids[(index + delta + (int)ids.size()) % ids.size()];
          draw(*w);
          schedule();
          NotifyWinEvent(EVENT_OBJECT_FOCUS, h, OBJID_CLIENT,
                         (LONG)(byId(*w, w->focus) - w->hits.data()) + 1);
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
          NotifyWinEvent(EVENT_OBJECT_FOCUS, h, OBJID_CLIENT,
                         (LONG)((std::find(ids.begin(), ids.end(), w->focus) - ids.begin()) + 1));
        }
        return 0;
      }
      if (wp == VK_RETURN || wp == VK_SPACE) {
        if (w->kind == 0) {
          draw(*w);
          action(*w, byId(*w, w->focus) ? w->focus : cfg.view == WallClockView ? 4 : 2);
        } else if (w->kind == 1) {
          action(*w, ((w->focus >= 10 && w->focus <= 12)) ? 50 : w->focus);
        }
        return 0;
      }
      if (w->kind == 1 && w->focus >= 10 && w->focus <= 12 && (wp == VK_UP || wp == VK_DOWN)) {
        adjustField(w->focus - 10, wp == VK_UP ? 1 : -1);
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
      timer.glass = backdrop(timer.h, windowGlass());
      settings.glass = backdrop(settings.h, windowGlass());
      draw(timer);
      draw(settings);
      schedule();
      return 0;
    case WM_TIMECHANGE:
      draw(timer);
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
        stopAudio();
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
          ? app->accessibleTime()
          : L"轻计时设置";
  if (!h) name = (app->cfg.personal ? L"个人模式，" : L"工作模式，") + name;
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
  return S_FALSE;
}
HRESULT Accessible::get_accRole(VARIANT v, VARIANT *p) {
  VariantInit(p);
  p->vt = VT_I4;
  auto h = accHit(hwnd, v);
  int id = h ? h->id : 0;
  p->lVal = !id                                              ? ROLE_SYSTEM_GROUPING
            : hwnd == app->timer.h && id == 4               ? ROLE_SYSTEM_BUTTONMENU
            : (id >= 10 && id <= 12)              ? ROLE_SYSTEM_SPINBUTTON
            : (id >= 40 && id <= 43)                          ? ROLE_SYSTEM_RADIOBUTTON
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
  if (hwnd == app->timer.h && h && !app->timerActionEnabled(h->id))
    p->lVal |= STATE_SYSTEM_UNAVAILABLE;
  if (hwnd == app->timer.h && h && h->id == 4)
    p->lVal |= STATE_SYSTEM_HASPOPUP;
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
  if (hwnd != app->settings.h || !h || h->id < 10 || h->id > 12 || !s)
    return E_INVALIDARG;
  wchar_t *end = nullptr;
  long n = wcstol(s, &end, 10);
  if (!end || *end || n < 0 || n > (h->id == 10 ? 99 : 59))
    return E_INVALIDARG;
  app->fields[h->id - 10] = (int)n;
  app->resetReels();
  app->draw(app->settings);
  return S_OK;
}

static int selftest() {
  Stopwatch sw;
  sw.toggle(100);
  if (sw.elapsed(450) != 350) return 10;
  sw.pause(450);
  if (sw.elapsed(50000) != 350) return 11;
  sw.toggle(50000);
  if (sw.elapsed(50650) != 1000) return 12;
  sw.reset();
  if (sw.elapsed(50650) != 1000) return 13;
  sw.advance(Stopwatch::maximum + 50000);
  if (sw.running || sw.frozen != Stopwatch::maximum) return 14;
  sw.reset();
  if (sw.elapsed(999999999) != 0) return 15;
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
