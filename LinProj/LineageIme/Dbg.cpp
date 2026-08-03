// Dbg.cpp: 對照 Rust 版 ime_overlay/src/dbg.rs — 寫到
// %LOCALAPPDATA%\Lineage38Launcher\dgvoodoo\ime_debug.log（沿用同一路徑慣例，方便比對）。
#include "Common.h"
#include "Dbg.h"
#include <cstdio>
#include <cstdarg>
#include <string>

static CRITICAL_SECTION g_logLock;
static bool g_logLockInit = false;

static void EnsureLockInit() {
  if (!g_logLockInit) {
    InitializeCriticalSection(&g_logLock);
    g_logLockInit = true;
  }
}

static bool BuildLogPath(char *out, size_t outSize) {
  char local[MAX_PATH] = {0};
  size_t len = 0;
  if (getenv_s(&len, local, MAX_PATH, "LOCALAPPDATA") != 0 || len == 0) return false;

  std::string dir = std::string(local) + "\\Lineage38Launcher\\dgvoodoo";
  CreateDirectoryA((std::string(local) + "\\Lineage38Launcher").c_str(), NULL);
  CreateDirectoryA(dir.c_str(), NULL);

  std::string full = dir + "\\ime_debug.log";
  strncpy_s(out, outSize, full.c_str(), _TRUNCATE);
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
