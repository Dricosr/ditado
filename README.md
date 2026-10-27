# ditado

Ditado por voz para qualquer campo de texto no Windows: **segure a combinação de teclas → fale → solte → o texto é digitado no campo ativo**.

- **Sem framework**: C++20/Win32 puro (hook de teclado, tray, WASAPI, WinInet) — um `.exe` de ~400 KB, sem DLLs de runtime.
- **Transcrição no `gate`** (nanosaurus `transcricao`): o app só grava, sobe o WAV e digita a resposta.

## Uso

1. Instale o `ditado-setup.exe` (instala por usuário, sem UAC) e rode.
2. O ícone aparece na bandeja (canto inferior direito). Clique nele para abrir as opções.
3. Segure `ctrl+shift+space` (configurável), fale, solte.

## Config (`%APPDATA%\ditado\config.json`)

Os campos do motor têm o mesmo nome da API do gate (`idioma`, `modelo`, `prompt`, `threshold`, `min_silence_ms`, `speech_pad_ms`, `max_speech_s`, `palavras`, `beam`, `label`, `timeout_ms`) — o app não inventa vocabulário. `url` é o endereço do gate.

## Build

    cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build'

## Status

| Fase | Entrega |
|---|---|
| P0–P4 ✅ | app completo: tray + opções + hotkey + WASAPI→gate→digitação — **E2E: o gate fala, o app digita sozinho** |
| P5–P6 ⏳ | `pending/*.wav`, instalador Inno Setup, release no GitHub |

Ver `docs/PLANO.md`. Ferramenta de teste do contrato: `tools/gate-test` (binário autocontido).
