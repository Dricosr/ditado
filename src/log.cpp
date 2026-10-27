#include "log.h"
#include <windows.h>
#include <fstream>
#include <mutex>

namespace {
std::mutex g_mu;
std::wstring g_dir;  // ...\ditado\logs

std::string Utf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring Day() {
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t b[16];
  wsprintfW(b, L"%04u%02u%02u", t.wYear, t.wMonth, t.wDay);
  return b;
}
}  // namespace

namespace log {
void Init(const std::wstring& dir) {
  std::lock_guard g(g_mu);
  g_dir = dir + L"\\logs";
  CreateDirectoryW(g_dir.c_str(), nullptr);
}

void Write(Level lvl, const std::string& msg) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t ts[32];
  wsprintfW(ts, L"%02u:%02u:%02u.%03u ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);

  std::string line = Utf8(ts) + (lvl == Level::Error ? "ERRO " : lvl == Level::Warn ? "WARN " : "INFO ") +
                     msg + "\n";
  std::wstring path = g_dir + L"\\app-" + Day() + L".log";

  std::lock_guard g(g_mu);
  if (g_dir.empty()) return;
  std::ofstream f(path, std::ios::app | std::ios::binary);
  if (f) f.write(line.data(), (std::streamsize)line.size());
}
}  // namespace log
