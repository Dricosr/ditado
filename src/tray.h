#pragma once
#include <windows.h>
#include <functional>
#include <string>

namespace tray {
struct Actions {
  std::function<void()> OpenSettings;
  std::function<void()> TestarMic;
  std::function<void()> ToggleAutostart;
  std::function<void()> Quit;
};
bool Init(HINSTANCE hi, const Actions& actions);
void SetTooltip(const std::wstring& tooltip);
void Balloon(const std::wstring& title, const std::wstring& msg);
}  // namespace tray
