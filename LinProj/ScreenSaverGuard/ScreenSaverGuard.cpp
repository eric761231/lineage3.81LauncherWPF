// ScreenSaverGuard.cpp: suppresses the OS screen saver while the game window
// is open and not minimized, restoring the user's original setting once the
// window is minimized or closed. Split out into its own DLL (separate from
// LauncherDll/ddraw.dll) so this concern's failure modes stay isolated.
//
// Earlier approach (kept only in history/commit log, not here): let the
// screen saver actually run and minimize/restore the game window ourselves
// around it (via SC_SCREENSAVE / a poll thread posting SC_MINIMIZE|SC_RESTORE).
// That worked mechanically, but minimizing this DirectDraw-hooked window
// still forces a primary-surface Restore() when un-minimizing, and repeated
// Restore() calls (one per screen saver cycle) reliably reproduced a
// pre-existing deadlock inside DDrawCompat between its own
// D3dDdi::ScopedCriticalSection::s_cs and the system ddraw.dll's internal
// first-time-init lock (confirmed live via cdb across multiple hangs: both
// locks held, contention count climbing, process reported Not Responding).
// That deadlock predates this feature and isn't something safe to patch
// around here.
//
// This version never calls ShowWindow/minimize/restore itself at all -- it
// only *observes* IsIconic() (a cheap, thread-safe, non-blocking read of the
// window's current state) and toggles the screen saver setting accordingly:
// suppressed while the window is visible/in use, allowed once the user has
// minimized it themselves. A single user-initiated restore whenever they
// come back is the same pattern already confirmed safe by hand; it's the
// *automated, repeated* minimize/restore cycling that reproduced the
// deadlock, not an occasional manual one.
// Only the screen saver itself is suppressed (SPI_SETSCREENSAVEACTIVE);
// display/monitor power-saving is a separate OS setting and is untouched,
// so the monitor can still sleep normally while the game is running.
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include "ScreenSaverGuard.h"

namespace {

const wchar_t *kGameClass = L"Lineage";

// Same log file the rest of the injected DLLs write to (Core\launcher.log),
// just with our own [ScreenSaverGuard] prefix so entries are easy to tell
// apart while still landing in the one file the user already knows to send.
void Log(const char *fmt, ...) {
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
  fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d.%03d][PID=%u][TID=%u] [ScreenSaverGuard] %s\n",
          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
          st.wMilliseconds, (unsigned)GetCurrentProcessId(), (unsigned)GetCurrentThreadId(), msg);
  fflush(fp);
  fclose(fp);
}

HWND WaitForGameWindow(DWORD timeoutMs) {
  DWORD start = GetTickCount();
  for (;;) {
    HWND h = FindWindowW(kGameClass, NULL);
    if (h) return h;
    if (GetTickCount() - start >= timeoutMs) return NULL;
    Sleep(200);
  }
}

}  // namespace

void RunScreenSaverGuard() {
  Log("RunScreenSaverGuard entered, PID=%u", (unsigned)GetCurrentProcessId());
  BOOL originalScreenSaverActive = FALSE;
  SystemParametersInfoW(SPI_GETSCREENSAVEACTIVE, 0, &originalScreenSaverActive, 0);
  if (!originalScreenSaverActive) {
    Log("no screen saver configured on this machine (SPI_GETSCREENSAVEACTIVE=FALSE), nothing to suppress");
    return;
  }

  HWND hwnd = WaitForGameWindow(180000);
  if (!hwnd) {
    Log("game window not found within 180s, giving up");
    return;
  }
  Log("game hwnd = 0x%p, watching for minimize state to toggle screen saver", hwnd);

  // Track our own idea of the current suppression state so we only call
  // SystemParametersInfoW when it actually needs to change, not every tick.
  bool suppressed = false;
  while (IsWindow(hwnd)) {
    bool shouldSuppress = !IsIconic(hwnd);
    if (shouldSuppress != suppressed) {
      SystemParametersInfoW(SPI_SETSCREENSAVEACTIVE, shouldSuppress ? FALSE : TRUE, NULL, SPIF_SENDCHANGE);
      Log(shouldSuppress ? "window visible -> screen saver suppressed"
                          : "window minimized -> screen saver allowed");
      suppressed = shouldSuppress;
    }
    Sleep(1000);
  }

  Log("game window closed, restoring screen saver setting");
  SystemParametersInfoW(SPI_SETSCREENSAVEACTIVE, TRUE, NULL, SPIF_SENDCHANGE);
}
