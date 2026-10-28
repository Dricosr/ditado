#include "history.h"
#include "config.h"
#include "inject.h"
#include "log.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {
std::mutex g_mu;
std::wstring g_today;

constexpr int ID_LIST = 301, ID_COPY = 302, ID_CLOSE = 303, ID_STATUS = 304;

std::string U(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, 0);
  if (n) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}
std::wstring W(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0);
  if (n) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

std::wstring Day() {
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t b[16];
  wsprintfW(b, L"%04u%02u%02u", t.wYear, t.wMonth, t.wDay);
  return b;
}
std::wstring Dir() {
  std::wstring d = Config::Dir() + L"\\history";
  CreateDirectoryW(d.c_str(), nullptr);
  return d;
}

struct Item {
  std::wstring quando;
  std::wstring texto;
  std::string jobId;
  std::string modelo;
};

std::vector<Item> g_itens;

std::vector<Item> LoadDay(const std::wstring& day) {
  std::vector<Item> out;
  std::lock_guard lk(g_mu);
  std::ifstream f(Dir() + L"\\" + day + L".jsonl", std::ios::binary);
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty()) continue;
    json j = json::parse(line, nullptr, false);
    if (j.is_discarded() || !j.contains("texto") || !j["texto"].is_string()) continue;
    Item it;
    it.texto = W(j["texto"].get<std::string>());
    if (it.texto.empty()) continue;
    it.quando = j.contains("quando") && j["quando"].is_string() ? W(j["quando"].get<std::string>()) : L"";
    it.jobId = j.contains("jobId") && j["jobId"].is_string() ? j["jobId"].get<std::string>() : "";
    it.modelo = j.contains("modelo") && j["modelo"].is_string() ? j["modelo"].get<std::string>() : "";
    out.push_back(std::move(it));
  }
  return out;
}

void CopiarSelecionado(HWND list, HWND status) {
  int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
  if (sel < 0) { SetWindowTextW(status, L"Selecione um registro."); return; }
  INT_PTR idx = (INT_PTR)SendMessageW(list, LB_GETITEMDATA, sel, 0);
  if (idx < 0 || idx >= (INT_PTR)g_itens.size()) return;
  if (inject::Copiar(g_itens[(size_t)idx].texto))
    SetWindowTextW(status, L"Copiado para o clipboard.");
  else
    SetWindowTextW(status, L"Não consegui copiar.");
}

LRESULT CALLBACK Wnd(HWND h, UINT m, WPARAM w, LPARAM l) {
  static HWND list = nullptr, status = nullptr;
  switch (m) {
    case WM_CREATE: {
      HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
      g_today = Day();
      g_itens = LoadDay(g_today);
      std::wstring cab = L"Histórico de " + g_today.substr(0, 4) + L"-" + g_today.substr(4, 2) +
                         L"-" + g_today.substr(6, 2) + L" — " + std::to_wstring(g_itens.size()) + L" texto(s)";
      HWND lbl = CreateWindowExW(0, L"STATIC", cab.c_str(), WS_CHILD | WS_VISIBLE,
                                 12, 10, 740, 20, h, nullptr, nullptr, nullptr);
      SendMessageW(lbl, WM_SETFONT, (WPARAM)f, TRUE);
      list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                             12, 36, 740, 440, h, (HMENU)(INT_PTR)ID_LIST, nullptr, nullptr);
      SendMessageW(list, WM_SETFONT, (WPARAM)f, TRUE);
      // carrega do mais novo para o mais antigo
      for (int i = (int)g_itens.size() - 1; i >= 0; --i) {
        std::wstring texto = g_itens[(size_t)i].quando + L"  —  " + g_itens[(size_t)i].texto;
        if (texto.size() > 180) texto = texto.substr(0, 180) + L"…";
        int idx = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)texto.c_str());
        SendMessageW(list, LB_SETITEMDATA, idx, (LPARAM)i);
      }
      if (g_itens.empty()) {
        int idx = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"Nenhum texto transcrito hoje.");
        SendMessageW(list, LB_SETITEMDATA, idx, (LPARAM)-1);
      }
      status = CreateWindowExW(0, L"STATIC", L"Selecione um registro e clique em Copiar.",
                               WS_CHILD | WS_VISIBLE, 12, 490, 460, 20, h, (HMENU)(INT_PTR)ID_STATUS, nullptr, nullptr);
      SendMessageW(status, WM_SETFONT, (WPARAM)f, TRUE);
      auto Btn = [&](const wchar_t* t, int id, int x, int cx) {
        HWND b = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                 x, 484, cx, 26, h, (HMENU)(INT_PTR)id, nullptr, nullptr);
        SendMessageW(b, WM_SETFONT, (WPARAM)f, TRUE);
      };
      Btn(L"Copiar", ID_COPY, 560, 90);
      Btn(L"Fechar", ID_CLOSE, 660, 90);
      return 0;
    }
    case WM_COMMAND:
      if (HIWORD(w) == BN_CLICKED && LOWORD(w) == ID_COPY) {
        CopiarSelecionado(list, status);
        return 0;
      }
      if (HIWORD(w) == BN_CLICKED && LOWORD(w) == ID_CLOSE) { DestroyWindow(h); return 0; }
      if (HIWORD(w) == LBN_DBLCLK) { CopiarSelecionado(list, status); return 0; }
      return 0;
    case WM_CLOSE:
      DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(h, m, w, l);
  }
}
}  // namespace

namespace history {
void Add(const std::wstring& texto, const std::string& jobId, const std::string& modelo, double duracaoS) {
  if (texto.empty()) return;
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t day[16], hora[16];
  wsprintfW(day, L"%04u%02u%02u", t.wYear, t.wMonth, t.wDay);
  wsprintfW(hora, L"%02u:%02u:%02u", t.wHour, t.wMinute, t.wSecond);
  json j;
  j["quando"] = U(hora);
  j["texto"] = U(texto);
  j["jobId"] = jobId;
  j["modelo"] = modelo;
  j["duracaoS"] = duracaoS;
  std::lock_guard lk(g_mu);
  std::wstring path = Dir() + L"\\" + std::wstring(day) + L".jsonl";
  std::ofstream f(path, std::ios::app | std::ios::binary);
  if (f) { f << j.dump() << "\n"; f.flush(); }
}

void Show() {
  static bool once = false;
  if (!once) {
    WNDCLASSEXW wc{
        .cbSize = sizeof(wc),
        .lpfnWndProc = Wnd,
        .hInstance = GetModuleHandleW(nullptr),
        .hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_SHARED),
        .lpszClassName = L"ditado.historico",
        .hIconSm = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, 0)};
    RegisterClassExW(&wc);
    once = true;
  }
  HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ditado.historico", L"ditado — histórico do dia",
                           WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, 790, 540,
                           nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!h) {
    lg::Error("janela de histórico falhou: " + std::to_string((int)GetLastError()));
    return;
  }
  ShowWindow(h, SW_SHOW);
  UpdateWindow(h);
  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0)) {
    if (!IsDialogMessageW(h, &m)) {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
  }
}
}  // namespace history
