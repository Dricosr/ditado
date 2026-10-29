# ditado

**Ditado por voz para qualquer campo de texto no Windows.** Segure a combinação de teclas, fale, solte — o áudio vai para o `gate`, volta como texto e é digitado no campo ativo.

![release](https://img.shields.io/github/v/release/Dricosr/ditado?label=release)
![platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)
![license](https://img.shields.io/badge/license-MIT-green)

- **Sem framework de UI**: C++20/Win32 puro (hook de teclado, tray, WASAPI, WinInet).
- **Sem DLLs de runtime**: CRT estático (`/MT`); o `.exe` é autocontido (com os sons embutidos).
- **Transcrição no gate**: o app só grava, envia o WAV e digita a resposta — sem VAD, streaming ou modelo local.
- **Feedback auditivo**: pop ao começar/terminar e um loop discreto enquanto transcreve.
- **Vários ditados seguidos**: fila FIFO; os textos aparecem na ordem em que foram falados.
- **Histórico do dia** na bandeja, com copiar um registro para o clipboard.

> Especificação completa do estado atual: **[docs/ESPECIFICACAO.md](docs/ESPECIFICACAO.md)**.

---

## Instalação

1. Baixe o instalador da release mais recente:  
   <https://github.com/Dricosr/ditado/releases/latest>
2. Rode `ditado-setup.exe`. Ele instala **por usuário**, em `%LOCALAPPDATA%\Programs\ditado`, **sem UAC**.
3. O app abre ao final e fica na bandeja (canto inferior direito).

Também é possível instalar em silêncio:

```bat
ditado-setup.exe /VERYSILENT /SUPPRESSMSGBARBOXTAGS /NORESTART
```

Requisitos: **Windows 10/11 x64** e acesso ao gate configurado.

---

## Uso

1. **Segure `ctrl+shift+space`** (configurável), fale e **solte**.
2. O app toca um **pop in** ao começar, um **pop out** ao terminar e um **loop de processamento** enquanto o gate transcreve.
3. O texto é digitado no campo que estiver ativo.
4. **Clique esquerdo** no ícone da bandeja → Opções.
5. **Clique direito** no ícone → menu:
   - Opções…
   - Testar microfone
   - **Histórico do dia…**
   - Iniciar com o Windows (toggle)
   - Sair
6. No histórico, selecione um registro e clique em **Copiar** (ou dê duplo clique) para mandar o texto completo ao clipboard.

Você pode gravar várias frases em sequência. As gravações entram numa fila e os textos são digitados na mesma ordem; o tooltip mostra `transcrevendo… N na fila`.

---

## Feedback auditivo

Os três WAVs estão embutidos no executável como recursos (`WAVE` 201/202/203):

| Momento | Som |
|---|---|
| começa a gravar | `popin.wav` |
| solta a tecla | `popout.wav` |
| há jobs pendentes | `running.wav` em loop, ~2 s após o pop out |

O loop para quando a fila esvazia. Uma nova gravação interrompe o loop e toca o pop in de novo.

> Se você usa **alto-falantes**, o pop inicial pode ser capturado pelo microfone junto com a fala; o gate tende a ignorar esse trecho. **Fones de ouvido** evitam a contaminação.

---

## Histórico diário

O histórico é **sempre** gravado, mesmo com o log de desenvolvimento desligado:

```
%APPDATA%\ditado\history\AAAAMMDD.jsonl
```

Cada linha contém `quando`, `texto`, `jobId`, `modelo` e `duracaoS`. A janela mostra os registros de hoje, mais novo primeiro; o botão **Copiar** coloca o texto completo do item selecionado no clipboard.

---

## Configuração

Arquivo: `%APPDATA%\ditado\config.json` (escrita atômica).

### O que a janela de Opções expõe

| Campo | Default | Para quê |
|---|---|---|
| `url` | `http://192.168.18.150:3070` | endereço do gate |
| `tecla` | `ctrl+shift+space` | combinação push-to-talk |
| `modelo` | `small` | `small` / `turbo` / `large-v3` |
| `dispositivo` | vazio = padrão | id WASAPI; a UI lista o nome amigável (ex.: `Microphone (UGREEN Camera)`) |
| `autostart` | `false` | inicia com o Windows |
| `log_local` | `true` | log de desenvolvimento (liga/desliga tudo) |

### Campos avançados do gate (no `config.json`)

| Campo | Default | Nota |
|---|---|---|
| `idioma` | `pt` | idioma de entrada; o gate não traduz |
| `prompt` | vazio | glossário/hotwords |
| `threshold` | `0.5` | VAD do gate |
| `min_silence_ms` | `300` | VAD do gate |
| `speech_pad_ms` | `100` | VAD do gate |
| `max_speech_s` | `30` | VAD do gate |
| `palavras` | `false` | timestamps por palavra |
| `beam` | `1` | precisão |
| `label` | `ditado` | rótulo no painel do gate |
| `timeout_ms` | `120000` | espera máxima |
| `app` | `ditado` | header `X-Gate-App` |
| `metodo_insercao` | `unicode` | `unicode` (SendInput) ou `area` (clipboard + Ctrl+V) |
| `mostrar_sem_fala` | `false` | carregado/salvo; **não usado** hoje |

> A UI atual não expõe os campos avançados; eles ficam no `config.json`.

---

## Como o gate é chamado

```http
POST {url}/v1/transcricao?idioma=pt&modelo=small&...
Content-Type: audio/wav
X-Gate-App: ditado
User-Agent: Ditado/<versão> (Windows 11)
```

Corpo = **WAV cru** (16 kHz mono s16). O app `ditado` é **aberto** no gate: só `X-Gate-App: ditado` identifica, sem token. O gate devolve:

```json
{
  "jobId": "job-…",
  "texto": "…",
  "semFala": false,
  "fala": { "segmentos": [ … ], "recortado": true },
  "modelo": "small",
  "estado": "succeeded"
}
```

O app usa `texto`/`semFala`; `saida` não é enviado (o gate não traduz). Job `ok` + `semFala` não digita nada. Erros (`401`, `413`, `502`, `503`, `504`) geram balão e o WAV fica em `pending/`.

O **cliente** no painel do gate é o campo `agent`, extraído do `User-Agent`. O app envia `Ditado/<versão> (Windows 11)`; sem header o painel mostra `-`.

Limites: áudio ≤ 64 MB; `transcricao` roda em lane paralela com `maxConcurrency 1`; VAD/recorte são do servidor; `small` ~1,5 s, `turbo` ~6 s, `large-v3` ~9 s.

---

## Onde ficam os arquivos

| Caminho | Conteúdo |
|---|---|
| `%APPDATA%\ditado\config.json` | configuração |
| `%APPDATA%\ditado\logs\app-AAAAMMDD.log` | opções, eventos, cliente, request/resposta, erros |
| `%APPDATA%\ditado\audio\AAAAMMDD-HHMMSS.wav` | WAV enviado ao gate (log local ligado; mantém os 30 mais novos) |
| `%APPDATA%\ditado\history\AAAAMMDD.jsonl` | histórico diário (sempre) |
| `%APPDATA%\ditado\pending\*.wav` | áudio de job que falhou; reenviado na próxima subida |

Com `log_local=false`, **nada** é escrito em `logs/` ou `audio/`; histórico e `pending` continuam funcionando.

---

## Build

```bat
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Saída: `build\ditado.exe` (~2,36 MB, com os WAVs embutidos).

### Instalador

```bat
ISCC.exe installer\ditado.iss
```

Saída: `build\installer\ditado-setup.exe` (~3,1 MB). Inno Setup 7; `PrivilegesRequired=lowest`; `ArchitecturesAllowed=x64compatible`; `SetupIconFile=src\icon.ico`.

---

## Modos de teste e ferramentas

```bat
ditado.exe --version      :: registra versão/gate no log (se ligado) e encerra
ditado.exe --selftest     :: abre a janela de Opções e encerra
ditado.exe --historico    :: abre o histórico
ditado.exe --ditar fala.wav :: E2E: WAV → gate → texto digitado
```

Ferramentas em `tools/`:

- `gate-test` (C#/.NET 10): `T.exe --tts <texto>`, `--transcreve <wav>`, `--ditar <texto>`. O binário em `tools/gate-test/bin/pub/` é `win-x64` framework-dependent (precisa do runtime **.NET 10**) e envia `User-Agent: Ditado-teste/0.1 (Windows 11)`.
- `capture-test.cpp` — sonda a captura com um tom nos alto-falantes e imprime amostras, rms e duração.
- `endpoints.cpp` — lista dispositivos de captura ativos com nome amigável.
- `e2e-test` — legado (simula teclas no Notepad; o keyup injetado nem sempre chega ao hook, então o E2E oficial é `--ditar`).

---

## Arquitetura resumida

```
WH_KEYBOARD_LL ─► estado (main) ─► audio::Start/Stop ─► fila FIFO ─► JobWorker
                                                  │                    │
                                          pop in/out + loop      POST /v1/transcricao
                                                  │                    │
                                                  ▼                    ▼
                                             PlaySound            inject::Texto
                                                  │                    │
                                                  └──── history::Add ◄─┘
```

- **Audio worker**: client WASAPI preparado no início; `Start` liga o stream; packets em frames → downmix → resample linear 16 kHz → PCM s16.
- **Job worker**: único, envia na ordem das gravações; só digita quando o usuário não está gravando; erros vão para `pending/`.
- **Histórico**: JSONL diário; janela no tray com copiar.

Detalhes de implementação, limites e correções: **[docs/ESPECIFICACAO.md](docs/ESPECIFICACAO.md)**.

---

## Solução de problemas

- **Microfone indisponível / `AUDCLNT_E_DEVICE_INVALIDATED`**: outro app (Teams, Webex, etc.) pode estar usando o dispositivo de forma exclusiva. Feche-o ou escolha outro dispositivo em Opções. O app tenta reabrir a cada gravação; desde a 0.3.1 ele ignora o auto-repeat do keydown e mostra **uma** notificação por falha, não dezenas.
- **WAVs antigos curtos/vazios**: eram do build anterior (bug de frames vs bytes, corrigido na 0.2.1). Os novos WAVs têm a duração real.
- **Cliente `-` no gate**: o campo é `agent` (User-Agent). O app e o gate-test agora enviam identificação; jobs antigos de clientes sem header ficam sem cliente.
- **Gate pausado/bloqueado**: uso externo do Strata pode parar os motores; a lane de transcrição é paralela. O painel mostra o motivo.
- **Texto antigo digitado ao abrir**: `pending/` é reenviado na subida por robustez; isso pode injetar uma frase antiga fora de contexto.
- **`--version` não imprime no console**: é um app WIN32; a versão vai para o log (quando o log está ligado).
- **Feedback nos alto-falantes**: o pop pode entrar na captura; use fones se isso atrapalhar.

---

## Histórico de versões

| Versão | Entrega |
|---|---|
| 0.1.0 | app base: tray, hotkey, WASAPI, gate, SendInput, instalador per-user |
| 0.2.0 | log local opcional, ícone próprio, opções com dispositivo |
| 0.2.1 | captura completa (frames vs bytes), stream preparado, nomes reais dos dispositivos |
| 0.2.2 | cliente explícito no gate (`User-Agent Ditado/<versão> (Windows 11)`) |
| 0.3.0 | pop in/out, loop de processamento, fila de vários ditados na ordem, histórico diário com copiar |
| 0.3.1 | fim das notificações repetidas de microfone (auto-repeat do keydown e uma notificação por falha) |

---

## Estrutura do repositório

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
   ├─ gate-test/
   ├─ capture-test.cpp
   ├─ endpoints.cpp
   └─ e2e-test/
```

---

## Licença

MIT — ver [LICENSE](LICENSE). O JSON vendorizado (`third_party/nlohmann/json.hpp`) também é MIT.
