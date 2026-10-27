#include "settings.h"
#include <windows.h>
#include <format>
#include <vector>
#include "log.h"

namespace {
// Diálogo de opções construído na mão, em memória — zero .rc, zero frameworks de UI.
// Sem DS_SETFONT: fontes dos controles ficam com o padrão do sistema (GUI font do SO).
// DLGTEMPLATE: style, exstyle, cdItems, x, y, cx, cy, menu, classe, título; itens idem.

std::string A2U(const wchar_t* w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w, (int)lstrlenW(w), nullptr, 0, nullptr, nullptr);
  std::string s(n, 0);
  if (n) WideCharToMultiByte(CP_UTF8, 0, w, (int)lstrlenW(w), s.data(), n, nullptr, nullptr);
  return s;
}
std::wstring U2W(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0);
  if (n) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}
void PushStr(std::vector<WORD>& v, const wchar_t* s) { while (*s) v.push_back(*s++); v.push_back(0); }

struct Builder {
  std::vector<WORD> w;
  void Pad() { if (w.size() % 2) w.push_back(0); }  // DLGITEMTEMPLATE exige fronteira DWORD
  void Header(WORD nItems, DWORD style, const wchar_t* title) {
    w.insert(w.end(), {(WORD)style, (WORD)(style >> 16), 0, 0, nItems, 6, 4, 248, 156});  // 9 WORDs
    w.push_back(0);        // menu: nenhum
    w.push_back(0x0000);   // classe: a predefinida de diálogo (1 WORD — átomo 0x80 deu erro 1407)
    PushStr(w, title);     // título
    Pad();                 // alinha o primeiro item
  }
  void Item(DWORD style, WORD id, WORD cls, const wchar_t* title, int x, int y, int cx, int cy) {
    w.insert(w.end(), {(WORD)style, (WORD)(style >> 16), 0, 0, (WORD)x, (WORD)y, (WORD)cx, (WORD)cy, id});  // 9 WORDs
    w.push_back(0xFFFF); w.push_back(cls);  // classe do controle (0x80 button, 0x81 edit, 0x82 static, 0x84 combo)
    if (title && *title) PushStr(w, title);
    else { w.push_back(0xFFFF); w.push_back(0x00A0); }  // sem rótulo: átomo "static"
    Pad();  // item seguinte também em fronteira DWORD
  }
};

INT_PTR CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_INITDIALOG: {
      SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)l);
      auto* c = (Config*)l;
      HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
      EnumChildWindows(h, [](HWND ch, LPARAM f) -> BOOL { SendMessageW(ch, WM_SETFONT, (WPARAM)f, TRUE); return TRUE; },
                       (LPARAM)f);
      SetDlgItemTextW(h, 101, c->url.c_str());
      SetDlgItemTextW(h, 102, U2W(c->tecla).c_str());
      for (auto* m2 : {"small", "turbo", "large-v3"})
        SendDlgItemMessageW(h, 103, CB_ADDSTRING, 0, (LPARAM)U2W(m2).c_str());
      SendDlgItemMessageW(h, 103, CB_SELECTSTRING, (WPARAM)(INT)-1, (LPARAM)U2W(c->modelo).c_str());
      SendDlgItemMessageW(h, 104, BM_SETCHECK, c->autostart ? BST_CHECKED : BST_UNCHECKED, 0);
      return TRUE;
    }
    case WM_COMMAND:
      if (LOWORD(w) == IDOK && HIWORD(w) == BN_CLICKED) {
        auto* c = (Config*)GetWindowLongPtrW(h, GWLP_USERDATA);
        wchar_t b[512];
        GetDlgItemTextW(h, 101, b, 500); c->url = b;
        GetDlgItemTextW(h, 102, b, 500); c->tecla = A2U(b);
        SendMessageW(GetDlgItem(h, 103), CB_GETLBTEXT,
                     (WPARAM)(INT)SendMessageW(GetDlgItem(h, 103), CB_GETCURSEL, 0, 0), (LPARAM)b);
        c->modelo = A2U(b);
        c->autostart = SendDlgItemMessageW(h, 104, BM_GETCHECK, 0, 0) == BST_CHECKED;
        EndDialog(h, IDOK);
        return TRUE;
      }
      if (LOWORD(w) == IDCANCEL) { EndDialog(h, IDCANCEL); return TRUE; }
      return FALSE;
  }
  return FALSE;
}

HINSTANCE Own() {
  wchar_t b[MAX_PATH];
  GetModuleFileNameW(nullptr, b, MAX_PATH);
  return (HINSTANCE)GetModuleHandleW(b);
}
}  // namespace

namespace settings {
void Show(HWND owner, Config& cfg) {
  Builder b;
  b.Header(9, DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_CENTER, L"ditado — opções");
  const DWORD E = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER;
  const DWORD L = WS_CHILD | WS_VISIBLE | SS_LEFT;
  b.Item(L, 0, 0x0082, L"Endereço do gate", 8, 8, 90, 8);
  b.Item(E, 101, 0x0081, L"", 8, 18, 230, 12);
  b.Item(L, 0, 0x0082, L"Tecla (ex.: ctrl+shift+space)", 8, 36, 140, 8);
  b.Item(E, 102, 0x0081, L"", 8, 46, 230, 12);
  b.Item(L, 0, 0x0082, L"Modelo", 8, 64, 40, 8);
  b.Item(WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 103, 0x0084, L"", 8, 74, 100, 60);
  b.Item(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 104, 0x0080, L"Iniciar com o Windows", 120, 66, 110, 10);
  b.Item(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK, 0x0080, L"OK", 8, 138, 50, 14);
  b.Item(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL, 0x0080, L"Cancelar", 64, 138, 50, 14);

  Config copy = cfg;
  INT_PTR r = DialogBoxIndirectParamW(Own(), (DLGTEMPLATE*)b.w.data(), owner, Proc, (LPARAM)&copy);
  if (r == IDOK) cfg = copy;
}
}  // namespace settings
