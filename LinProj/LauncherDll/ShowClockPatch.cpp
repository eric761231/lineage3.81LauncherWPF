// ShowClockPatch.cpp: see ShowClockPatch.h.
//
// A2（見 D:\天堂資料\RUST移植整理\計畫\A階段_吃肉修武_時鐘_範圍傷害.md）：
// 3.8 底部遊戲時間本來就有繪製邏輯，但進入繪製前會先檢查某個 UI 物件的
// hover/visibility byte，沒 hover 就跳過 `%02d:%02d` 格式化與文字繪製。把這個
// 條件跳轉 NOP 掉，時間就會每 frame 常態顯示。位址跟位元組對照 RUST 參考
// src/aux/show_clock_patch.rs（已在其他伺服器/版本上驗證過）：
//
//   0x0078AD47  mov eax, [ebp-0x68]
//   0x0078AD4A  movzx ecx, byte ptr [eax+0x48]
//   0x0078AD4E  test ecx, ecx
//   0x0078AD50  je 0x0078ADF7          <- 這 6 bytes 被 NOP 掉
//
// 跟 SmoothRunPatch.cpp 同一套風格：先驗證原始 bytes 再動手，不符合就跳過
// （fail-soft），不強行覆蓋。
#include "stdafx.h"
#include "ShowClockPatch.h"
#include "LauncherDll.h"
#include <cstring>

namespace {

// 遊戲時間繪製檢查判斷點之記憶體位址與比對用位元組
constexpr DWORD CLOCK_GATE_ADDR = 0x0078AD50;
constexpr int PATCH_LEN = 6;
const BYTE EXPECTED_BYTES[PATCH_LEN] = {0x0F, 0x84, 0xA1, 0x00, 0x00, 0x00};
const BYTE PATCHED_BYTES[PATCH_LEN] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};

/**
 * @brief 修改指定記憶體位址的 Code (修正記憶體保護屬性後寫入)。
 * @param addr 目標記憶體位址
 * @param code 欲寫入的指令資料
 * @param len 資料長度
 */
void PatchCode(void *addr, const void *code, int len) {
  DWORD dwOldProtect;
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, PAGE_READWRITE, &dwOldProtect);
  memcpy(addr, code, len);
  VirtualProtectEx(INVALID_HANDLE_VALUE, addr, len, dwOldProtect, &dwOldProtect);
}

} // namespace

/**
 * @brief 安裝時鐘常駐顯示修補（將 hover 判斷的條件跳轉指令 NOP 掉）。
 */
void InstallShowClockPatch() {
  BYTE *addr = (BYTE *)CLOCK_GATE_ADDR;
  if (memcmp(addr, PATCHED_BYTES, PATCH_LEN) == 0) {
    launcherdll_hook_log("[ShowClock] 已經是 patch 過的狀態，跳過");
    return;
  }
  if (memcmp(addr, EXPECTED_BYTES, PATCH_LEN) != 0) {
    launcherdll_hook_log(
        "[ShowClock][WARN] 0x%08X 位元組不符（%02X %02X %02X %02X %02X %02X，"
        "預期 0F 84 A1 00 00 00），跳過",
        (unsigned)CLOCK_GATE_ADDR, addr[0], addr[1], addr[2], addr[3], addr[4],
        addr[5]);
    return;
  }
  PatchCode(addr, PATCHED_BYTES, PATCH_LEN);
  launcherdll_hook_log("[ShowClock] hook 已安裝 @0x%08X（時鐘常駐顯示）",
                       (unsigned)CLOCK_GATE_ADDR);
}
