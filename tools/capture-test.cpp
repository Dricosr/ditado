#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys.h>
#include <propsys.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include "audio.h"
#include "log.h"
#pragma comment(lib, "winmm.lib")
int main() {
  const wchar_t* wav_in = L"C:\\Users\\drico\\AppData\\Local\\Temp\\opencode\\fala.wav";
  // fala do gate sai nos alto-falantes; o microfone padrão (loopback/real) escuta:
  PlaySoundW(wav_in, NULL, SND_FILENAME | SND_ASYNC);
  Sleep(150);
  if (audio::Start(L"")) printf("audio::Start ok\n"); else { printf("audio::Start FALHOU\n"); return 1; }
  Sleep(3500);
  audio::Stop();
  auto pcm = audio::Flush();
  double rms = 0; size_t nz = 0;
  for (auto s : pcm) { rms += (double)s * s; if (s) nz++; }
  if (!pcm.empty()) rms = std::sqrt(rms / pcm.size());
  printf("amostras=%zu rms=%.1f não-zero=%zu\n", pcm.size(), rms, nz);
  auto wav = audio::ToWav(pcm);
  FILE* fp = fopen("C:\\Users\\drico\\AppData\\Local\\Temp\\opencode\\capturado.wav", "wb");
  if (fp) { fwrite(wav.data(), 1, wav.size(), fp); fclose(fp); }
  PlaySoundW(NULL, NULL, SND_PURGE);
}
