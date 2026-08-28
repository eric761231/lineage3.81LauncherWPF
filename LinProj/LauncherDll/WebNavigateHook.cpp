// WebNavigateHook.cpp: see WebNavigateHook.h.
//
// CWebWindow::Navigate is the lowest-level funnel point every path that
// opens the in-game browser goes through (before the URL gets converted to a
// BSTR and handed to the embedded IE control) - hooking here catches all of
// them without needing to touch the higher-level wrapper or any XML.
#include "stdafx.h"
#include "WebNavigateHook.h"
#include "detours.h"

#pragma comment(lib, "detours.lib")

namespace {

// Each translation unit in this project owns its own tiny logger (see
// NetLog in DisconnectHook.cpp, ImGuiLog in ImGuiHook.cpp) - the
// launcherdll_net_log in LauncherDll.cpp is `static` (internal linkage) and
// isn't linkable from here.
void WebLog(const char *fmt, ...) {
  char exePath[MAX_PATH] = {0};
  char logPath[MAX_PATH] = "./Core/launcher.log";
  if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0) {
    for (int i = (int)strlen(exePath) - 1; i >= 0; i--) {
      if (exePath[i] == '\\' || exePath[i] == '/') {
        exePath[i] = '\0';
        break;
      }
    }
    sprintf_s(logPath, "%s\\Core\\launcher.log", exePath);
  }
  FILE *fp = NULL;
  if (fopen_s(&fp, logPath, "a+") != 0 || fp == NULL) return;
  SYSTEMTIME st;
  GetLocalTime(&st);
  char msg[512] = {0};
  va_list args;
  va_start(args, fmt);
  vsprintf_s(msg, fmt, args);
  va_end(args);
  fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d.%03d][PID=%u][TID=%u] [WebNavigate] %s\n",
          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
          st.wMilliseconds, (unsigned)GetCurrentProcessId(), (unsigned)GetCurrentThreadId(), msg);
  fflush(fp);
  fclose(fp);
}

typedef void(__thiscall *Navigate_t)(void *self, const char *url);

Navigate_t real_Navigate = (Navigate_t)0x610D70;
const char kRedirectUrl[] = "http://localhost:8082/index.html";

// MSVC won't let a free function be defined with __thiscall directly (only
// member functions and function-pointer types may use it - real_Navigate
// above is fine as a typedef'd pointer, but this definition needs the
// standard __fastcall-with-a-dummy-EDX-slot trick: __fastcall passes its
// first two params in ECX/EDX, which is binary-compatible with how a
// thiscall caller already puts `this` in ECX; the unused edx parameter just
// absorbs whatever thiscall didn't put there, and `url` still arrives via
// the stack exactly like a thiscall stack argument would.
void __fastcall Hook_Navigate(void *self, void * /*edx*/, const char *url) {
  WebLog("intercepted url=[%s] -> redirecting to %s", url ? url : "(null)", kRedirectUrl);
  real_Navigate(self, kRedirectUrl);
}

} // namespace

void InstallWebNavigateHook() {
  BYTE prologue[3];
  memcpy(prologue, (void *)0x610D70, 3);
  if (prologue[0] != 0x55 || prologue[1] != 0x8B || prologue[2] != 0xEC) {
    WebLog("0x610D70 prologue mismatch (expected push ebp; mov ebp,esp), skipping install");
    return;
  }

  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourAttach(&(PVOID &)real_Navigate, reinterpret_cast<PVOID>(Hook_Navigate));
  LONG result = DetourTransactionCommit();
  WebLog("Navigate hook install result=%ld", result);
}
