#include "inject.h"
#include "log.h"
#include <windows.h>
#include <thread>

namespace {
void Key(WORD vk, bool up) {
  INPUT i{};
  i.type = INPUT_KEYBOARD;
  i.ki.wVk = vk;
  i.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
  i.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (vk == 0 ? KEYEVENTF_UNICODE : 0);
  if (vk) i.ki.dwFlags |= (i.ki.wScan ? KEYEVENTF_UNICODE & 0 : 0);  // vk scan já tratado pelo SO
  SendInput(1, &i, sizeof i);
}
}  // namespace

namespace inject {
void Texto(const Config& cfg, const std::wstring& t) {
  if (t.empty()) return;
  if (cfg.metodo_insercao == "area") {  // clipboard + Ctrl+V, restaurando o conteúdo anterior
    HGLOBAL old = nullptr;
    if (IsClipboardFormatAvailable(CF_UNICODETEXT) && OpenClipboard(nullptr)) {
      HANDLE c = GetClipboardData(CF_UNICODETEXT);
      if (c) {
        void* p = GlobalLock(c);
        if (p) {
          size_t n = GlobalSize(c);
          old = GlobalAlloc(GMEM_MOVEABLE, n);
          if (old) {
            CopyMemory(GlobalLock(old), p, n);
            GlobalUnlock(old);
          }
          GlobalUnlock(c);
        }
      }
      CloseClipboard();
    }
    if (OpenClipboard(nullptr)) {
      EmptyClipboard();
      HGLOBAL m = GlobalAlloc(GMEM_MOVEABLE, (t.size() + 1) * 2);
      if (m) {
        CopyMemory(GlobalLock(m), t.data(), (t.size() + 1) * 2);
        GlobalUnlock(m);
        SetClipboardData(CF_UNICODETEXT, m);
      }
      CloseClipboard();
      Key(VK_CONTROL, false);
      Key('V', false);
      Key('V', true);
      Key(VK_CONTROL, true);
      if (old && OpenClipboard(nullptr)) {
        EmptyClipboard();
        SetClipboardData(CF_UNICODETEXT, old);
        CloseClipboard();
      }
    }
    return;
  }
  for (wchar_t c : t) {  // unicode: cada code unit vira um evento (surrogate pairs = 2 eventos ✓)
    INPUT i{};
    i.type = INPUT_KEYBOARD;
    i.ki.wScan = (WORD)c;
    i.ki.dwFlags = KEYEVENTF_UNICODE;
    SendInput(1, &i, sizeof i);
    i.ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(1, &i, sizeof i);
  }
}

bool Copiar(const std::wstring& t) {
  if (t.empty() || !OpenClipboard(nullptr)) return false;
  EmptyClipboard();
  const size_t bytes = (t.size() + 1) * sizeof(wchar_t);
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
  bool ok = false;
  if (h) {
    void* p = GlobalLock(h);
    if (p) {
      CopyMemory(p, t.c_str(), bytes);
      GlobalUnlock(h);
      ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;  // o clipboard toma posse do HGLOBAL
    }
    if (!ok) GlobalFree(h);
  }
  CloseClipboard();
  return ok;
}
}  // namespace inject
