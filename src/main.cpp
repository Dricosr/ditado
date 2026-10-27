#include <windows.h>
#include <shellapi.h>
#include <format>
#include <string>
#include <thread>

#include "audio.h"
#include "config.h"
#include "gate.h"
#include "hotkey.h"
#include "inject.h"
#include "log.h"
#include "settings.h"
#include "tray.h"
#include "version.h"

namespace {
Config g_cfg;
std::wstring ExeDir() {
  wchar_t b[MAX_PATH];
  GetModuleFileNameW(nullptr, b, MAX_PATH);
  return std::wstring(b).substr(0, std::wstring(b).find_last_of(L'\\'));
}
// conversão dois sentidos: U é a canônica (wide→utf8); W é o inverso
std::string U(const std::wstring& w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, 0);
  if (n) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}
std::wstring W(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0);
  if (n) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}
void SetAutostart(bool on) {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &k)) return;
  if (on) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --autostart";
    RegSetValueExW(k, L"ditado", 0, REG_SZ, (BYTE*)cmd.c_str(), (DWORD)cmd.size() * 2);
  } else {
    RegDeleteValueW(k, L"ditado");
  }
  RegCloseKey(k);
}
}  // namespace

int APIENTRY wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
  HANDLE inst = CreateMutexW(nullptr, TRUE, L"Local\\ditado.instancia");
  if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;  // instância única
  (void)inst;                                            // o mutex vive até o fim do processo

  SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e) -> LONG {
    lg::Error("exceção não tratada: " + std::format("{:08X}", (unsigned)e->ExceptionRecord->ExceptionCode));
    return EXCEPTION_EXECUTE_HANDLER;  // encerra logado em vez de calado
  });

  g_cfg = Config::Load();
  lg::Init(Config::Dir());
  const wchar_t* cmd = GetCommandLineW();
  bool selftest = wcsstr(cmd, L"--selftest") != nullptr;
  lg::Info(std::format("ditado {} iniciado — gate={}", DITADO_VERSION, U(g_cfg.url)));

  // ---- máquina de estados: Ocioso → Gravando → Transcrevendo → Ocioso ----
  enum class Estado { Ocioso, Gravando } est = Estado::Ocioso;
  auto on_key = [&](const KeyEvent& e) {
    if (e.down && est == Estado::Ocioso) {
      est = Estado::Gravando;
      tray::SetTooltip(L"gravando… (solte para transcrever)");
      audio::Start(W(g_cfg.dispositivo));
    } else if (!e.down && est == Estado::Gravando) {
      est = Estado::Ocioso;
      audio::Stop();
      auto pcm = audio::Flush();
      tray::SetTooltip(L"transcrevendo…");
      std::thread([pcm = std::move(pcm)] {
        std::string wav = audio::ToWav(pcm);
        auto r = gate::Transcrever(g_cfg, wav);
        if (r.ok && !r.semFala)
          inject::Texto(g_cfg, r.texto);
        else if (r.ok)
            lg::Info("sem fala — nada enviado");
        else {
            lg::Error("gate: " + r.erro + " " + r.detail + " jobId=" + r.jobId);
            tray::Balloon(L"ditado", W(r.erro + " " + r.detail));
            // a frase nunca se perde: WAV guardado e reenviado quando o gate voltar
            SYSTEMTIME t;
            GetLocalTime(&t);
            wchar_t name[64];
            wsprintfW(name, L"\\%04u%02u%02u-%02u%02u%02u.wav", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
            std::wstring dir = Config::Dir() + L"\\pending";
            CreateDirectoryW(dir.c_str(), nullptr);
            std::wstring p = dir + name;
            if (INVALID_FILE_ATTRIBUTES == GetFileAttributesW(p.c_str())) {
              HANDLE f = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
              DWORD wr = 0;
              WriteFile(f, wav.data(), (DWORD)wav.size(), &wr, nullptr);
              CloseHandle(f);
              lg::Info("áudio guardado em pending: " + U(std::wstring(name)));
            }
        }
      }).detach();
    }
  };
  hotkey::Start(g_cfg.tecla, on_key);

  // frases antigas que ficaram presas no pending: reenvia na subida
  {
    WIN32_FIND_DATAW fd;
    std::wstring pat = Config::Dir() + L"\\pending\\*.wav";
    HANDLE hq = FindFirstFileW(pat.c_str(), &fd);
    if (hq != INVALID_HANDLE_VALUE) {
      do {
        std::wstring p = Config::Dir() + L"\\pending\\" + fd.cFileName;
        HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
          std::string wav((DWORD)GetFileSize(f, nullptr), 0);
          DWORD rd = 0;
          ReadFile(f, wav.data(), (DWORD)wav.size(), &rd, nullptr);
          CloseHandle(f);
          auto r = gate::Transcrever(g_cfg, wav);
          if (r.ok && !r.semFala) inject::Texto(g_cfg, r.texto);
          if (r.ok) {
            lg::Info("pending reenviado: " + U(std::wstring(fd.cFileName)));
            DeleteFileW(p.c_str());  // granular: só o arquivo que a gente mesmo gravou
          }
        }
      } while (FindNextFileW(hq, &fd));
      FindClose(hq);
    }
  }

  if (wcsstr(cmd, L"--version") || wcsstr(cmd, L"--help")) {
    lg::Info(std::string("ditado ") + DITADO_VERSION + " — gate: " + U(g_cfg.url));
    if (!selftest) return 0;
  }

  auto open_settings = [] {
    Config c = g_cfg;
    tray::SetTooltip(L"configurando…");
    settings::Show(nullptr, c);
    if (c.url != g_cfg.url || c.tecla != g_cfg.tecla || c.modelo != g_cfg.modelo || c.autostart != g_cfg.autostart) {
      bool combo = c.tecla != g_cfg.tecla, start = c.autostart != g_cfg.autostart;
      g_cfg = c;
      g_cfg.Save();
      if (combo) hotkey::SetCombo(g_cfg.tecla);
      if (start) SetAutostart(g_cfg.autostart);
      lg::Info("config salva");
    }
    tray::SetTooltip(L"pronto");
  };

  // ---- modos de teste (--selftest já existia; --falar/--ditar fecham o E2E) ----
  auto arg = [&](std::wstring& out, const wchar_t* name) {
    int n = 0;
    LPWSTR* a = CommandLineToArgvW(cmd, &n);
    for (int i = 1; i < n; i++)
      if (lstrcmpW(a[i], name) == 0 && i + 1 < n) { out = a[i + 1]; return true; }
    return false;
  };
  std::wstring v;
  if (selftest) {
    open_settings();
    hotkey::Stop();
    return 0;
  }
  if (arg(v, L"--ditar") && GetFileAttributesW(v.c_str()) != INVALID_FILE_ATTRIBUTES) {
    // E2E: WAV do gate-tts → rota de transcrição do app → SendInput no campo ativo.
    // Percorre a máquina de estados real (Gravando→Transcrevendo→Ocioso) como se a tecla fosse segurada.
    est = Estado::Gravando;
    tray::SetTooltip(L"gravando…");
    audio::Start(W(g_cfg.dispositivo));
    std::thread([&] {
      Sleep(400);
      audio::Stop();
      auto pcm = audio::Flush();
      (void)pcm;  // o áudio do teste vem do arquivo, não do microfone
      HANDLE f = CreateFileW(v.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
      std::string wav((DWORD)GetFileSize(f, nullptr), 0);
      DWORD rd = 0;
      ReadFile(f, wav.data(), (DWORD)wav.size(), &rd, nullptr);
      CloseHandle(f);
      auto r = gate::Transcrever(g_cfg, wav);
      if (r.ok && !r.semFala) inject::Texto(g_cfg, r.texto);
      lg::Info("--ditar: " + std::string(r.ok && !r.semFala ? "digitado" : "falhou " + r.erro));
      est = Estado::Ocioso;
      tray::SetTooltip(L"pronto");
    }).detach();
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
  }

  if (!selftest &&
      tray::Init(hi, {
                         .OpenSettings = open_settings,
                         .TestarMic = [] {
                           lg::Info("testando microfone…");
                           tray::SetTooltip(L"testando microfone…");
                           audio::Start(W(g_cfg.dispositivo));
                           std::thread([] {
                             Sleep(3000);
                             audio::Stop();
                             auto pcm = audio::Flush();
                             auto r = gate::Transcrever(g_cfg, audio::ToWav(pcm));
                             lg::Info(std::string("teste mic: ") + (r.ok ? "ok" : "erro: " + r.erro));
                             if (r.ok && !r.texto.empty()) inject::Texto(g_cfg, r.texto);
                             tray::SetTooltip(L"pronto");
                           }).detach();
                         },
                         .ToggleAutostart = [] {
                           g_cfg.autostart = !g_cfg.autostart;
                           g_cfg.Save();
                           SetAutostart(g_cfg.autostart);
                         },
                         .Quit = [] { PostMessageW(FindWindowW(L"ditado.wnd", nullptr), WM_CLOSE, 0, 0); },
                     })) {
    tray::SetTooltip(L"pronto");
  }

  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0)) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  hotkey::Stop();
  return (int)m.wParam;
}
