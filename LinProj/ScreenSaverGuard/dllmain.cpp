// dllmain.cpp: DLL_PROCESS_ATTACH spawns a worker thread (avoids doing real
// work under the loader lock) that runs RunScreenSaverGuard().
#include <windows.h>
#include "ScreenSaverGuard.h"

namespace {

DWORD WINAPI Worker(LPVOID) {
  RunScreenSaverGuard();
  return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(hModule);
    HANDLE h = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
    if (h) CloseHandle(h);
  }
  return TRUE;
}
