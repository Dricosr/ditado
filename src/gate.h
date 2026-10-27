#pragma once
#include "config.h"
#include <string>
#include <vector>

namespace gate {
struct Result {
  bool ok = false;
  std::wstring texto;   // o que o app digita
  bool semFala = false;
  std::string erro, detail, jobId;  // como a doc do gate nomeia
};
// POST /v1/transcricao — corpo é o WAV cru, params vêm da config.
Result Transcrever(const Config& cfg, const std::string& wavBytes);
}  // namespace gate
