#include "stdafx.h"
#include "TradeStatusHook.h"
#include "LauncherDll.h"
#include <string.h>

// =============================================================================
// 交易狀態 Hook（TrStatus）
//
// 目的：讓交易視窗也走「道具格式說明切割」流程。遊戲原生交易視窗 ctor 在
//「道具結構 +0x10 欄位為 0」時會用 je 跳過對 0x4AEC90（SplitFmt——把道具
// 格式字串依 0x17 分隔符切成多行的函式，定義在 PrivateShopStatus.cpp）的呼叫，
// 導致交易視窗少了這段狀態文字。這裡把該 je 改成兩個 NOP，強制不論 +0x10
// 是否為 0 都執行 SplitFmt，跟倉庫（WhStatus）那套顯示邏輯對齊。
//
// 範圍刻意收窄：只動交易視窗這一條路徑，不碰個人商店（ShStatus）、也不碰
// 倉庫用的 0x4AF070 附加入口。安裝時機在 DelayedDetour（等遊戲程式碼解密
// 完成）之後，由 DelayedDetourThread 依序呼叫，見 LauncherDll.cpp。
// =============================================================================

/**
 * @brief 安裝交易狀態修補：把交易視窗 ctor 裡「item+0x10==0 就跳過 SplitFmt」
 * 的 je 指令（0x45A9E0）改成 0x90 0x90 NOP。
 *
 * 0x45A9DC 起的 6 bytes 原生指令為：
 *   83 7A 10 00    cmp dword ptr [edx+0x10], 0
 *   74 2C          je  +0x2C        ; 成立就跳過 0x4AEC90 呼叫
 *
 * 流程：先 memcmp 驗證這 6 bytes 跟預期逐 byte 一致（版本不符就記 log 直接
 * 放棄，避免在錯位址上亂補），通過後只改 je 那 2 bytes（位於 pJe+4，也就是
 * log 裡寫的 0x45A9E0），其餘指令原樣保留，最後 FlushInstructionCache 讓
 * CPU 指令快取看到新內容。
 */
void InstallTradeStatusHook() {
  BYTE *pJe = reinterpret_cast<BYTE *>(0x45A9DC);
  static const BYTE kTradeCmpJe[6] = {0x83, 0x7A, 0x10, 0x00, 0x74, 0x2C};
  if (memcmp(pJe, kTradeCmpJe, sizeof(kTradeCmpJe)) != 0) {
    launcherdll_hook_log("[TrStatus] 45A9E0 nop-je mismatch, skip");
    return;
  }
  DWORD old = 0;
  VirtualProtect(pJe + 4, 2, PAGE_EXECUTE_READWRITE, &old);
  pJe[4] = 0x90;
  pJe[5] = 0x90;
  VirtualProtect(pJe + 4, 2, old, &old);
  FlushInstructionCache(GetCurrentProcess(), pJe + 4, 2);
  launcherdll_hook_log("[TrStatus] 45A9E0 nop-je ok=1");
}
