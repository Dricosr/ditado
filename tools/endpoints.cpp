#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys.h>
#include <propsys.h>
#include <stdio.h>
#include <cmath>
#include <vector>
#include <string>
#include "audio.h"
#pragma comment(lib, "ole32.lib")
#include <functiondiscoverykeys.h>
int main() {
  CoInitializeEx(0, COINIT_APARTMENTTHREADED);
  IMMDeviceEnumerator* en = nullptr;
  CoCreateInstance(__uuidof(MMDeviceEnumerator), 0, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en);
  IMMDeviceCollection* coll = nullptr;
  en->EnumAudioEndpoints(eCapture, 1 | 2 | 4 | 8, &coll);
  UINT n = 0; coll->GetCount(&n);
  printf("%u endpoints de captura:\n", n);
  for (UINT i = 0; i < n; i++) {
    IMMDevice* d = nullptr; coll->Item(i, &d);
    wchar_t* id = nullptr; d->GetId(&id);
    IPropertyStore* ps = nullptr;
    wchar_t name[128] = L"(sem nome)";
    if (SUCCEEDED(d->Activate(__uuidof(IPropertyStore), CLSCTX_ALL, nullptr, (void**)&ps))) {
      PROPVARIANT pv; PropVariantInit(&pv);
      if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR) wcscpy_s(name, pv.pwszVal);
      PropVariantClear(&pv); ps->Release();
    }
    printf("  [%u] nome=%ls id=%ls\n", i, name, id);
    CoTaskMemFree(id); d->Release();
  }
  IMMDevice* dft = nullptr;
  if (SUCCEEDED(en->GetDefaultAudioEndpoint(eCapture, eConsole, &dft)) && dft) {
    wchar_t* id = nullptr; dft->GetId(&id);
    IPropertyStore* ps = nullptr; wchar_t name[128] = L"?";
    if (SUCCEEDED(dft->Activate(__uuidof(IPropertyStore), CLSCTX_ALL, nullptr, (void**)&ps))) {
      PROPVARIANT pv; PropVariantInit(&pv);
      if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv))) wcscpy_s(name, pv.pwszVal);
      PropVariantClear(&pv); ps->Release();
    }
    printf("DEFAULT captura = [%ls] %ls\n", name, id);
    CoTaskMemFree(id); dft->Release();
  } else printf("SEM endpoint de captura default!\n");
}
