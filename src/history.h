#pragma once
#include <string>

namespace history {
// Histórico diário: textos transcritos com sucesso, gravados em <config>\history\YYYYMMDD.jsonl.
void Add(const std::wstring& texto, const std::string& jobId, const std::string& modelo, double duracaoS);
void Show();  // janela do histórico do dia, com copiar para o clipboard
}
