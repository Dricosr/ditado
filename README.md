# ditado

Ditado por voz para qualquer campo de texto no Windows: **segure a combinação de teclas → fale → solte → o texto é digitado no campo ativo**.

- **Sem framework**: C++20/Win32 puro (hook de teclado, tray, WASAPI, WinInet) — um `.exe` de ~400 KB, sem DLLs de runtime.
- **Transcrição no `gate`** (nanosaurus `transcricao`): o app só grava, sobe o WAV e digita a resposta.

## Uso

1. Instale o `ditado-setup.exe` (instala por usuário, sem UAC) e rode.
2. O ícone aparece na bandeja (canto inferior direito). Clique nele para abrir as opções.
3. Segure `ctrl+shift+space` (configurável), fale, solte.

## Config (`%APPDATA%\ditado\config.json`)

Os campos do motor têm o mesmo nome da API do gate (`idioma`, `modelo`, `prompt`, `threshold`, `min_silence_ms`, `speech_pad_ms`, `max_speech_s`, `palavras`, `beam`, `label`, `timeout_ms`) — o app não inventa vocabulário. `url` é o endereço do gate. O dispositivo de captura aparece na UI com o nome amigável do Windows (ex.: `Microphone (UGREEN Camera)`).

O gate identifica quem pediu pelo header `User-Agent` (o campo `agent`, mostrado como **cliente** no painel). O app envia `Ditado/<versão> (Windows 11)`; sem isso o painel mostra `-`.

## Log local (opcional, para desenvolvimento)

Ligue `Log local (dev)` em **Opções**. Ele grava todas as opções, o áudio gravado e o que foi enviado/recebido do gate:

- `%APPDATA%\ditado\logs\app-AAAAMMDD.log` — opções (JSON), eventos, request e resposta do gate, incluindo o `cliente` (User-Agent) enviado.
- `%APPDATA%\ditado\audio\AAAAMMDD-HHMMSS.wav` — o WAV 16 kHz mono que foi enviado ao gate (mantém os 30 mais novos).

Desligado, nada disso é gravado; o `config.json` e a pasta `pending/` continuam funcionando.

## Build

    cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build'

## Status

| Fase | Entrega |
|---|---|
| P0–P4 ✅ | app completo: tray + opções + hotkey + WASAPI→gate→digitação — **E2E: o gate fala, o app digita sozinho** |
| P5 ✅ | `pending/*.wav` com reenvio na subida, `--version`, balões de erro |
| P6 ✅ | Inno 7 per-user (sem UAC) → `ditado-setup.exe` · repo público + release v0.1.0 |
| 0.2.2 ✅ | áudio gravado completo (o tamanho do pacote era tratado como bytes); nomes reais dos dispositivos; ícone próprio; log local com todas as opções; cliente (`User-Agent`) explícito no gate |

Ver `docs/PLANO.md`. Ferramenta de teste do contrato: `tools/gate-test` (binário autocontido).
