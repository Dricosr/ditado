#pragma once
#include <string>

// Campos com o MESMO nome do contrato do gate (docs/transcricao §1) — o resto é cliente.
struct Config {
  std::wstring url = L"http://192.168.18.150:3070";
  std::string idioma = "pt";
  std::string modelo = "small";  // um modelo por app (trocar o residente custa 5-10 s)
  std::string prompt;            // glossário/hotwords, vazio = não enviado
  float threshold = 0.5f;
  int min_silence_ms = 300;
  int speech_pad_ms = 100;
  float max_speech_s = 30.f;
  bool palavras = false;
  int beam = 1;
  std::string label = "ditado";
  int timeout_ms = 120000;

  // cliente
  std::string app = "ditado";  // X-Gate-App
  std::string tecla = "ctrl+shift+space";
  std::string dispositivo;     // vazio = padrão do sistema
  std::string metodo_insercao = "unicode";  // unicode | area
  bool mostrar_sem_fala = false;
  bool autostart = false;

  static Config Load();
  void Save() const;
  std::string ToQuery() const;  // query string exata da rota
  static std::wstring Dir();
};
