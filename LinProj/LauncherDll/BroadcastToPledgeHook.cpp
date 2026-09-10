// BroadcastToPledgeHook.cpp: see BroadcastToPledgeHook.h.
//
// 原廠 Action_BrodcastToPledge @ 0x62EC10 是真正的 toggle-type Action（對照
// NumberingMarkerHook.cpp 註解的反組譯）：settings+0x293 存開關，0x62B980
// SyncButtonVisual 同步按鈕圖示。這個原生功能（血盟成員登入通知開關）已經在
// 伺服器端 C_BroadcastToPledge.java 停用。
//
// 2026-09-06：原本借這顆按鈕當「自動喝藥」設定視窗的開啟入口，但實測發現這個
// Action 函式不是只有玩家手動點擊才會呼叫——遊戲登入連線後會自動呼叫一次
// （同步 UI 狀態），導致視窗一開遊戲就自動跳出來。入口改成 HOME 熱鍵（見
// LauncherDll.cpp 的 HookProc / AutoPotionOverlay_Show），這裡改回單純 no-op：
// 繼續攔截（避免原本已知會壞掉的原生行為執行到），但什麼都不做。
#include "stdafx.h"
#include "BroadcastToPledgeHook.h"
#include "LauncherDll.h"
#include "detours.h"
#include <cstring>

#pragma comment(lib, "detours.lib")

namespace {

constexpr uintptr_t kActionVa = 0x62EC10;

typedef void(__cdecl *ActionHandlerFn)(void *component);

ActionHandlerFn real_ActionBroadcastToPledge = (ActionHandlerFn)kActionVa;

void __cdecl Hook_ActionBroadcastToPledge(void *component) {
  (void)component;
  // 故意什麼都不做——原生行為已知壞掉（不送包），伺服器端功能也停用了，繼續
  // 攔截只是為了不要意外跑到原本可能有問題的原生程式碼。
}

} // namespace

void InstallBroadcastToPledgeHook() {
  BYTE *addr = reinterpret_cast<BYTE *>(kActionVa);
  // 鬆檢查：標準函數開頭 push ebp; mov ebp, esp
  if (addr[0] != 0x55 || addr[1] != 0x8B || addr[2] != 0xEC) {
    launcherdll_hook_log(
        "[BroadcastToPledge] 0x%08X prologue mismatch (%02X %02X %02X %02X %02X), skip",
        (unsigned)kActionVa, addr[0], addr[1], addr[2], addr[3], addr[4]);
    return;
  }

  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourAttach(&(PVOID &)real_ActionBroadcastToPledge,
               reinterpret_cast<PVOID>(Hook_ActionBroadcastToPledge));
  const LONG result = DetourTransactionCommit();
  launcherdll_hook_log("[BroadcastToPledge] Action_BrodcastToPledge hook result=%ld",
                       result);
}
