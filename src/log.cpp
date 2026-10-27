#include "log.h"
#include <windows.h>
#include <fstream>
#include <mutex>
#include <vector>
#include <algorithm>

namespace {
std::mutex g_mu;
std::wstring g_base;  // ...\ditado
bool g_verbose = false;

std::string Utf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  if (n) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
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

namespace lg {
void Init(const std::wstring& dir) {
  std::lock_guard g(g_mu);
  g_base = dir;
  CreateDirectoryW((g_base + L"\\logs").c_str(), nullptr);
}

void Verbose(bool on) { std::lock_guard g(g_mu); g_verbose = on; }
bool Verbose() { std::lock_guard g(g_mu); return g_verbose; }

void Write(Level lvl, const std::string& msg) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t ts[32];
  wsprintfW(ts, L"%02u:%02u:%02u.%03u ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);

  std::string line = Utf8(ts) + (lvl == Level::Error ? "ERRO " : lvl == Level::Warn ? "WARN " : "INFO ") +
                     msg + "\n";
  std::wstring path;
  {
    std::lock_guard g(g_mu);
    if (g_base.empty()) return;
    path = g_base + L"\\logs\\app-" + Day() + L".log";
  }
  std::ofstream f(path, std::ios::app | std::ios::binary);
  if (f) f.write(line.data(), (std::streamsize)line.size());
}

void Audio(const std::string& wav) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t name[64];
  wsprintfW(name, L"\\%04u%02u%02u-%02u%02u%02u.wav", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
  std::wstring dir, path;
  {
    std::lock_guard g(g_mu);
    if (!g_verbose || g_base.empty()) return;
    dir = g_base + L"\\audio";
    path = dir + name;
  }
  CreateDirectoryW(dir.c_str(), nullptr);
  {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0;
    WriteFile(f, wav.data(), (DWORD)wav.size(), &wr, nullptr);
    CloseHandle(f);
  }
  // teto: mantém só os 30 arquivos mais novos (nomes ordenáveis por data-hora ✓)
  std::vector<std::wstring> todos;
  WIN32_FIND_DATAW fd;
  HANDLE hq = FindFirstFileW((dir + L"\\*.wav").c_str(), &fd);
  if (hq != INVALID_HANDLE_VALUE) {
    do todos.push_back(fd.cFileName); while (FindNextFileW(hq, &fd));
    FindClose(hq);
  }
  std::sort(todos.begin(), todos.end());
  for (size_t i = 0; i + 30 < todos.size(); i++) DeleteFileW((dir + L"\\" + todos[i]).c_str());
}
}  // namespace lg
