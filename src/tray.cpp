#include "tray.h"
#include "log.h"
#include <shellapi.h>

namespace {
const UINT WM_TRAY = WM_APP + 1;
NOTIFYICONDATAW g_n{};
tray::Actions g_a;

std::wstring W(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

LRESULT CALLBACK Wnd(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_TRAY && (l == WM_LBUTTONUP || l == WM_LBUTTONDBLCLK)) {
    if (g_a.OpenSettings) g_a.OpenSettings();
    return 0;
  }
  if (m == WM_TRAY && l == WM_RBUTTONUP) {
    POINT p;
    GetCursorPos(&p);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Opções…");
    AppendMenuW(menu, MF_STRING, 2, L"Testar microfone");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"Iniciar com o Windows");
    AppendMenuW(menu, MF_STRING, 4, L"Sair");
    SetForegroundWindow(h);
    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y, 0, h, nullptr);
    DestroyMenu(menu);
    switch (id) {
      case 1: if (g_a.OpenSettings) g_a.OpenSettings(); break;
      case 2: if (g_a.TestarMic) g_a.TestarMic(); break;
      case 3: if (g_a.ToggleAutostart) g_a.ToggleAutostart(); break;
      case 4: if (g_a.Quit) g_a.Quit(); break;
    }
    return 0;
  }
  if (m == WM_DESTROY) {
    Shell_NotifyIconW(NIM_DELETE, &g_n);
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}
}  // namespace

namespace tray {
bool Init(HINSTANCE hi, const Actions& actions) {
  g_a = actions;
  WNDCLASSEXW wc{.cbSize = sizeof(wc), .lpfnWndProc = Wnd, .hInstance = hi,
                 .lpszClassName = L"ditado.wnd"};
  static bool once = false;
  if (!once) {
    RegisterClassExW(&wc);
    once = true;
  }
  HWND h = CreateWindowExW(0, wc.lpszClassName, L"ditado", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hi, nullptr);
  if (!h) return false;

  g_n.cbSize = sizeof(g_n);
  g_n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  g_n.uCallbackMessage = WM_TRAY;
  g_n.hWnd = h;
  g_n.uID = 1;
  g_n.hIcon = LoadIconW(nullptr, IDI_APPLICATION);  // temporário: ícone próprio no P5
  wsprintfW(g_n.szTip, L"ditado");
  if (!Shell_NotifyIconW(NIM_ADD, &g_n)) {
    log::Error("Shell_NotifyIcon(NIM_ADD) falhou: " + std::to_string(GetLastError()));
    return false;
  }
  return true;
}

void SetTooltip(const std::wstring& t) {
  lstrcpynW(g_n.szTip, (L"ditado — " + t).c_str(), 128);
  g_n.uFlags = NIF_TIP;
  Shell_NotifyIconW(NIM_MODIFY, &g_n);
}

void Balloon(const std::wstring& title, const std::wstring& msg) {
  lstrcpynW(g_n.szInfoTitle, title.c_str(), 64);
  lstrcpynW(g_n.szInfo, msg.c_str(), 128);
  g_n.uFlags = NIF_INFO;
  g_n.dwInfoFlags = NIIF_INFO;
  Shell_NotifyIconW(NIM_MODIFY, &g_n);
}
}  // namespace tray
