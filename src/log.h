#pragma once
#include <string>

namespace lg {
enum class Level { Info, Warn, Error };
void Init(const std::wstring& dir);
void Write(Level, const std::string& msg);
inline void Info(const std::string& m) { Write(Level::Info, m); }
inline void Warn(const std::string& m) { Write(Level::Warn, m); }
inline void Error(const std::string& m) { Write(Level::Error, m); }
}  // namespace lg

