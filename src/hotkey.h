#pragma once
#include <windows.h>
#include <functional>
#include <string>

struct KeyEvent {
  UINT vk = 0;
  bool ctrl = false, shift = false, alt = false, win = false;
  bool down = false;  // false = soltou
};

namespace hotkey {
// combo: "ctrl+shift+space" | "win+." | "ctrl+alt+z"... — nomes e letras/dígitos
bool Start(const std::string& combo, const std::function<void(const KeyEvent&)>& cb);
void Stop();
bool SetCombo(const std::string& combo);  // muda sem reiniciar o app
}  // namespace hotkey
