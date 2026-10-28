#include "som.h"
#include "log.h"
#include <windows.h>
#include <mutex>

namespace {
constexpr int kPopIn = 201, kPopOut = 202, kRunning = 203;
std::mutex g_mu;
bool g_running = false;

void PlayResource(int id, DWORD flags) {
  if (!PlaySoundW(MAKEINTRESOURCEW(id), GetModuleHandleW(nullptr),
                  SND_RESOURCE | SND_ASYNC | SND_NODEFAULT | flags))
    lg::Warn("som: não consegui tocar o recurso " + std::to_string(id));
}
}  // namespace

namespace som {
void PopIn() {
  std::lock_guard lk(g_mu);
  g_running = false;  // PlaySound interrompe o loop anterior
  PlayResource(kPopIn, 0);
}
void PopOut() {
  std::lock_guard lk(g_mu);
  g_running = false;
  PlayResource(kPopOut, 0);
}
void Running(bool on) {
  std::lock_guard lk(g_mu);
  if (on) {
    if (g_running) return;
    PlayResource(kRunning, SND_LOOP);
    g_running = true;
  } else if (g_running) {
    PlaySoundW(nullptr, nullptr, SND_PURGE);
    g_running = false;
  }
}
void StopAll() {
  std::lock_guard lk(g_mu);
  PlaySoundW(nullptr, nullptr, SND_PURGE);
  g_running = false;
}
}  // namespace som
