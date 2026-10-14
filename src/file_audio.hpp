#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <string>

// Stream the local file through the Windows media driver. Never load a whole song into RAM.
class FileAudio {
  MCIDEVICEID device = 0;
public:
  DWORD error = 0;
  ~FileAudio() { close(); }
  void close() {
    if (device) {
      mciSendCommandW(device, MCI_CLOSE, 0, 0);
      device = 0;
    }
  }
  bool play(const std::wstring &path, HWND callback) {
    close();
    MCI_OPEN_PARMSW open{};
    open.lpstrDeviceType = L"mpegvideo";
    open.lpstrElementName = path.c_str();
    error = mciSendCommandW(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_ELEMENT,
                           reinterpret_cast<DWORD_PTR>(&open));
    if (error) return false;
    device = open.wDeviceID;
    MCI_SET_PARMS set{};
    set.dwTimeFormat = MCI_FORMAT_MILLISECONDS;
    error = mciSendCommandW(device, MCI_SET, MCI_SET_TIME_FORMAT, reinterpret_cast<DWORD_PTR>(&set));
    MCI_PLAY_PARMS play{};
    play.dwCallback = reinterpret_cast<DWORD_PTR>(callback);
    if (!error)
      error = mciSendCommandW(device, MCI_PLAY, MCI_NOTIFY, reinterpret_cast<DWORD_PTR>(&play));
    if (error) close();
    return !error;
  }
  DWORD status(DWORD item) const {
    if (!device) return 0;
    MCI_STATUS_PARMS value{};
    value.dwItem = item;
    return mciSendCommandW(device, MCI_STATUS, MCI_STATUS_ITEM,
                          reinterpret_cast<DWORD_PTR>(&value)) ? 0 : static_cast<DWORD>(value.dwReturn);
  }
  bool ended(WPARAM reason, LPARAM id) const {
    return device && static_cast<MCIDEVICEID>(id) == device &&
           (reason == MCI_NOTIFY_FAILURE ||
            (reason == MCI_NOTIFY_SUCCESSFUL && status(MCI_STATUS_MODE) == MCI_MODE_STOP));
  }
};
