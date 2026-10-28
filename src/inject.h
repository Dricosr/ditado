#pragma once
#include "config.h"
#include <string>

namespace inject {
// Digita no campo ativo. unicode: SendInput KEYEVENTF_UNICODE. area: clipboard + Ctrl+V, restaurando depois.
void Texto(const Config& cfg, const std::wstring& t);
// Coloca o texto no clipboard (usado pelo histórico).
bool Copiar(const std::wstring& t);
}  // namespace inject
