// WebNavigateHook.cpp: see WebNavigateHook.h.
//
// CWebWindow::Navigate is the lowest-level funnel point every path that
// opens the in-game browser goes through (before the URL gets converted to a
// BSTR and handed to the embedded IE control) - hooking here catches all of
// them without needing to touch the higher-level wrapper or any XML.
#include "stdafx.h"
#include "WebNavigateHook.h"
#include "LauncherDll.h"
#include "detours.h"

#pragma comment(lib, "detours.lib")

namespace {

/**
 * @brief 寫入內建瀏覽器導向 Hook 專屬的日誌記錄。
 * @param fmt 格式化字串
 * @param ... 可變參數
 */
void WebLog(const char *fmt, ...) {
  (void)fmt;
}

typedef void(__thiscall *Navigate_t)(void *self, const char *url);

Navigate_t real_Navigate = (Navigate_t)0x610D70;
const char kRedirectUrl[] = "http://localhost:8082/index.html";

/**
 * @brief CWebWindow::Navigate 的 Hook 攔截函式，將導向目標重定向至本機網址。
 * @param self CWebWindow 物件指標 (this)
 * @param edx __fastcall 佔位 EDX
 * @param url 原始欲開啟的網址
 */
void __fastcall Hook_Navigate(void *self, void * /*edx*/, const char *url) {
  WebLog("intercepted url=[%s] -> redirecting to %s", url ? url : "(null)", kRedirectUrl);
  real_Navigate(self, kRedirectUrl);
}

} // namespace

/**
 * @brief 安裝遊戲內建 Web 瀏覽器導向 Hook。
 */
void InstallWebNavigateHook() {
  BYTE prologue[3];
  memcpy(prologue, (void *)0x610D70, 3);
  if (prologue[0] != 0x55 || prologue[1] != 0x8B || prologue[2] != 0xEC) {
    launcherdll_hook_log("[Install] WebNavigate skip");
    return;
  }

  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourAttach(&(PVOID &)real_Navigate, reinterpret_cast<PVOID>(Hook_Navigate));
  LONG result = DetourTransactionCommit();
  launcherdll_hook_log("[Install] WebNavigate %s", result == 0 ? "ok" : "fail");
}
