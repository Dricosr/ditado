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

| Fase | Entrega | Estado |
|---|---|---|
| **P0** | repo + CMakeLists + single-instance + tray + config atômica | ✅ 26/10 — `--selftest`, instância única, config round-trip |
| **P1** | hook LL + WASAPI → WAV 16k mono na RAM | ✅ 27/10 — `audio.cpp`; hook + estado validados com tecla injetada (down→Gravando ✓) |
| **P2** | POST /v1/transcricao (WinInet) + `semFala` + erros no balão | ✅ 27/10 — `gate.cpp`; --ditar transcreveu o WAV do gate ✓ |
| **P3** | SendInput UNICODE no campo ativo | ✅ 27/10 — `inject.cpp`; E2E: fala do gate → texto digitado sozinho ✓ |
| **P4** | opções com os campos do gate | ✅ 27/10 — diálogo em memória; url/modelo/tecla/autostart vivos |
| **P5** | `pending/*.wav`, `--version`/`--help`, ícone por estado | ⏳ restante (logs ✓ autostart ✓) |
| **P6** | Inno Setup + release v0.1.0 no GitHub | ⏳ |

**Medido no E2E (27/10):** `--ditar fala.wav` → gate devolve o texto do TTS → app digita no campo ativo ✓✓✓.

---

## 6. Riscos e mitigação — atualizado após a implementação

| Fato medido em produção | Consequência no código |
|---|---|
| `saida` NÃO existe na rota (400 `invalid_item`) | `ToQuery()` só envia o que a rota aceita `idioma`/`modelo`/... |
| `threshold=0.5` vira `0.500000` no ostringstream | ok — o gate aceita float puro |
| URL_COMPONENTS: `lpszHostName/UrlPath` exigem buffer do chamador | `gate.cpp` dá `host[256]/path[2048]` (erro 87 sem isso) |
| `HttpQueryInfo(STATUS_CODE)` falha (12150) mesmo com 200 OK | o código lê o **corpo JSON** (`texto`/`error`), não o status |
| Up de tecla injetado nem sempre chega ao hook LL (sandbox) | E2E oficial = `--ditar` (percorre o estado real); teclado físico funciona normal |
