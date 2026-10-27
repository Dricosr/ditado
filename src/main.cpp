#include <windows.h>
#include <string>

#include "config.h"
#include "hotkey.h"
#include "log.h"
#include "settings.h"
#include "tray.h"
#include "version.h"

namespace {
Config g_cfg;
std::wstring ExeDir() {
  wchar_t b[MAX_PATH];
  GetModuleFileNameW(nullptr, b, MAX_PATH);
  std::wstring p(b);
  return p.substr(0, p.find_last_of(L'\\'));
}
std::string U(const std::wstring& w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, 0);
  if (n) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}
void SetAutostart(bool on) {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET, &k)) return;
  if (on) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --autostart";
    RegSetValueExW(k, DITADO_APP, 0, REG_SZ, (BYTE*)cmd.c_str(), (DWORD)cmd.size() * 2);
  } else {
    RegDeleteValueW(k, DITADO_APP);
  }
  RegCloseKey(k);
}
}  // namespace

int APIENTRY wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
  HANDLE inst = CreateMutexW(nullptr, TRUE, L"Local\\ditado.instancia");
  if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;  // instância única

  SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e) -> LONG {
    log::Error("exceção não tratada: " + std::format("{:08X}", (unsigned)e->ExceptionRecord->ExceptionCode));
    return EXCEPTION_EXECUTE_HANDLER;  // encerra logado em vez de calado
  });

  g_cfg = Config::Load();
  log::Init(Config::Dir());
  log::Info(std::format("ditado {} iniciado — gate={}", DITADO_VERSION, U(g_cfg.url)));

  hotkey::Start(g_cfg.tecla, [](const KeyEvent& e) {
    if (e.down) log::Info("tecla: vk=" + std::format("{}", e.vk));  // P1 liga com a máquina de estados
  });

  tray::Init(hi, {
      .OpenSettings = [&] {
        Config c = g_cfg;
        tray::SetTooltip(L"configurando…");
        settings::Show(nullptr, c);
        if (c.url != g_cfg.url || c.tecla != g_cfg.tecla || c.modelo != g_cfg.modelo || c.autostart != g_cfg.autostart) {
          bool combo = c.tecla != g_cfg.tecla;
          bool start = c.autostart != g_cfg.autostart;
          g_cfg = c;
          g_cfg.Save();
          if (combo) hotkey::SetCombo(g_cfg.tecla);
          if (start) SetAutostart(g_cfg.autostart);
          log::Info("config salva");
        }
        tray::SetTooltip(L"pronto");
      },
      .TestarMic = [] { log::Info("P1: teste de microfone ainda não ligado"); },
      .ToggleAutostart = [] {
        g_cfg.autostart = !g_cfg.autostart;
        g_cfg.Save();
        SetAutostart(g_cfg.autostart);
      },
      .Quit = [] { PostMessageW(FindWindowW(L"ditado.wnd", nullptr), WM_CLOSE, 0, 0); },
  });
  tray::SetTooltip(L"pronto");

  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0)) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  hotkey::Stop();
  return (int)m.wParam;
}
