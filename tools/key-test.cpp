#include <windows.h>
#include <cstdio>
// key-test down|up — envia a combinação do app como eventos injetados reais
int main(int argc, char** argv) {
  bool down = argc > 1 && lstrcmpA(argv[1], "down") == 0;
    WORD vks[3] = {VK_CONTROL, VK_SHIFT, VK_SPACE};
    INPUT e[3]{};
    for (int i = 0; i < 3; i++) {
        e[i].type = INPUT_KEYBOARD;
        e[i].ki.wVk = down ? vks[i] : (i == 0 ? VK_SPACE : vks[i]);  // solta na ordem inversa: space, shift, ctrl
    }
    if (down) { e[0].ki.wVk = VK_CONTROL; e[1].ki.wVk = VK_SHIFT; e[2].ki.wVk = VK_SPACE; }
    else { e[0].ki.wVk = VK_SPACE; e[1].ki.wVk = VK_SHIFT; e[2].ki.wVk = VK_CONTROL; }
    for (int i = 0; i < 3; i++) e[i].ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    for (int t = 0; t < 20; t++) {  // o hook do app pode estar ocupando a fila neste exato ms
        UINT n = SendInput(3, e, sizeof(INPUT));
        if (n == 3) { printf("%s=3 ok\n", down ? "down" : "up"); return 0; }
        Sleep(50);
    }
    printf("%s falhou err=%u\n", down ? "down" : "up", (unsigned)GetLastError());
    return 1;
}
