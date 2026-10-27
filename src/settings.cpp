#include "settings.h"
#include "log.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys.h>
#include <propsys.h>
#include <string>
#include <format>

namespace {
// Janela de opções feita com CreateWindowExW + controles filhos (sem DLGTEMPLATE — formato que
// já provou funcionar neste app: tray.cpp usa CreateWindowExW e funciona ✓).

std::wstring U2W(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}
std::string W2U(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, 0);
  if (n) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}
std::wstring Txt(HWND h) {
  wchar_t b[512];
  GetWindowTextW(h, b, 500);
  return b;
}

LRESULT CALLBACK Wnd(HWND h, UINT m, WPARAM w, LPARAM l) {
  static Config* cfg = nullptr;
  static HWND e101, e102, e103, c104, c105, c106;
  static HFONT fnt = nullptr;
  switch (m) {
    case WM_CREATE: {
      cfg = (Config*)((CREATESTRUCTW*)l)->lpCreateParams;
      HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
      fnt = f;
      auto Lbl = [&](const wchar_t* t, int x, int y) {
        HWND e = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE, x, y, 260, 16, h, nullptr, nullptr, nullptr);
        SendMessageW(e, WM_SETFONT, (WPARAM)f, TRUE);
        return e;
      };
      auto Edit = [&](int id, int x, int y, int cx) {
        HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER,
                                 x, y, cx, 22, h, (HMENU)(INT_PTR)id, nullptr, nullptr);
        SendMessageW(e, WM_SETFONT, (WPARAM)f, TRUE);
        return e;
      };
      Lbl(L"Endereço do gate", 12, 14);
      e101 = Edit(101, 12, 32, 300);
      Lbl(L"Tecla (ex.: ctrl+shift+space)", 12, 64);
      e102 = Edit(102, 12, 82, 300);
      Lbl(L"Modelo", 330, 14);
      e103 = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
                             330, 32, 130, 160, h, (HMENU)103, nullptr, nullptr);
      SendMessageW(e103, WM_SETFONT, (WPARAM)f, TRUE);
      c104 = CreateWindowExW(0, L"BUTTON", L"Iniciar com o Windows", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                             12, 112, 180, 22, h, (HMENU)104, nullptr, nullptr);
      SendMessageW(c104, WM_SETFONT, (WPARAM)f, TRUE);
      c106 = CreateWindowExW(0, L"BUTTON", L"Log local (dev)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                             200, 112, 120, 22, h, (HMENU)106, nullptr, nullptr);
      SendMessageW(c106, WM_SETFONT, (WPARAM)f, TRUE);
      Lbl(L"Dispositivo de captura", 330, 64);
      c105 = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
                             330, 82, 170, 200, h, (HMENU)105, nullptr, nullptr);
      SendMessageW(c105, WM_SETFONT, (WPARAM)f, TRUE);
      // preenche lista: todas as capturas ativas (nome amigável → id); seleciona a atual
      {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        IMMDeviceEnumerator* en = nullptr; IMMDeviceCollection* coll = nullptr;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en)) &&
            SUCCEEDED(en->EnumAudioEndpoints(eCapture, 1 /*DEVICE_ACTIVE*/, &coll))) {
          std::wstring def; IMMDevice* dd = nullptr;
          if (SUCCEEDED(en->GetDefaultAudioEndpoint(eCapture, eConsole, &dd)) && dd) {
            wchar_t* di = nullptr; dd->GetId(&di); def = di; CoTaskMemFree(di); dd->Release();
          }
          UINT n = 0; coll->GetCount(&n);
          for (UINT i = 0; i < n; i++) {
            IMMDevice* d = nullptr;
            if (FAILED(coll->Item(i, &d))) continue;
            wchar_t* id = nullptr; d->GetId(&id);
            IPropertyStore* ps = nullptr; wchar_t nm[128] = L"(sem nome)";
            if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps))) {
              PROPVARIANT pv; PropVariantInit(&pv);
              if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR) wcscpy_s(nm, pv.pwszVal);
              PropVariantClear(&pv); ps->Release();
            }
            int idx = (int)SendMessageW(c105, CB_ADDSTRING, 0, (LPARAM)nm);
            SendMessageW(c105, CB_SETITEMDATA, idx, (LPARAM)_wcsdup(id));  // cópia nossa: o id original é liberado abaixo
            std::wstring sel = cfg->dispositivo.empty() ? def : U2W(cfg->dispositivo);
            if (!sel.empty() && sel == std::wstring(id)) SendMessageW(c105, CB_SETCURSEL, idx, 0);
            CoTaskMemFree(id); d->Release();
          }
        }
      }
      for (auto* m2 : {"small", "turbo", "large-v3"})
        SendMessageW(e103, CB_ADDSTRING, 0, (LPARAM)U2W(m2).c_str());
      SetWindowTextW(e101, cfg->url.c_str());
      SetWindowTextW(e102, U2W(cfg->tecla).c_str());
      SendMessageW(e103, CB_SELECTSTRING, -1, (LPARAM)U2W(cfg->modelo).c_str());
      SendMessageW(c104, BM_SETCHECK, cfg->autostart ? BST_CHECKED : BST_UNCHECKED, 0);
      SendMessageW(c106, BM_SETCHECK, cfg->log_local ? BST_CHECKED : BST_UNCHECKED, 0);
      auto Btn = [&](const wchar_t* t, int id, int x) {
        HWND e = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | (id == IDOK ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
                                 x, 150, 76, 24, h, (HMENU)(INT_PTR)id, nullptr, nullptr);
        SendMessageW(e, WM_SETFONT, (WPARAM)f, TRUE);
        return e;
      };
      Btn(L"OK", IDOK, 330);
      Btn(L"Cancelar", IDCANCEL, 414);
      return 0;
    }
    case WM_COMMAND:
      if (HIWORD(w) == BN_CLICKED) {
        if (LOWORD(w) == IDOK) {
          cfg->url = Txt(e101);
          cfg->tecla = W2U(Txt(e102));
          wchar_t b[64];
          SendMessageW(e103, CB_GETLBTEXT, SendMessageW(e103, CB_GETCURSEL, 0, 0), (LPARAM)b);
          cfg->modelo = W2U(b);
          cfg->autostart = SendMessageW(c104, BM_GETCHECK, 0, 0) == BST_CHECKED;
          cfg->log_local = SendMessageW(c106, BM_GETCHECK, 0, 0) == BST_CHECKED;
          {
            int sel = (int)SendMessageW(c105, CB_GETCURSEL, 0, 0);
            wchar_t* id = (wchar_t*)SendMessageW(c105, CB_GETITEMDATA, sel, 0);
            cfg->dispositivo = id ? W2U(id) : std::string();
          }
          DestroyWindow(h);  // fecha; quem chamou lê o cfg salvo em Show
          return 0;
        }
        if (LOWORD(w) == IDCANCEL) { DestroyWindow(h); return 0; }
      }
      return 0;
    case WM_CLOSE:
      DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}
}  // namespace

namespace settings {
void Show(HWND owner, Config& cfg) {
  static bool once = false;
  if (!once) {
    WNDCLASSEXW wc{.cbSize = sizeof(wc), .lpfnWndProc = Wnd, .hInstance = GetModuleHandleW(nullptr), .hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_SHARED), .lpszClassName = L"ditado.opcoes", .hIconSm = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, 0)};
    RegisterClassExW(&wc);
    once = true;
  }
  HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ditado.opcoes", L"ditado — opções",
                           WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, 520, 240, owner, nullptr, GetModuleHandleW(nullptr), &cfg);
  if (!h) {
    lg::Error("janela de opções falhou: " + std::to_string((int)GetLastError()));
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
  // OK já escreveu em cfg (ponteiro via lpCreateParams); Cancelar não.
}
}  // namespace settings
