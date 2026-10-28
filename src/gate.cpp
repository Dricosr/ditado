#include "gate.h"
#include "log.h"
#include "version.h"
#include <windows.h>
#include <wininet.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {
std::wstring U8(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0);
  if (n) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}
std::string W8(const std::wstring& w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, 0);
  if (n) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}
}  // namespace

namespace gate {
Result Transcrever(const Config& cfg, const std::string& wav) {
  Result r;
  if (wav.size() > 64ull * 1024 * 1024) {  // o limite do gate; cortamos antes de estourar lá
    r.erro = "input_too_large";
    r.detail = "o cliente cortou antes de chamar o gate";
    return r;
  }
  HINTERNET s = InternetOpenW(L"ditado/" DITADO_VERSION, INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
  DWORD tmo = (DWORD)cfg.timeout_ms + 5000;  // folga p/ o cliente nunca travar se o nó travar
  InternetSetOptionW(s, INTERNET_OPTION_CONNECT_TIMEOUT, &tmo, 4);
  InternetSetOptionW(s, INTERNET_OPTION_RECEIVE_TIMEOUT, &tmo, 4);
  URL_COMPONENTSW u{}; u.dwStructSize = sizeof(u);
  wchar_t host[256], path[2048];
  u.lpszHostName = host; u.dwHostNameLength = 255;
  u.lpszUrlPath = path; u.dwUrlPathLength = 2047;
  const std::wstring url = cfg.url + L"/v1/transcricao?" + U8(cfg.ToQuery());  // query é ascii puro
  if (!s || !InternetCrackUrlW(url.c_str(), (DWORD)url.size(), 0, &u)) {
    r.erro = "url_invalida";
    InternetCloseHandle(s);
    return r;
  }
  if (lg::Verbose())
    lg::Info("→ enviado: POST " + W8(url) + " corpo=" + std::to_string(wav.size()) + " bytes");
  HINTERNET c = InternetConnectW(s, u.lpszHostName, u.nPort, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
  HINTERNET req = c ? HttpOpenRequestW(c, L"POST", u.lpszUrlPath, nullptr, nullptr, nullptr,
                                       INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_KEEP_CONNECTION, 0)
                    : nullptr;
  if (req) {
    const std::wstring hdr = L"Content-Type: audio/wav\r\nX-Gate-App: " + U8(cfg.app);
    if (HttpSendRequestW(req, hdr.c_str(), (DWORD)hdr.size(), (void*)wav.data(), (DWORD)wav.size())) {
      std::string body;
      char b[4096];
      DWORD got = 0;
      while (InternetReadFile(req, b, sizeof b, &got) && got) body.append(b, got);
      json j = json::parse(body, nullptr, false);
      if (j.is_discarded()) j = json::object();
      if (lg::Verbose()) lg::Info("← recebido: " + (body.size() > 2000 ? body.substr(0, 2000) + "…" : body));
      if (j.contains("error")) r.erro = j["error"].get<std::string>();
      if (j.contains("detail")) r.detail = j["detail"].get<std::string>();
      if (j.contains("jobId")) r.jobId = j["jobId"].get<std::string>();
      r.ok = j.contains("texto") && j["texto"].is_string();
      if (r.ok) {
        r.texto = U8(j["texto"].get<std::string>());
        r.semFala = j.value("semFala", false);
        lg::Info("transcrito ok jobId=" + r.jobId);
      } else if (r.erro.empty()) {
        r.erro = "resposta_inesperada";
      }
    } else {
      r.erro = "conexao";
      r.detail = "rede: " + std::to_string(GetLastError());
    }
    InternetCloseHandle(req);
  } else {
    r.erro = "conexao";
    r.detail = "gate inacessível: " + std::to_string(GetLastError()) + " url=" + W8(url);
  }
  if (c) InternetCloseHandle(c);
  InternetCloseHandle(s);
  return r;
}
}  // namespace gate
