#pragma once
#include "config.h"
#include <windows.h>

namespace settings {
// Janela modal de opções; OK grava no cfg recebido (quem chama decide Save).
void Show(HWND owner, Config& cfg);
}  // namespace settings
