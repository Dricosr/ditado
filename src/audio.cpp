#include "audio.h"
#include "log.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <audioclient.h>
#include <functiondiscoverykeys.h>
#include <mmdeviceapi.h>
#include <process.h>
#include <atomic>
#include <mutex>
#include <vector>

// Captura WASAPI em modo shared. O client é preparado uma vez (no início do app) e fica parado
// entre ditados: Start só liga o stream, então não se perde o começo da fala na inicialização.

namespace {
constexpr int kRate = 16000;  // formato que o Whisper do gate espera

struct Cap {
  HANDLE wake = nullptr, started = nullptr, stopped = nullptr, ev = nullptr, th = nullptr;
  std::atomic<bool> run{false}, active{false}, startOk{false}, reprepare{false}, quit{false};
  std::mutex mu;
  std::vector<int16_t> buf;
  std::wstring dev;            // vazio = padrão do sistema
  std::wstring preparedDev;
  IAudioClient* ac = nullptr;
  IAudioCaptureClient* cc = nullptr;
  WAVEFORMATEX* fmt = nullptr;
} g;

void ReleaseClient() {
  if (g.ac) { g.ac->Stop(); g.ac->Release(); g.ac = nullptr; }
  if (g.cc) { g.cc->Release(); g.cc = nullptr; }
  if (g.fmt) { CoTaskMemFree(g.fmt); g.fmt = nullptr; }
}

// Monta (ou remonta) o client do dispositivo atual. Chamado só no thread de captura:
// os objetos COM ficam no apartment desse thread, onde Start/Stop/Reset são chamados.
bool PrepareClient() {
  ReleaseClient();
  IMMDeviceEnumerator* en = nullptr;
  IMMDevice* dev = nullptr;
  HRESULT r = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                               __uuidof(IMMDeviceEnumerator), (void**)&en);
  if (SUCCEEDED(r))
    r = g.dev.empty() ? en->GetDefaultAudioEndpoint(eCapture, eConsole, &dev)
                      : en->GetDevice(g.dev.c_str(), &dev);
  if (SUCCEEDED(r)) r = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&g.ac);
  if (SUCCEEDED(r)) r = g.ac->GetMixFormat(&g.fmt);
  if (SUCCEEDED(r))
    r = g.ac->Initialize(AUDCLNT_SHAREMODE_SHARED,
                         AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                           AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                         0, 0, g.fmt, nullptr);  // buffer padrão do engine
  if (SUCCEEDED(r)) r = g.ac->SetEventHandle(g.ev);
  if (SUCCEEDED(r))
    r = g.ac->GetService(__uuidof(IAudioCaptureClient), (void**)&g.cc);
  if (dev) dev->Release();
  if (en) en->Release();
  if (FAILED(r)) {
    lg::Error("capture WASAPI falhou: 0x" + std::format("{:08X}", (unsigned)r));
    ReleaseClient();
    return false;
  }
  g.preparedDev = g.dev;
  return true;
}

// Converte um pacote (frames, N canais, float32 ou s16) para mono 16 kHz.
// GetBuffer/GetNextPacketSize devolvem QUADROS, não bytes — era a origem do áudio cortado.
void AppendPacket(const WAVEFORMATEX* f, const BYTE* d, UINT32 frames, DWORD flags,
                  std::vector<int16_t>& out) {
  const WORD tag = f->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                     ? (WORD)((const WAVEFORMATEXTENSIBLE*)f)->SubFormat.Data1
                     : f->wFormatTag;
  const int ch = f->nChannels, rate = f->nSamplesPerSec;
  const bool flt = tag != WAVE_FORMAT_PCM;  // shared mode quase sempre float32
  const int bpp = f->nBlockAlign ? (int)f->nBlockAlign : ch * (flt ? 4 : 2);
  const int bps = bpp / ch;
  const unsigned long long outN = (unsigned long long)frames * kRate / rate;
  if (!outN) return;
  if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
    out.insert(out.end(), (size_t)outN, 0);
    return;
  }
  std::vector<float> mono(frames);
  for (UINT32 i = 0; i < frames; i++) {
    float acc = 0;
    for (int c = 0; c < ch; c++) {
      const size_t off = (size_t)i * bpp + (size_t)c * bps;
      acc += flt ? ((const float*)d)[off / 4] : (float)((const int16_t*)d)[off / 2] / 32768.f;
    }
    mono[i] = acc / ch;
  }
  out.reserve(out.size() + (size_t)outN);
  for (unsigned long long j = 0; j < outN; j++) {
    const double pos = (double)j * rate / kRate;
    const size_t i0 = (size_t)pos;
    const float frac = (float)(pos - (double)i0);
    float s = mono[i0];
    if (i0 + 1 < frames) s = s * (1 - frac) + mono[i0 + 1] * frac;
    s = std::clamp(s, -1.f, 1.f);
    out.push_back((int16_t)std::lrintf(s * 32767.f));
  }
}

void Drain() {
  if (!g.cc) return;
  UINT32 n = 0;
  while (g.cc->GetNextPacketSize(&n) == S_OK && n) {
    BYTE* d = nullptr;
    DWORD fl = 0;
    UINT64 dp = 0, dq = 0;
    UINT32 frames = n;  // GetBuffer pode ajustar; guardamos o tamanho real
    if (FAILED(g.cc->GetBuffer(&d, &frames, &fl, &dp, &dq))) break;
    std::vector<int16_t> mono;
    AppendPacket(g.fmt, d, frames, fl, mono);
    {
      std::lock_guard lk(g.mu);
      g.buf.insert(g.buf.end(), mono.begin(), mono.end());
    }
    g.cc->ReleaseBuffer(frames);  // consome o pacote (0 deixaria a fila travada)
  }
}

void Worker() {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  PrepareClient();
  while (!g.quit) {
    WaitForSingleObject(g.wake, INFINITE);
    if (!g.run) continue;  // pedido de parada sem gravação
    if (g.reprepare || g.dev != g.preparedDev || !g.ac) {
      g.reprepare = false;
      if (!PrepareClient()) {
        g.reprepare = true;  // nova tentativa no próximo Start
        g.startOk = false;
        g.run = false;
        SetEvent(g.started);
        SetEvent(g.stopped);
        continue;
      }
    }
    g.ac->Reset();  // descarta qualquer sobra da gravação anterior
    HRESULT r = g.ac->Start();
    if (FAILED(r)) {
      lg::Error("capture Start falhou: 0x" + std::format("{:08X}", (unsigned)r));
      g.startOk = false;
      g.run = false;
      SetEvent(g.started);
      SetEvent(g.stopped);
      continue;
    }
    g.startOk = true;
    g.active = true;
    SetEvent(g.started);
    while (g.run) {
      WaitForSingleObject(g.ev, 10);  // o áudio acorda o loop; drena sempre
      Drain();
    }
    Drain();  // últimos pacotes antes de parar
    g.ac->Stop();
    g.active = false;
    SetEvent(g.stopped);
  }
  ReleaseClient();
  CoUninitialize();
}

void EnsureWorker() {
  if (g.th) return;
  g.wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  g.started = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  g.stopped = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  g.ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  g.th = (HANDLE)_beginthreadex(nullptr, 0, [](void*) { Worker(); return 0u; }, nullptr, 0, nullptr);
}
}  // namespace

namespace audio {
void Prepare(const std::wstring& deviceId) {
  {
    std::lock_guard lk(g.mu);
    if (deviceId != g.dev) g.reprepare = true;
    g.dev = deviceId;
  }
  EnsureWorker();
}

bool Start(const std::wstring& deviceId) {
  {
    std::lock_guard lk(g.mu);
    g.buf.clear();  // nunca arrasta áudio da gravação anterior
    g.dev = deviceId;
    g.run = true;
  }
  EnsureWorker();
  SetEvent(g.wake);
  // espera o stream realmente começar: assim o primeiro trecho não fica de fora
  WaitForSingleObject(g.started, 4000);
  return g.startOk;
}

void Stop() {
  g.run = false;
  if (g.active) {
    SetEvent(g.wake);
    if (WaitForSingleObject(g.stopped, 4000) == WAIT_TIMEOUT) lg::Error("capture Stop: timeout");
  }
  g.active = false;
}

std::vector<int16_t> Flush() {
  std::lock_guard lk(g.mu);
  std::vector<int16_t> out;
  out.swap(g.buf);
  return out;
}

std::string ToWav(const std::vector<int16_t>& pcm) {
  const uint32_t data = (uint32_t)pcm.size() * 2;
  const uint32_t riff = 36 + data, fmt = 16, byteRate = 32000;  // 16000 Hz × 2 bytes
  const uint16_t pcm1 = 1, ch1 = 1, align = 2, rate = 16000, bits = 16;
  std::string h;
  auto put = [&](const void* p, size_t n) { h.append((const char*)p, n); };
  put("RIFF", 4); put(&riff, 4); put("WAVEfmt ", 8); put(&fmt, 4); put(&pcm1, 2); put(&ch1, 2);
  put(&rate, 4); put(&byteRate, 4); put(&align, 2); put(&bits, 2); put("data", 4); put(&data, 4);
  h.append((const char*)pcm.data(), pcm.size() * 2);
  return h;
}

std::string ToWav(const std::wstring& utf16) {
  std::vector<int16_t> pcm;
  for (wchar_t c : utf16) pcm.push_back((int16_t)(unsigned short)c);
  return ToWav(pcm);
}
}  // namespace audio
