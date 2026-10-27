#include <windows.h>
#include <cstdio>
#include <vector>
#pragma comment(lib, "user32.lib")
void PS(std::vector<WORD>& v, const wchar_t* s) { while (*s) v.push_back(*s++); v.push_back(0); }
void Pad(std::vector<WORD>& v) { if (v.size() % 2) v.push_back(0); }  // DLGITEM precisa começar em fronteira DWORD
INT_PTR C(HWND h, UINT m, WPARAM w, LPARAM) {
  if (m == WM_INITDIALOG) { EndDialog(h, 1); return 1; }
  if (m == WM_COMMAND) { EndDialog(h, 1); return 1; }
  return 0;
}
void T(const char* name, std::vector<WORD> w) {
  INT_PTR r = DialogBoxIndirectParamW(GetModuleHandleW(0), (DLGTEMPLATE*)w.data(), 0, C, 0);
  printf("%-12s r=%lld err=%u\n", name, (long long)r, GetLastError());
}
int main() {
  {  // A: zero itens
    std::vector<WORD> w{(WORD)(WS_POPUP | WS_CAPTION | DS_CENTER), (WORD)(WS_POPUP >> 16), 0, 0, 0, 6, 4, 248, 156, 0};
    w.push_back(0x0000);                       // classe: "usa a classe predefinida"
    PS(w, L"teste"); Pad(w);
    T("A sem itens", w);
  }
  {  // B: um static — item alinhado em DWORD
    std::vector<WORD> w{(WORD)(WS_POPUP | WS_CAPTION | DS_CENTER), (WORD)(WS_POPUP >> 16), 0, 0, 1, 6, 4, 248, 156, 0};
    w.push_back(0x0000); PS(w, L"teste"); Pad(w);
    w.insert(w.end(), {(WORD)(WS_CHILD | WS_VISIBLE | SS_LEFT), 0, 0, 0, 8, 8, 120, 8, 1});
    w.push_back(0xFFFF); w.push_back(0x0082); PS(w, L"oi");
    T("B com static", w);
  }
  {  // C: com DS_SETFONT + fonte (como será no app real)
    std::vector<WORD> w{(WORD)(WS_POPUP | WS_CAPTION | DS_CENTER | DS_SETFONT), (WORD)(WS_POPUP >> 16), 0, 0, 1, 6, 4, 248, 156};
    w.push_back(0x0000); PS(w, L"teste"); w.push_back(9); PS(w, L"Segoe UI"); Pad(w);
    w.insert(w.end(), {(WORD)(WS_CHILD | WS_VISIBLE | SS_LEFT), 0, 0, 0, 8, 8, 120, 8, 1});
    w.push_back(0xFFFF); w.push_back(0x0082); PS(w, L"oi");
    T("C com fonte", w);
  }
}
