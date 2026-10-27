# ditado — plano de desenvolvimento

App Windows, tray-first, push-to-talk: segura a combinação → fala → solta → o texto aparece no campo ativo.

---

## 0. O que já está confirmado (medido, não assumido)

| Item | Valor |
|---|---|
| Rota principal | `POST /v1/transcricao?idioma=pt&modelo=...` — corpo **é o áudio cru** (wav), sem `job` no código do app |
| Resposta | `{ jobId, texto, semFala, fala: { segmentos[], falaS, ... }, estado }` — `saida`/`idiomaSaida` **somem do contrato**: a rota não aceita `saida` (devolve `400 invalid_item`), só `idioma` |
| Auth | **sem token — testado e aprovado em produção**: só o cabeçalho de rótulo `X-Gate-App: ditado` |
| Medido (26/10/2026, `small`) | 7,35 s de fala → `inferenciaMs: 1289`, `rtf: 0,19`, VAD do servidor cortou 1,89 s de silêncio das pontas (`recortado: true`) |
| Modelo | `small` (~1,5 s E2E) · `turbo` (~6 s) · `large-v3` (~9 s). Um modelo por app — trocar o residente custa 5-10 s |
| VAD | **no servidor** (Silero). O app não implementa VAD nem corta silêncio |
| Concorrência | `transcricao: maxConcurrency 1`, lane própria, `cpuOnly`, roda em paralelo com os outros motores |
| Limites | áudio ≤ 64 MB · `timeout_ms` padrão 120 000 → `504 transcription_timeout` + `jobId` |
| Idioma | transcreve, **não traduz**: `saida != entrada` → `400 invalid_item` |

Consequência de design: o app **não** precisa de VAD, de segmentação, de streaming nem de decoder próprio. Empurra WAV cru na rota e injeta o `texto`. Nada além disso.

---

## 1. Stack

**C++20 / MSVC** (VS 2026 Build Tools + SDK 26100 **já instalados** ✓; `vcvars64.bat` → `cl /std:c++20 /MT /W4 /EHsc`), CMake + Ninja (também já existem ✓).

| Necessidade | API nativa |
|---|---|
| Tecla global (com *release*, que `RegisterHotKey` não dá) | `WH_KEYBOARD_LL` → `KBDLLHOOKSTRUCT.vkCode` + estado de `vk/shift/ctrl/alt` no próprio hook |
| Ícone na bandeja + menu + janela de opções | `Shell_NotifyIconW` + `CreatePopupMenu` + `DialogBoxIndirectParamW` (diálogo criado na mão, `DLGTEMPLATE` com fonte do SO) — sem framework de UI |
| Gravar o microfone | WASAPI shared: `IMMDeviceEnumerator` → `IAudioClient::Initialize` + `IAudioCaptureClient::GetBuffer`, evento de `SetEvent` |
| Converter para 16 kHz mono s16 | downmix (estéreo→mono) + resample por média de janelas (`mix/Mix`) — o formato do dispositivo raramente é 16 k |
| Digitar no campo ativo | `SendInput` com `KEYEVENTF_UNICODE` (UTF-16 → surrogate pairs) · modo B: `OpenClipboard` → `EmptyClipboard` → `CF_UNICODETEXT` → `Ctrl+V` → **restaurar** clipboard com `IsFormatNameAvailable` |
| HTTP | **WinInet** (`InternetCrackUrl` + `InternetOpenUrl` com `HTTP_REQ_TYPE_POST` — o corpo é direto `InternetOpenUrl`, zero código de socket) — `http://` na LAN, zero libs de rede |
| JSON | `nlohmann/json.hpp` **vendorizado** em `third_party/` (1 arquivo, licença MIT) — o único .h de fora do projeto |

**Dependências: zero além da STL + API Windows + 1 header vendorizado.** Zero DLLs no instalador: `/MT` (CRT estático; UCRT já vem no SO).

Instalador: **Inno Setup** — `DefaultDirName={localappdata}\Programs\ditado`, `ArchitecturesInstallIn64BitMode=x64`, `PrivilegesRequired=lowest` → **sem UAC**; atalho no menu Iniciar + `...\CurrentVersion\Run` para autostart.

---

## 2. Arquitetura (3 tarefas, 2 canais)

```
tecla (hook LL) ──evento──▶ máquina de estados ──WAV──▶ thread WinHTTP ──texto──▶ thread de mensagem ──▶ SendInput
                                   ▲                                                        │
                                   └───────────── config.json ◀──── janela de opções ──────┘
```

1. **Thread de mensagem oculta** (janela de mensagem) — hook do teclado, tray, menu, janela de opções.
2. **Thread de áudio** — `IAudioCaptureClient::GetBuffer` → downmix + resample → buffer circular s16 mono 16 kHz (60 s, `std::mutex`).
3. **Thread de rede** — `WinHttpOpen` → `WinHttpOpenRequest` (POST) → `WinHttpSendRequest` com query string da config → resposta chega pela janela de mensagem (`WM_APP+n`).

Máquina de estados: `Ocioso → (tecla) Gravando → (solta) Transcrevendo → Inserindo → Ocioso`.

Regras que evitam complexidade:
- `< 250 ms` de fala → o gate responde `semFala: true` + `texto: ""` → não injeta nada, sem estado especial.
- Solta a tecla antes do job anterior terminar → fila sequencial (`std::deque` de 1 pedido: `maxConcurrency` do motor é 1).
- Modelo fixo por sessão (trocar custa 5-10 s no nó), escolhido na janela de opções.

---

## 3. Janela de opções = espelho literal do contrato do gate

Nenhuma opção que não exista na doc: `url` (base do gate) · `idioma` · `saida` · `modelo` · `prompt` · `vad` · `recortar` · `threshold` · `min_speech_ms` · `min_silence_ms` · `speech_pad_ms` · `max_speech_s` · `palavras` · `beam` · `label` · `timeout_ms` + as 4 do cliente: `tecla`, `dispositivo`, `metodo_de_insercao`, `mostrar_sem_fala`.

Cada campo da UI escreve exatamente a chave que a rota/`items[].input` aceita. Zero vocabulário próprio.

---

## 4. Segurança e erro

- **Config**: um único `%APPDATA%\ditado\config.json`; escrita atômica (`temp` + `MOVEFILE_REPLACE_EXISTING`); ACL restrita ao usuário.
- **Panic/SEH**: `SetUnhandledExceptionFilter` → loga o código da exceção + `.dmp` em `logs/` em vez de fechar calado; sem exceção cruzando borda do hook (`__try/__except` nos callbacks).
- **Erro visível**: cada `error` do gate (`invalid_item`, `input_too_large`, `engine_unavailable`, `engine_error`, `transcription_timeout`) → balão no tray com o `detail` do nó + `jobId` no log para `GET /v1/jobs/:jobId`.
- **Falha não-corruptora**: se o gate cair no meio, o WAV vai para `pending/*.wav` e é reenviado na próxima conexão — a frase do usuário nunca se perde.
- **Timeout**: `timeout_ms` do gate + folga de 5 s no cliente (via `WinHttpSetOption`), para o app não travar se o nó travar.
- **Sem telemetria**: nenhuma chamada de rede além do `url` configurado.

---

## 5. Fases

| Fase | Entrega | Critério de aceite |
|---|---|---|
| **P0** | repo + `CMakeLists` + single-instance (`CreateMutexW`) + tray com menu + config | ícone na bandeja, menu abre, config reaparece após salvar |
| **P1** | hook LL da tecla + WASAPI → WAV 16k mono em RAM + botão "testar" (grava → texto no log) | WAV de 3 s do microfone salvo e ouvido, duração correta |
| **P2** | `POST /v1/transcricao` com query completa + `504`→`jobId` + `semFala` | fala real → texto no log; sem fala → `semFala: true` silencioso |
| **P3** | `SendInput`/`Ctrl+V` + restauração do clipboard | **demo ponta a ponta**: Notepad/Chrome/Teams — segura tecla, fala, solta, texto no cursor |
| **P4** | janela de opções com 100% dos campos do gate | cada campo alterado muda a query enviada (visto no eco do gate) |
| **P5** | log com rotação, `pending/*.wav`, autostart, `--version`, ícone por estado | matar o `whisper.service` no nó → balão `engine_unavailable`, reenvio automático depois |
| **P6** | Inno Setup + `ditado-setup.exe` na tag `v0.1.0` + branch `main` | instalar em perfil limpo → ícone na bandeja na hora, sem UAC |

---

## 6. Riscos e mitigação (as armadilhas da doc, já mapeadas)

| Risco | Mitigação |
|---|---|
| Campo que ignora `KEYEVENTF_UNICODE` (jogos, apps elevados por UIPI) | modo `Ctrl+V` na UI; logar quando um campo recusar o evento |
| `vad=0` → nó alucina frase em silêncio | o app **nunca** envia `vad=0`; o toggle nem aparece desligável em v1 |
| Trocar de modelo por requisição → +5-10 s | um modelo por app, `modelo` fixo vindo da config |
| `maxConcurrency 1` → 2ª fala na fila | tooltip com estado (`transcrevendo…`); clique durante o job é enfileirado, nunca perdido |
| `saida` ≠ `idioma` → `400` | a UI não permite escolher (não há tradução); `saida` só ecoa `idioma` |
| `speech_pad_ms` "comendo" consoante (100 → 150-200) | campo `speech_pad_ms` na UI, default `100` da doc |
| Áudio > 64 MB | clamp no cliente (`duracao × 32000 > 64_000_000` → `input_too_large` no log, o gate nem é chamado) |
