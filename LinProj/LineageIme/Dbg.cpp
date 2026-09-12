// Dbg.cpp: 寫到 <遊戲根目錄>\Core\ime_debug.log（與 launcher.log 同目錄）。
#include "Common.h"
#include "Dbg.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>

static CRITICAL_SECTION g_logLock;
static bool g_logLockInit = false;

static void EnsureLockInit() {
  if (!g_logLockInit) {
    InitializeCriticalSection(&g_logLock);
    g_logLockInit = true;
  }
}

// 注入後 GetModuleFileNameA(NULL) = 遊戲 exe 目錄；log 放同層 Core\。
static bool BuildLogPath(char *out, size_t outSize) {
  char exePath[MAX_PATH] = {0};
  if (GetModuleFileNameA(NULL, exePath, MAX_PATH) == 0)
    return false;

  for (int i = (int)strlen(exePath) - 1; i >= 0; i--) {
    if (exePath[i] == '\\' || exePath[i] == '/') {
      exePath[i] = '\0';
      break;
    }
  }

  char coreDir[MAX_PATH] = {0};
  sprintf_s(coreDir, "%s\\Core", exePath);
  CreateDirectoryA(coreDir, NULL);

  sprintf_s(out, outSize, "%s\\ime_debug.log", coreDir);
  return true;
}

void ImeDbgLog(const char *fmt, ...) {
  EnsureLockInit();
  EnterCriticalSection(&g_logLock);

  char path[MAX_PATH];
  if (BuildLogPath(path, sizeof(path))) {
    FILE *f = nullptr;
    if (fopen_s(&f, path, "a") == 0 && f) {
      SYSTEMTIME st;
      GetLocalTime(&st);
      fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d.%03d][PID=%lu][TID=%lu] ",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
              (unsigned long)GetCurrentProcessId(), (unsigned long)GetCurrentThreadId());

      va_list args;
      va_start(args, fmt);
      vfprintf(f, fmt, args);
      va_end(args);

      fprintf(f, "\n");
      fclose(f);
    }
  }

  LeaveCriticalSection(&g_logLock);
}
