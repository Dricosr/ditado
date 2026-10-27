#include "config.h"
#include "log.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

namespace {
std::string U(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}
std::wstring W(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}
std::string Enc(const std::string& s) {  // percent-encoding p/ query string
  static const char* h = "0123456789ABCDEF";
  std::string o;
  for (unsigned char c : s) {
    if (isalnum(c) || strchr("-_.~", c)) o += (char)c;
    else { o += '%'; o += h[c >> 4]; o += h[c & 15]; }
  }
  return o;
}
template <class T> void Get(const json& j, const char* k, T& out) {
  if (j.contains(k) && j[k].is_binary() == false && !j[k].is_null()) {
    try { out = j[k].get<T>(); } catch (...) {}
  }
}
void GetB(const json& j, const char* k, bool& out) { if (j.contains(k) && j[k].is_boolean()) out = j[k]; }
}  // namespace

std::wstring Config::Dir() {
  wchar_t appdata[MAX_PATH];
  SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata);
  std::wstring dir = std::wstring(appdata) + L"\\ditado";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

Config Config::Load() {
  Config c;
  std::ifstream f(Config::Dir() + L"\\config.json", std::ios::binary);
  if (!f) { c.Save(); return c; }  // primeira execução: cria com defaults
  json j = json::parse(f, nullptr, false);
  if (j.is_discarded()) { lg::Error("config.json ilegível — usando defaults"); return c; }
  if (j.contains("url")) c.url = W(j["url"].get<std::string>());
  GetB(j, "palavras", c.palavras);
  GetB(j, "mostrar_sem_fala", c.mostrar_sem_fala);
  GetB(j, "autostart", c.autostart);
  Get(j, "idioma", c.idioma);   Get(j, "modelo", c.modelo);
  Get(j, "prompt", c.prompt);   Get(j, "app", c.app);
  Get(j, "tecla", c.tecla);     Get(j, "dispositivo", c.dispositivo);
  Get(j, "metodo_insercao", c.metodo_insercao);
  Get(j, "label", c.label);
  Get(j, "threshold", c.threshold); Get(j, "min_silence_ms", c.min_silence_ms);
  Get(j, "speech_pad_ms", c.speech_pad_ms); Get(j, "max_speech_s", c.max_speech_s);
  Get(j, "beam", c.beam); Get(j, "timeout_ms", c.timeout_ms);
  return c;
}

void Config::Save() const {
  json j;
  j["url"] = U(url); j["idioma"] = idioma; j["modelo"] = modelo; j["prompt"] = prompt;
  j["threshold"] = threshold; j["min_silence_ms"] = min_silence_ms;
  j["speech_pad_ms"] = speech_pad_ms; j["max_speech_s"] = max_speech_s;
  j["palavras"] = palavras; j["beam"] = beam; j["label"] = label; j["timeout_ms"] = timeout_ms;
  j["app"] = app; j["tecla"] = tecla; j["dispositivo"] = dispositivo;
  j["metodo_insercao"] = metodo_insercao; j["mostrar_sem_fala"] = mostrar_sem_fala;
  j["autostart"] = autostart;
  std::wstring p = Dir() + L"\\config.json", tmp = p + L".tmp";
  { std::ofstream f(tmp, std::ios::binary); f << j.dump(2); }
  MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING);  // atômica: nunca corta no meio
}

std::string Config::ToQuery() const {
  std::ostringstream q;
  q << "idioma=" << Enc(idioma) << "&modelo=" << Enc(modelo)
    << "&threshold=" << threshold << "&min_silence_ms=" << min_silence_ms
    << "&speech_pad_ms=" << speech_pad_ms << "&max_speech_s=" << max_speech_s
    << "&beam=" << beam << "&label=" << Enc(label) << "&timeout_ms=" << timeout_ms
    << "&palavras=" << (palavras ? 1 : 0);
  if (!prompt.empty()) q << "&prompt=" << Enc(prompt);
  return q.str();
}

