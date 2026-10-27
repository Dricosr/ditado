#include <windows.h>
#include <cstdio>
LRESULT CALLBACK LL(int c, WPARAM w, LPARAM l) {
  if (c == HC_ACTION) { auto& k = *(KBDLLHOOKSTRUCT*)l; printf("hook vk=%u flags=%02X scan=%02X\n", k.vkCode, k.flags, k.scanCode); fflush(stdout); }
  return CallNextHookEx(0, c, w, l);
}
int main() {
  SetConsoleOutputCP(65001);
  HHOOK h = SetWindowsHookExW(WH_KEYBOARD_LL, LL, GetModuleHandleW(0), 0);
  printf("hook=%p\n", (void*)h); fflush(stdout);
  INPUT evs[6]{};
  int i = 0;
  auto add = [&](WORD vk, DWORD f) { INPUT x{}; x.type = INPUT_KEYBOARD; x.ki.wVk = vk; x.ki.dwFlags = f; evs[i++] = x; };
  add(VK_CONTROL, 0); add(VK_SHIFT, 0); add(VK_SPACE, 0);            // segura
  add(VK_SPACE, KEYEVENTF_KEYUP); add(VK_SHIFT, KEYEVENTF_KEYUP); add(VK_CONTROL, KEYEVENTF_KEYUP);  // solta
  for (int k = 0; k < 3; k++) { MSG m; while (PeekMessageW(&m, 0, 0, 0, PM_REMOVE)) DispatchMessageW(&m); Sleep(300); }
  UINT ok = SendInput(6, evs, sizeof(INPUT));
  printf("sendinput=%u err=%u\n", ok, (unsigned)GetLastError()); fflush(stdout);
  for (int k = 0; k < 20; k++) { MSG m; while (PeekMessageW(&m, 0, 0, 0, PM_REMOVE)) DispatchMessageW(&m); Sleep(100); }
  printf("fim\n");
}
