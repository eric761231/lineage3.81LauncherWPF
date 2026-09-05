#include "stdafx.h"
#include "TradeStatusHook.h"
#include "LauncherDll.h"
#include <string.h>

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
