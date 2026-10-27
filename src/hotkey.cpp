#include "hotkey.h"
#include "log.h"
#include <cctype>
#include <map>
#include <vector>

namespace {
struct Parsed {
  std::vector<UINT> vks;  // teclas do combo (não-modificadoras)
  bool need_ctrl = false, need_shift = false, need_alt = false, need_win = false;
};
Parsed g_p;
std::function<void(const KeyEvent&)> g_cb;
HHOOK g_hook = nullptr;

UINT Vk1(const std::string& t) {  // "a"->A, "f1"->VK_F1, "space"...
  if (t == "space") return VK_SPACE;
  if (t == "enter") return VK_RETURN;
  if (t == "tab") return VK_TAB;
  if (t == "esc") return VK_ESCAPE;
  if (t == "ctrl") return VK_CONTROL;
  if (t == "shift") return VK_SHIFT;
  if (t == "alt") return VK_MENU;
  if (t == "win") return VK_LWIN;
  if (t.size() == 1) return (UINT)toupper((unsigned char)t[0]);
  if (t.size() == 2 && t[0] == 'f' && isdigit(t[1])) return VK_F1 + (t[1] - '1');
  if (t.size() == 3 && t[0] == 'f' && isdigit(t[1]) && isdigit(t[2]))
    return VK_F10 + (t[1] - '0') * 10 + (t[2] - '0') - 10;
  return 0;
}

bool Mod(UINT vk) { return vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN; }
UINT Base(UINT vk) {  // L/R → código base, p/ o hook bater com o combo configurado
  switch (vk) {
    case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
    case VK_LSHIFT:   case VK_RSHIFT:   return VK_SHIFT;
    case VK_LMENU:    case VK_RMENU:    return VK_MENU;
    case VK_LWIN:     case VK_RWIN:     return VK_LWIN;
    default: return vk;
  }
}

Parsed Parse(const std::string& combo) {
  Parsed p;
  size_t i = 0;
  while (i < combo.size()) {
    size_t j = combo.find('+', i);
    std::string t = combo.substr(i, j == std::string::npos ? j : j - i);
    for (auto& c : t) c = (char)tolower(c);
    if (t == "ctrl") p.need_ctrl = true;
    else if (t == "shift") p.need_shift = true;
    else if (t == "alt") p.need_alt = true;
    else if (t == "win") p.need_win = true;
    else if (UINT vk = Vk1(t)) p.vks.push_back(vk);
    else lg::Warn("combo: trecho desconhecido '" + t + "'");
    if (j == std::string::npos) break;
    i = j + 1;
  }
  return p;
}

bool Down(UINT vk) { return (GetAsyncKeyState((int)vk) & 0x8000) != 0; }

LRESULT CALLBACK LL(int code, WPARAM w, LPARAM l) {
  if (code == HC_ACTION && g_cb) {
    auto& k = *(KBDLLHOOKSTRUCT*)l;
    bool down = !(k.flags & LLKHF_UP);
    bool mine = false;
    for (UINT v : g_p.vks) {  // vks só contém a tecla não-modificadora do combo; vkCode de letra já é base ✓
      if (v == k.vkCode || v == Base(k.vkCode)) mine = true;
    }
    bool mods_ok = (!g_p.need_ctrl || Down(VK_CONTROL)) && (!g_p.need_shift || Down(VK_SHIFT)) &&
                   (!g_p.need_alt || Down(VK_MENU)) && (!g_p.need_win || Down(VK_LWIN));
    if (mine && mods_ok)
      g_cb({k.vkCode, Down(VK_CONTROL), Down(VK_SHIFT), Down(VK_MENU), Down(VK_LWIN), down});
  }
  return CallNextHookEx(nullptr, code, w, l);
}
}  // namespace

namespace hotkey {
bool Start(const std::string& combo, const std::function<void(const KeyEvent&)>& cb) {
  g_cb = cb;
  g_p = Parse(combo);
  g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LL, GetModuleHandleW(nullptr), 0);
  if (!g_hook) lg::Error("SetWindowsHookEx(WH_KEYBOARD_LL) falhou: " + std::to_string(GetLastError()));
  return g_hook != nullptr;
}
void Stop() {
  if (g_hook) UnhookWindowsHookEx(g_hook);
  g_hook = nullptr;
  g_cb = nullptr;
}
bool SetCombo(const std::string& combo) {
  bool was = g_hook != nullptr;
  auto cb = g_cb;
  if (was) Stop();
  return !was ? (g_cb = cb, false) : Start(combo, cb);
}
}  // namespace hotkey

