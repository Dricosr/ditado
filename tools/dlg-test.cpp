#include "settings.h"
#include "log.h"
#include <windows.h>
#include <cstdio>
int main() {
  lg::Init(Config::Dir());
  SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e) -> LONG {
    lg::Info("dlg-test AV " + std::to_string((unsigned long long)e->ExceptionRecord->ExceptionAddress));
    return EXCEPTION_EXECUTE_HANDLER;
  });
  Config c = Config::Load();
  settings::Show(0, c);
  printf("voltou url=%ls tecla=%s\n", c.url.c_str(), c.tecla.c_str());
  return 0;
}
