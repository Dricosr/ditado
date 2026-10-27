#pragma once
#include <string>

namespace lg {
enum class Level { Info, Warn, Error };
void Init(const std::wstring& dir);
void Write(Level, const std::string& msg);
void Verbose(bool on);              // log local detalhado ligado/desligado (config "log_local")
bool Verbose();                     // tudo que entrou, foi enviado e recebido fica registrado
void Audio(const std::string& wav); // salva o WAV gravado em <dir>\audio\ (só se Verbose)
inline void Info(const std::string& m) { Write(Level::Info, m); }
inline void Warn(const std::string& m) { Write(Level::Warn, m); }
inline void Error(const std::string& m) { Write(Level::Error, m); }
}  // namespace lg

