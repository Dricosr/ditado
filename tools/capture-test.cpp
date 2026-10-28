#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include "audio.h"
#pragma comment(lib, "winmm.lib")
// Sonda o caminho real de captura: toca tom.wav nos alto-falantes e grava com audio::Start/Stop.
int main(int argc, char** argv) {
  std::wstring dev;
  if (argc > 1) {
    int n = MultiByteToWideChar(CP_UTF8, 0, argv[1], (int)strlen(argv[1]), 0, 0);
    dev.resize(n);
    MultiByteToWideChar(CP_UTF8, 0, argv[1], (int)strlen(argv[1]), dev.data(), n);
  }
  audio::Prepare(dev);
  const wchar_t* wav_in = L"C:\\Users\\drico\\AppData\\Local\\Temp\\opencode\\tom.wav";

  for (int round = 1; round <= 3; round++) {
    if (!audio::Start(dev)) { printf("audio::Start FALHOU\n"); return 1; }
    if (round < 3) PlaySoundW(wav_in, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    const int ms = round == 1 ? 3500 : (round == 2 ? 1500 : 3000);
    Sleep(ms);
    audio::Stop();
    auto pcm = audio::Flush();
    PlaySoundW(NULL, NULL, SND_PURGE);
    double rms = 0;
    size_t nz = 0;
    for (auto s : pcm) { rms += (double)s * s; if (s) nz++; }
    if (!pcm.empty()) rms = std::sqrt(rms / pcm.size());
    printf("rodada %d: esperado=~%d amostras=%zu (%.2fs) rms=%.1f nao-zero=%zu\n",
           round, ms * 16, pcm.size(), pcm.size() / 16000.0, rms, nz);
    if (round == 1) {
      FILE* fp = fopen("C:\\Users\\drico\\AppData\\Local\\Temp\\opencode\\capturado.wav", "wb");
      if (fp) { auto w = audio::ToWav(pcm); fwrite(w.data(), 1, w.size(), fp); fclose(fp); }
    }
  }
}
