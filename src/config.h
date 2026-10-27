#pragma once
#include <string>

struct Config {
  std::wstring url = L"http://192.168.18.150:3070";
  std::string idioma = "pt", modelo = "small", prompt, app = "ditado", label = "ditado";
  float threshold = 0.5f;
  int min_silence_ms = 300, speech_pad_ms = 100, beam = 1, timeout_ms = 120000;
  float max_speech_s = 30.f;
  bool palavras = false, mostrar_sem_fala = false, autostart = false;
  std::string tecla = "ctrl+shift+space", dispositivo, metodo_insercao = "unicode";
  static Config Load();
  void Save() const;
  std::string ToQuery() const;
  static std::wstring Dir();
};
