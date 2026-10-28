#pragma once
namespace som {
// Feedback curto: pop ao começar/terminar a gravação e um loop discreto enquanto transcreve.
void PopIn();
void PopOut();
void Running(bool on);  // liga/desliga o loop de "processando"
void StopAll();
}  // namespace som
