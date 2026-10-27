#include <windows.h>
#include <wininet.h>
#include <cstdio>
#pragma comment(lib, "wininet.lib")
int main() {
  HINTERNET s = InternetOpenW(L"teste/0", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
  printf("open=%d err=%u\n", s != 0, (unsigned)GetLastError());
  wchar_t host[256], path[2048];
  URL_COMPONENTSW u{};
  u.dwStructSize = sizeof(u);
  u.lpszHostName = host; u.dwHostNameLength = 255;
  u.lpszUrlPath = path; u.dwUrlPathLength = 2047;
  const wchar_t* url = L"http://192.168.18.150:3070/v1/transcricao?idioma=pt&modelo=small&threshold=0.5&min_silence_ms=300&speech_pad_ms=100&max_speech_s=30&beam=1&label=ditado&timeout_ms=120000&palavras=0";
  BOOL ok = InternetCrackUrlW(url, (DWORD)lstrlenW(url), 0, &u);
  printf("crack=%d host=%ls path=%ls err=%u\n", ok, host, path, (unsigned)GetLastError());
  HINTERNET c = InternetConnectW(s, host, u.nPort, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
  printf("connect=%p err=%u\n", c, (unsigned)GetLastError());
  HINTERNET r = HttpOpenRequestW(c, L"POST", path, nullptr, nullptr, nullptr,
                                 INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_KEEP_CONNECTION, 0);
  printf("open_req=%p err=%u\n", r, (unsigned)GetLastError());
  const char body[4] = {1, 2, 3, 4};
  if (r && HttpSendRequestW(r, L"Content-Type: audio/wav\r\nX-Gate-App: ditado", 41, (void*)body, 4)) printf("enviou\n"); else printf("send err=%u\n", (unsigned)GetLastError());
  char b[256]; DWORD got = 0;
  if (r && InternetReadFile(r, b, 200, &got)) b[got] = 0, printf("resposta: %s\n", b);
}
