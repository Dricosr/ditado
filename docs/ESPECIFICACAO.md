# ditado — especificação do sistema

**Estado:** versão **0.3.0** · atualizado em **28/10/2026**  
**Repositório:** <https://github.com/Dricosr/ditado> · **Release:** <https://github.com/Dricosr/ditado/releases/tag/v0.3.0>  
**Licença:** MIT (ver `LICENSE`).

Este documento descreve o **estado atual** do ditado (implementação, contratos, caminhos e limites), não um plano futuro.

---

## 1. Objetivo

App Windows residente na bandeja, *push-to-talk*:

> segurar `ctrl+shift+space` → falar → soltar → o WAV vai ao gate → o texto é digitado no campo ativo.

O app é deliberadamente pequeno: grava, envia e digita. A transcrição, o VAD, o recorte de silêncio e o modelo ficam no **gate** (`POST /v1/transcricao`). Não há tradução, streaming nem modelo local.

**Não objetivos:** editar áudio, VAD no cliente, tradução, diarização, gravação contínua, UI framework.

---

## 2. Ambiente

| Item | Requisito |
|---|---|
| SO | Windows 10/11 x64 |
| Executável | autocontido; CRT estático (`/MT`); sem DLLs de runtime |
| Gate | `http://192.168.18.150:3070` por padrão; configurável em Opções |
| Auth | sem token; `X-Gate-App: ditado` é obrigatório |
| Microfone | dispositivo WASAPI shared; escolhido em Opções |
| Feedback | alto-falantes ou fones (ver §7) |
| Build | VS 2026 Build Tools + SDK, CMake + Ninja; Inno Setup 7 para o instalador |

O **gate** identifica o cliente pelo header `User-Agent` → campo `agent` (coluna “cliente” no painel).

---

## 3. Arquitetura

```
WH_KEYBOARD_LL
      │ down/up
      ▼
thread de mensagem (main)
  máquina de estados ── pop in/out ── PlaySound (recursos 201/202/203)
      │ WAV (PCM 16 kHz mono)
      ▼
fila FIFO de jobs ── JobWorker (thread única)
      │ POST /v1/transcricao
      ▼
gate (transcrição Whisper, lane paralela, maxConcurrency 1)
      │ texto / semFala / erro
      ▼
inject::Texto (SendInput Unicode ou clipboard)
      │
      ▼
history::Add <dir>\history\AAAAMMDD.jsonl
```

Threads principais:

1. **Main / mensagem** — hook de teclado, tray, janela de opções, janela de histórico, máquina de estados.
2. **Audio worker** — client WASAPI preparado uma vez; `Start` liga o stream, `Stop` para e drena; buffer s16 mono 16 kHz.
3. **Job worker** — um único worker consome a fila FIFO, envia um job por vez ao gate, injeta e grava o histórico. A ordem das gravações é preservada.
4. **Feedback** — `PlaySound` assíncrono com recursos embutidos; o loop de processamento é agendado após o pop out.

Máquina de estados:

```
Ocioso ──(keydown)──► Gravando ──(keyup)──► fila/transcrevendo ──► Ocioso
```

O usuário pode gravar novamente enquanto jobs anteriores estão na fila. O worker **não digita** enquanto `Gravando` (evita `SendInput` com Ctrl/Shift presos); ele espera o keyup.

---

## 4. Componentes e arquivos

| Arquivo | Responsabilidade |
|---|---|
| `src/main.cpp` | single-instance, config, hook, máquina de estados, fila, worker, tray, modos de teste |
| `src/audio.{h,cpp}` | WASAPI shared, downmix, resample linear para 16 kHz, stop/flush, preparação do client |
| `src/gate.{h,cpp}` | `POST /v1/transcricao`, query string, `User-Agent`, leitura do JSON de resposta |
| `src/hotkey.{h,cpp}` | `WH_KEYBOARD_LL`, parse do combo, normalização L/R, callback down/up |
| `src/inject.{h,cpp}` | `SendInput` Unicode; modo clipboard + Ctrl+V com restauração; copiar para clipboard |
| `src/config.{h,cpp}` | `config.json`, defaults, escrita atômica, `Dump()`, `ToQuery()` |
| `src/settings.{h,cpp}` | janela de Opções (`CreateWindowExW`), lista de dispositivos com nome amigável |
| `src/tray.{h,cpp}` | ícone, menu, tooltip, balão |
| `src/log.{h,cpp}` | log diário opcional, WAV de log, cap de 30 arquivos |
| `src/som.{h,cpp}` | pop in/out e loop de processamento |
| `src/history.{h,cpp}` | histórico diário JSONL e janela com copiar |
| `src/app.rc` | ícone 101 e recursos WAVE 201/202/203 |
| `src/assets/*.wav` | popin, popout, running (44,1 kHz, estéreo, 24 bits) |
| `src/version.h.in` | versão gerada pelo CMake |
| `third_party/nlohmann/json.hpp` | JSON vendorizado (MIT) |

---

## 5. Contrato com o gate

### 5.1 Requisição

```http
POST {url}/v1/transcricao?idioma=pt&modelo=small&threshold=0.5&...
Content-Type: audio/wav
X-Gate-App: ditado
User-Agent: Ditado/<versão> (Windows 11)
```

Corpo = **WAV cru** (16 kHz mono s16 gerado pelo app). Sem JSON de job.

### 5.2 Parâmetros enviados

`ToQuery()` envia: `idioma`, `modelo`, `threshold`, `min_silence_ms`, `speech_pad_ms`, `max_speech_s`, `beam`, `label`, `timeout_ms`, `palavras`; `prompt` só se não vazio.

### 5.3 Resposta

```json
{
  "jobId": "job-…",
  "texto": "…",
  "semFala": false,
  "idioma": "pt",
  "idiomaSaida": "pt",
  "probabilidadeIdioma": 1,
  "duracaoAudioS": 3.76,
  "inferenciaMs": 1504,
  "rtf": 0.4,
  "fala": { "temFala": true, "trechos": 1, "segmentos": [ … ], "recortado": true },
  "modelo": "small",
  "estado": "succeeded"
}
```

O app usa `texto`/`semFala`/`erro`/`detail`/`jobId`; não usa `saida`.

### 5.4 Erros

`401 unauthorized`, `413 input_too_large`, `501 engine_not_implemented`, `502 engine_error`, `503 engine_unavailable`, `504 transcription_timeout` (com `jobId`). O app mostra balão e mantém o WAV em `pending/`.

### 5.5 Limites e comportamento do gate

- áudio ≤ 64 MB;
- `transcricao` é CPU, lane paralela, `maxConcurrency 1`;
- VAD/recorte no servidor;
- sem tradução (`saida != idioma` → erro);
- `small` ~1,5 s, `turbo` ~6 s, `large-v3` ~9 s; troca de modelo residente custa 5–10 s;
- sem token para o app `ditado` (aberto).

---

## 6. Captura de áudio

- **WASAPI shared**, `AUDCLNT_STREAMFLAGS_EVENTCALLBACK`, `AUTOCONVERTPCM`, `SRC_DEFAULT_QUALITY`; buffer padrão do engine.
- O client é **preparado no início do app** (`audio::Prepare`) e fica parado entre ditados. `Start` apenas liga o stream; assim o começo da fala não é perdido na inicialização.
- Cada gravação começa com `g.buf` limpo e `IAudioClient::Reset()`.
- `GetNextPacketSize`/`GetBuffer` devolvem **frames**, não bytes. O app usa `frames` diretamente e `ReleaseBuffer(frames)`.
- Downmix de N canais para mono; resample **linear** para exatamente **16 kHz**; pacotes `SILENT` viram zeros; saída `int16_t`.
- `Stop()` para o stream, drena os últimos pacotes, espera o worker e devolve o buffer em `Flush()`.
- A falha de captura é logada e o keydown mostra balão (“Microfone indisponível…”). O app tenta reabrir em cada gravação.

### Correção histórica importante

Na versão 0.1/0.2.0, o tamanho do pacote (frames) era tratado como bytes em `Mix`, dividindo o áudio por `canais × bytes_por_amostra`. Isso produzia WAVs ~1/8 da duração (ou curtos/vazios). Corrigido na 0.2.1; a captura atual devolve a duração real (ex.: 3,5 s → 56.000 amostras = 3,51 s).

### Dispositivo

`audio::Prepare` usa o id salvo em `dispositivo`; vazio = endpoint padrão do sistema. Na janela de Opções, os dispositivos ativos são listados com `PKEY_Device_FriendlyName` (fallback `PKEY_Device_DeviceDesc`), ex. `Microphone (UGREEN Camera)`. Se o id salvo não existir, a UI seleciona o padrão ou o primeiro.

---

## 7. Feedback auditivo

Recursos embutidos no `.exe` (tipo `WAVE`):

| ID | Arquivo | Uso |
|---|---|---|
| 201 | `src/assets/popin.wav` | pop ao começar a gravar |
| 202 | `src/assets/popout.wav` | pop ao soltar |
| 203 | `src/assets/running.wav` | loop discreto enquanto transcreve |

Sequência:

1. keydown → captura ligada → `som::PopIn()`.
2. keyup → captura parada → `som::PopOut()`.
3. ~2 s depois (para o pop out ser audível) → se há jobs pendentes e não está gravando, `som::Running(true)`.
4. quando a fila esvazia → `som::Running(false)`.
5. nova gravação → `PopIn()` interrompe o loop; novo ciclo.

Os arquivos têm ~2 s, 44,1 kHz, estéreo, 24 bits; `PlaySound` toca via dispositivo padrão. Se você usa alto-falantes, o pop inicial pode entrar no WAV capturado; o gate normalmente ignora esse trecho (teste com `popin.wav` isolado devolve `semFala`). Fones evitam a contaminação. Feedback não é configurável pela UI.

---

## 8. Vários jobs e ordem

- Cada gravação não vazia vira um `Job` com `seq` crescente, `pcm`, e uma **cópia** da config.
- A fila é `std::deque<Job>` FIFO, protegida por mutex/condition variable.
- **Um único worker** envia ao gate em ordem; o gate tem `maxConcurrency 1`, então não há ganho em paralelizar.
- O worker espera `!g_rec` antes de digitar: se o usuário já está gravando o próximo trecho, o texto anterior é digitado após o keyup.
- Resultado:
  - `ok` + `!semFala` → `inject::Texto`, `history::Add`;
  - `ok` + `semFala` → nada digitado;
  - erro → balão, WAV salvo em `pending/`, segue para o próximo job.
- `g_pending` alimenta o tooltip (`transcrevendo… N na fila`) e o loop de som.
- O worker é único, então os textos saem na mesma ordem das gravações; não há mapa de resultados fora de ordem.

Na subida, arquivos `%APPDATA%\ditado\pending\*.wav` são reenviados (na ordem em que o sistema de arquivos os lista) e apagados individualmente quando o gate responde `ok`.

---

## 9. Inserção no campo ativo

`metodo_insercao`:

- `unicode` (padrão): `SendInput` com `KEYEVENTF_UNICODE`, uma code unit por evento (surrogate pairs = 2 eventos).
- `area`: clipboard temporário + `Ctrl+V`, restaurando o conteúdo anterior do clipboard.

O worker só injeta quando não está gravando. O histórico usa `inject::Copiar` para colocar o texto selecionado no clipboard.

---

## 10. Histórico diário

Sempre gravado, independente de `log_local`:

```
%APPDATA%\ditado\history\AAAAMMDD.jsonl
```

Uma linha JSON por texto transcrito com sucesso:

```json
{"quando":"18:45:59","texto":"…","jobId":"job-…","modelo":"small","duracaoS":3.76}
```

- Menu da bandeja: **“Histórico do dia…”**.
- Janela lista os registros de hoje, **mais novo primeiro**, com hora e trecho (até 180 caracteres).
- **Copiar** ou duplo clique copia o **texto completo** do registro selecionado para o clipboard.
- A janela é um snapshot do dia ao abrir; novos registros aparecem ao reabrir.
- `--historico` abre a mesma janela (modo de teste).

---

## 11. Configuração

Arquivo: `%APPDATA%\ditado\config.json` (escrita atômica com temp + `MoveFileExW`).

| Campo | Default | Uso | Na UI? |
|---|---|---|---|
| `url` | `http://192.168.18.150:3070` | base do gate | sim |
| `tecla` | `ctrl+shift+space` | combo push-to-talk | sim |
| `modelo` | `small` | `small` / `turbo` / `large-v3` | sim (combo) |
| `dispositivo` | vazio | id WASAPI; vazio = padrão | sim (combo com nome) |
| `autostart` | `false` | inicia com o Windows | sim (checkbox) |
| `log_local` | `true` | log de desenvolvimento | sim (checkbox) |
| `idioma` | `pt` | idioma de entrada | não |
| `prompt` | vazio | hotwords/glossário | não |
| `threshold` | `0.5` | VAD do gate | não |
| `min_silence_ms` | `300` | VAD do gate | não |
| `speech_pad_ms` | `100` | VAD do gate | não |
| `max_speech_s` | `30` | VAD do gate | não |
| `palavras` | `false` | timestamps por palavra | não |
| `beam` | `1` | precisão | não |
| `label` | `ditado` | rótulo no painel | não |
| `timeout_ms` | `120000` | espera máxima | não |
| `app` | `ditado` | `X-Gate-App` | não |
| `metodo_insercao` | `unicode` | `unicode` / `area` | não |
| `mostrar_sem_fala` | `false` | carregado/salvo, **não usado** hoje | não |

A UI expõe apenas os seis primeiros; os demais são editáveis no `config.json`. Essa é uma limitação atual, não um contrato do gate.

`Config::Dump()` registra todas as opções no log local.

---

## 12. Caminhos e dados

| Caminho | Conteúdo | Quando |
|---|---|---|
| `%APPDATA%\ditado\config.json` | configuração | sempre |
| `%APPDATA%\ditado\logs\app-AAAAMMDD.log` | opções, eventos, cliente, request/resposta, erros | se `log_local` |
| `%APPDATA%\ditado\audio\AAAAMMDD-HHMMSS.wav` | WAV enviado ao gate | se `log_local`; mantém 30 mais novos |
| `%APPDATA%\ditado\history\AAAAMMDD.jsonl` | histórico diário | sempre |
| `%APPDATA%\ditado\pending\*.wav` | áudio de job que falhou | criado em erro, reenviado na subida |

Com `log_local=false`, **nada** é escrito em `logs/` ou `audio/`; o histórico e o `pending` continuam.

---

## 13. Tray e janelas

- **Clique esquerdo** no ícone → Opções.
- **Clique direito** → menu:
  1. Opções…
  2. Testar microfone
  3. Histórico do dia…
  4. Iniciar com o Windows (toggle)
  5. Sair
- Tooltip: `pronto`, `gravando…`, `transcrevendo… N na fila`, `configurando…`.
- Balões para erro de microfone e erro do gate.
- Opções: URL, tecla, modelo, autostart, log local, dispositivo.
- Histórico: lista + Copiar + Fechar.

---

## 14. Instalador e build

### Build

```bat
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Saída: `build\ditado.exe` (~2,36 MB, com os três WAVs embutidos).

### Instalador

`installer/ditado.iss` (Inno Setup 7):

- `PrivilegesRequired=lowest` → sem UAC, instala em `{localappdata}\Programs\ditado`;
- `ArchitecturesAllowed=x64compatible`;
- `SetupIconFile=..\src\icon.ico`;
- atalho no menu Iniciar com o ícone;
- `[Run]` abre o app no fim (interativo; `skipifsilent` no silent install).

```bat
ISCC.exe installer\ditado.iss
```

Saída: `build\installer\ditado-setup.exe` (~3,1 MB). Release atual: **v0.3.0**.

---

## 15. Modos de teste e ferramentas

| Comando | Efeito |
|---|---|
| `ditado.exe --version` | registra versão/gate no log (se ligado) e encerra |
| `ditado.exe --selftest` | abre a janela de Opções e encerra |
| `ditado.exe --historico` | abre a janela do histórico |
| `ditado.exe --ditar <wav>` | E2E: usa o WAV do gate, transcreve, digita no campo ativo |
| `--autostart` | flag usada pelo registro de autostart; não altera o comportamento |

Ferramentas em `tools/`:

- `gate-test` (C#/.NET 10): `T.exe --tts <texto>`, `--transcreve <wav>`, `--ditar <texto>`; agora envia `User-Agent: Ditado-teste/0.1 (Windows 11)`;
- `capture-test.cpp` — sonda a captura com tom nos alto-falantes e imprime amostras/rms/duração;
- `endpoints.cpp` — lista dispositivos de captura ativos com nome amigável;
- `e2e-test` — legado (simula teclas/Notepad; o keyup injetado nem sempre chega ao hook, então o E2E oficial é `--ditar`).

O binário `tools/gate-test/bin/pub/T.exe` é publicado para `win-x64` **framework-dependent**; precisa do runtime **.NET 10**.

---

## 16. Segurança, erros e privacidade

- Instância única (`Local\ditado.instancia`).
- `SetUnhandledExceptionFilter` loga código de exceção em vez de fechar calado.
- Config atômica; sem telemetria; única rede é o gate configurado.
- Áudio só é enviado quando o usuário solta a tecla; não há gravação contínua.
- `pending` preserva a frase em falha de rede/timeout; é reenviada na próxima subida.
- Erros do gate → balão com `detail` e `jobId`; `timeout_ms` do gate + folga de 5 s no cliente.
- Microfone exclusivo por outro app pode invalidar o endpoint; o app tenta reabrir a cada gravação e avisa se falhar.

---

## 17. Limitações atuais

1. A janela de Opções **não** expõe todos os parâmetros do gate; os avançados ficam no `config.json`.
2. `mostrar_sem_fala` está no config, mas não é usado.
3. `--version` grava no log; não imprime em console (app WIN32).
4. O feedback usa o dispositivo de saída padrão; com alto-falantes o pop pode ser capturado pelo microfone.
5. O histórico é um snapshot ao abrir; não se atualiza ao vivo.
6. Um único worker de transcrição: a ordem é garantida, mas não há envio concorrente (o gate também é `maxConcurrency 1`).
7. `pending` reenviado na subida pode injetar texto antigo fora do contexto atual; é uma escolha de robustez.
8. Sem tradução, sem VAD local, sem streaming.
9. Jobs de `transcricao` rodam em lane paralela; outros motores (imagem, tts, etc.) podem esperar quando o Strata está em uso externo. O painel mostra o motivo.

---

## 18. Histórico de versões

| Versão | Entrega |
|---|---|
| 0.1.0 | app base: tray, hotkey, WASAPI, gate, SendInput, instalador per-user, release |
| 0.2.0 | log local opcional, ícone próprio, janela de opções com dispositivo |
| 0.2.1 | correção da captura (frames vs bytes), stream preparado, nomes reais dos dispositivos, log opcional real |
| 0.2.2 | cliente explícito no gate (`User-Agent Ditado/<versão> (Windows 11)`) |
| 0.3.0 | pop in/out, loop de processamento, fila de vários ditados na ordem e histórico diário com copiar |

---

## 19. Estrutura do repositório

```
ditado/
├─ CMakeLists.txt
├─ LICENSE
├─ README.md
├─ docs/ESPECIFICACAO.md
├─ installer/ditado.iss
├─ src/
│  ├─ main.cpp
│  ├─ audio.*  gate.*  hotkey.*  inject.*
│  ├─ config.* settings.* tray.* log.*
│  ├─ som.*  history.*
│  ├─ app.rc  icon.ico  version.h.in
│  └─ assets/popin.wav  popout.wav  running.wav
├─ third_party/nlohmann/json.hpp
└─ tools/
   ├─ gate-test/           (C#/.NET 10)
   ├─ capture-test.cpp
   ├─ endpoints.cpp
   └─ e2e-test/            (legado)
```

---

## 20. Roadmap sugerido

- Expor os parâmetros avançados do gate na janela de Opções.
- Configurar volume/arquivo dos sons e permitir desligar o feedback.
- Navegação por dia no histórico (além de hoje).
- Retry/backoff explícito para `pending` em vez de só na subida.
- Teste automatizado da ordem da fila (sem depender de tecla física).
- Token opcional para o app no gate.
