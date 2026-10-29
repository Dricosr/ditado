#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio.h"
#include "config.h"
#include "gate.h"
#include "history.h"
#include "hotkey.h"
#include "inject.h"
#include "log.h"
#include "settings.h"
#include "som.h"
#include "tray.h"
#include "version.h"

namespace {
Config g_cfg;

// Fila de gravações: cada soltura vira um job; um único worker envia na ordem em que
// o usuário falou. Assim os textos são digitados na mesma ordem, mesmo que o gate demore.
struct Job {
  uint64_t seq;
  std::vector<int16_t> pcm;
  Config cfg;  // cópia: mudar opções não afeta um job já criado
};
std::mutex g_q_mu;
std::condition_variable g_q_cv;
std::deque<Job> g_q;
std::atomic<int> g_pending{0};
std::atomic<bool> g_rec{false};
std::mutex g_rec_mu;
std::condition_variable g_rec_cv;
std::atomic<bool> g_quit{false};
std::atomic<uint64_t> g_seq{0};  // ordem das gravações

std::string U(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, 0);
  if (n) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}
std::wstring W(const std::string& s) {
  if (s.empty()) return {};
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

void SavePending(uint64_t seq, const std::string& wav) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  std::wstring nome = std::format(L"{:04}{:02}{:02}-{:02}{:02}{:02}-{}.wav",
                                  t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, seq);
  std::wstring dir = Config::Dir() + L"\\pending";
  CreateDirectoryW(dir.c_str(), nullptr);
  std::wstring p = dir + L"\\" + nome;
  HANDLE f = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) { lg::Error("pending: não consegui criar " + U(nome)); return; }
  DWORD wr = 0;
  WriteFile(f, wav.data(), (DWORD)wav.size(), &wr, nullptr);
  CloseHandle(f);
  lg::Info("áudio guardado em pending: " + U(nome));
}

void WaitNotRecording() {
  std::unique_lock lk(g_rec_mu);
  g_rec_cv.wait(lk, [] { return !g_rec.load() || g_quit.load(); });
}

void UpdateTip() {
  if (g_rec.load()) {
    tray::SetTooltip(L"gravando… (solte para transcrever)");
  } else {
    int n = g_pending.load();
    tray::SetTooltip(n > 0 ? L"transcrevendo… " + std::to_wstring(n) + L" na fila" : L"pronto");
  }
}

void JobWorker() {
  while (true) {
    Job job;
    {
      std::unique_lock lk(g_q_mu);
      g_q_cv.wait(lk, [] { return g_quit.load() || !g_q.empty(); });
      if (g_quit.load()) break;
      job = std::move(g_q.front());
      g_q.pop_front();
    }
    std::string wav = audio::ToWav(job.pcm);
    lg::Audio(wav);
    try {
      auto r = gate::Transcrever(job.cfg, wav);
      if (r.ok && !r.semFala) {
        WaitNotRecording();  // não digita enquanto o usuário ainda segura a tecla gravando
        inject::Texto(job.cfg, r.texto);
        history::Add(r.texto, r.jobId, job.cfg.modelo, (double)job.pcm.size() / 16000.0);
        lg::Info("job seq=" + std::to_string(job.seq) + " transcrito ok jobId=" + r.jobId);
      } else if (r.ok) {
        lg::Info("job seq=" + std::to_string(job.seq) + " sem fala — nada digitado");
      } else {
        lg::Error("gate: " + r.erro + " " + r.detail + " jobId=" + r.jobId + " seq=" + std::to_string(job.seq));
        tray::Balloon(L"ditado", W(r.erro + " " + r.detail));
        SavePending(job.seq, wav);
      }
    } catch (const std::exception& e) {
      lg::Error("job seq=" + std::to_string(job.seq) + " falhou: " + e.what());
    }
    int left = --g_pending;
    UpdateTip();
    if (left == 0) som::Running(false);
  }
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
  lg::Verbose(g_cfg.log_local);
  audio::Prepare(W(g_cfg.dispositivo));  // deixa o stream pronto: gravar começa sem atraso
  lg::Info("opções:\n" + g_cfg.Dump());
  const wchar_t* cmd = GetCommandLineW();
  bool selftest = wcsstr(cmd, L"--selftest") != nullptr;
  lg::Info(std::format("ditado {} iniciado — gate={}", DITADO_VERSION, U(g_cfg.url)));

  std::thread(JobWorker).detach();  // um único worker preserva a ordem das gravações

  // ---- máquina de estados: Ocioso → Gravando → (fila) Transcrevendo → Ocioso ----
  enum class Estado { Ocioso, Gravando } est = Estado::Ocioso;
  bool teclaDown = false;       // evita o auto-repeat do keydown
  bool capturando = false;
  bool micErroMostrado = false; // uma notificação por falha, até uma captura dar certo
  auto on_key = [&](const KeyEvent& e) {
    if (e.down) {
      if (teclaDown) return;  // auto-repeat: a tecla continua fisicamente pressionada
      teclaDown = true;
      if (est != Estado::Ocioso) return;
      est = Estado::Gravando;
      g_rec = true;
      tray::SetTooltip(L"gravando… (solte para transcrever)");
      if (!audio::Start(W(g_cfg.dispositivo))) {
        est = Estado::Ocioso;
        g_rec = false;
        capturando = false;
        g_rec_cv.notify_all();
        lg::Error("não consegui iniciar a captura — veja o dispositivo em Opções");
        if (!micErroMostrado) {
          tray::Balloon(L"ditado", L"Microfone indisponível. Escolha outro dispositivo em Opções.");
          micErroMostrado = true;
        }
        UpdateTip();
        return;
      }
      capturando = true;
      micErroMostrado = false;  // captura voltou a funcionar
      som::PopIn();             // captura já está ligada; o pop é o feedback de "gravando"
      return;
    }

    if (!teclaDown) return;
    teclaDown = false;
    if (est == Estado::Gravando && capturando) {
      est = Estado::Ocioso;
      g_rec = false;
      g_rec_cv.notify_all();
      audio::Stop();
      auto pcm = audio::Flush();
      capturando = false;
      if (lg::Verbose())
        lg::Info("entrada: tecla '" + g_cfg.tecla + "' solta — gravado " + std::to_string(pcm.size()) + " amostras de '" + g_cfg.dispositivo + "'");
      som::PopOut();
      if (pcm.empty()) {
        lg::Info("gravação vazia — nada a enviar");
        UpdateTip();
        return;
      }
      Job job{++g_seq, std::move(pcm), g_cfg};
      ++g_pending;
      {
        std::lock_guard lk(g_q_mu);
        g_q.push_back(std::move(job));
      }
      g_q_cv.notify_one();
      UpdateTip();
      // o pop final já está tocando; depois dele, entra o loop de "processando"
      std::thread([] {
        Sleep(2000);
        if (!g_rec.load() && g_pending.load() > 0) som::Running(true);
      }).detach();
    } else {
      est = Estado::Ocioso;
      g_rec = false;
      UpdateTip();
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
          if (r.ok && !r.semFala) {
            inject::Texto(g_cfg, r.texto);
            history::Add(r.texto, r.jobId, g_cfg.modelo, 0.0);
          }
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
  if (wcsstr(cmd, L"--historico")) {  // modo de teste: abre a janela do histórico
    history::Show();
    return 0;
  }

  auto open_settings = [&] {
    Config c = g_cfg;
    tray::SetTooltip(L"configurando…");
    settings::Show(nullptr, c);
    if (c.url != g_cfg.url || c.tecla != g_cfg.tecla || c.modelo != g_cfg.modelo || c.autostart != g_cfg.autostart ||
        c.dispositivo != g_cfg.dispositivo || c.log_local != g_cfg.log_local) {
      bool combo = c.tecla != g_cfg.tecla, start = c.autostart != g_cfg.autostart,
           dev = c.dispositivo != g_cfg.dispositivo;
      g_cfg = c;
      g_cfg.Save();
      lg::Verbose(g_cfg.log_local);
      if (combo) hotkey::SetCombo(g_cfg.tecla);
      if (start) SetAutostart(g_cfg.autostart);
      if (dev) { audio::Prepare(W(g_cfg.dispositivo)); micErroMostrado = false; }
      lg::Info("config salva — opções:\n" + g_cfg.Dump());
    }
    UpdateTip();
  };

  // ---- modos de teste (--selftest já existia; --ditar fecha o E2E) ----
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
                           if (!audio::Start(W(g_cfg.dispositivo))) {
                             lg::Error("teste mic: não consegui iniciar a captura");
                             tray::Balloon(L"ditado", L"Microfone indisponível. Escolha outro dispositivo em Opções.");
                             tray::SetTooltip(L"pronto");
                             return;
                           }
                           som::PopIn();
                           tray::SetTooltip(L"testando microfone…");
                           std::thread([] {
                             Sleep(3000);
                             audio::Stop();
                             som::PopOut();
                             auto pcm = audio::Flush();
                             auto r = gate::Transcrever(g_cfg, audio::ToWav(pcm));
                             if (r.ok)
                               lg::Info("teste mic: ouvi '" + U(r.texto) + "'" + (r.semFala ? " (SILÊNCIO — veja o dispositivo em Opções)" : ""));
                             else
                               lg::Error("teste mic: erro " + r.erro);
                             if (r.ok && !r.texto.empty()) inject::Texto(g_cfg, r.texto);
                             tray::SetTooltip(L"pronto");
                           }).detach();
                         },
                         .ToggleAutostart = [] {
                           g_cfg.autostart = !g_cfg.autostart;
                           g_cfg.Save();
                           SetAutostart(g_cfg.autostart);
                         },
                         .Historico = [] { history::Show(); },
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
  g_quit = true;
  g_q_cv.notify_all();
  som::StopAll();
  return (int)m.wParam;
}
