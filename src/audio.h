#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace audio {
// WASAPI shared capture → PCM s16 mono 16 kHz (o formato que o Whisper do gate quer).
void Prepare(const std::wstring& deviceId /* vazio = padrão do sistema */);  // monta o client uma vez
bool Start(const std::wstring& deviceId /* vazio = padrão do sistema */);
void Stop();  // para o capture; o que foi gravado fica no buffer
std::vector<int16_t> Flush();  // devolve o gravado e limpa
std::string ToWav(const std::vector<int16_t>& pcm);   // bytes WAV
std::string ToWav(const std::wstring& utf16);         // p/ testes: voz sintética vira WAV 16k mono
}  // namespace audio
