// dllmain.cpp: DLL 的進入點 (DllMain) 以及 Detours 鉤子的初始化與移除。
// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include <windows.h>

extern void init();
extern HINSTANCE hins;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call,
                      LPVOID lpReserved) {
  switch (ul_reason_for_call) {
  case DLL_PROCESS_ATTACH: {
    hins = (HINSTANCE)hModule;
    DisableThreadLibraryCalls(hModule);
    // 恢復在背景執行緒中做初始化與掛鉤，以避免 InjectDll 死鎖
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)init, NULL, 0, NULL);
    break;
  }
  case DLL_THREAD_ATTACH:
  case DLL_THREAD_DETACH:
  case DLL_PROCESS_DETACH:
    break;
  }
  return TRUE;
}
