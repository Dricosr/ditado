#include "audio.h"
#include "log.h"
#include <algorithm>
#include <format>
#include <audioclient.h>
#include <functiondiscoverykeys.h>
#include <mmdeviceapi.h>
#include <process.h>
#include <shellapi.h>
#include <atomic>
#include <mutex>
#include <process.h>
#include <thread>

namespace {
struct Cap {
  HANDLE ev = nullptr, th = nullptr;
  std::atomic<bool> run{false};
  std::mutex mu;
  std::vector<int16_t> buf;
  std::wstring dev;  // vazio = padrão do sistema
} g;

// shared-mode devolve quase sempre float32 estéreo 48k → mixa p/ mono e reduz p/ 16k
void Mix(const WAVEFORMATEX* f, BYTE* d, UINT32 n, std::vector<int16_t>& out) {
  const WORD tag = f->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                       ? (WORD)((const WAVEFORMATEXTENSIBLE*)f)->SubFormat.Data1
                       : f->wFormatTag;
  const int ch = f->nChannels, rate = f->nSamplesPerSec;
  const bool flt = tag != WAVE_FORMAT_PCM;  // shared mode quase sempre é float32
  const int frames = n / (int)(ch * (flt ? sizeof(float) : sizeof(int16_t)));
  const int per = rate >= 32000 ? rate / 16000 : 1;  // 48k→3, 44,1k→2 (gate reamostra o resto)
  for (int i = 0; i < frames; i += per) {
    const int got = (per < frames - i ? per : frames - i);
    float acc = 0;
    for (int k = 0; k < got; k++)
      for (int c = 0; c < ch; c++) {
        const int j = (i + k) * ch + c;
        acc += flt ? ((const float*)d)[j] : (float)((const int16_t*)d)[j] / 32768.f;
      }
    out.push_back((int16_t)(acc / (ch * got) * 32767));
  }
}

void Thread() {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IMMDeviceEnumerator* en = nullptr;
  IMMDevice* dev = nullptr;
  IAudioClient* ac = nullptr;
  IAudioCaptureClient* cc = nullptr;
  WAVEFORMATEX* f = nullptr;
  HRESULT r = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en);
  if (SUCCEEDED(r))
    r = g.dev.empty() ? en->GetDefaultAudioEndpoint(eCapture, eConsole, &dev)
                      : en->GetDevice(g.dev.c_str(), &dev);
  if (SUCCEEDED(r)) r = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&ac);
  if (SUCCEEDED(r)) r = ac->GetMixFormat(&f);
  if (SUCCEEDED(r)) r = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, f, nullptr);
  if (SUCCEEDED(r)) r = ac->SetEventHandle(g.ev);  // evento: o áudio ACORDA o loop
  if (SUCCEEDED(r)) r = ac->GetService(__uuidof(IAudioCaptureClient), (void**)&cc);
  if (SUCCEEDED(r)) r = ac->Start();
  if (FAILED(r)) lg::Error("capture WASAPI falhou: 0x" + std::format("{:08X}", (unsigned)r));
  else
    while (g.run) {
      if (WaitForSingleObject(g.ev, 50) == WAIT_OBJECT_0) {
        UINT32 n = 0;
        while (cc->GetNextPacketSize(&n) == S_OK && n) {
          BYTE* d = nullptr;
          DWORD fl = 0;
          UINT64 dp = 0, dq = 0;
          if (cc->GetBuffer(&d, &n, &fl, &dp, &dq) == S_OK) {
            std::vector<int16_t> got;
            if (fl & AUDCLNT_BUFFERFLAGS_SILENT) got.resize(n / (f->nChannels * 2) / 2, (int16_t)0);
            else Mix(f, d, n, got);
            std::lock_guard lk(g.mu);
            g.buf.insert(g.buf.end(), got.begin(), got.end());
            cc->ReleaseBuffer(n);
          }
        }
      }
    }
  if (ac) ac->Stop(), ac->Release();
  if (cc) cc->Release();
  if (dev) dev->Release();
  if (en) en->Release();
  if (f) CoTaskMemFree(f);
  CoUninitialize();
}
}  // namespace

namespace audio {
bool Start(const std::wstring& deviceId) {
  g.dev = deviceId;
  g.ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  g.run = true;
  g.th = (HANDLE)_beginthreadex(nullptr, 0, [](void*) { Thread(); return 0u; }, nullptr, 0, nullptr);
  return g.ev && g.th;
}
void Stop() {
  g.run = false;
  if (g.th) {
    WaitForSingleObject(g.th, 1000);
    CloseHandle(g.th);
    g.th = nullptr;
  }
  if (g.ev) {
    CloseHandle(g.ev);
    g.ev = nullptr;
  }
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
  put("RIFF", 4); put(&riff, 4); put("WAVEfmt ", 8); put(&fmt, 4); put(&pcm1, 2); put(&ch1, 2); put(&rate, 4);
  put(&byteRate, 4); put(&align, 2); put(&bits, 2); put("data", 4); put(&data, 4);
  h.append((const char*)pcm.data(), pcm.size() * 2);
  return h;
}
std::string ToWav(const std::wstring& utf16) {
  std::vector<int16_t> pcm;
  for (wchar_t c : utf16) pcm.push_back((int16_t)(unsigned short)c);
  return ToWav(pcm);
}
}  // namespace audio
