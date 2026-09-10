// InventoryDebugHook.h：背包指標鏈存取（原本是除錯用，2026-09-08 起兼作
// 「點格子選道具」流程的正式資料來源——見
// docs/AutoPotionOverlay_點選道具計畫.md）。
#pragma once
#include <windows.h>

// 剛被點擊那件道具的資訊（從 BAGITEM_INFO 複製出來，脫離原始指標的生命週期，
// 呼叫端可以安全跨函式/跨訊息使用）。
struct ClickedItemInfo {
  DWORD objId = 0;          // 背包裡這一件的實體 id（不是樣板 id，樣板 id 要送
                            // 給伺服器用 objId 查）
  char nameBig5[128] = {};  // 顯示名稱原始位元組（Big5），null-terminated
};

// 掃過目前背包全部道具，回傳「剛被點擊」那一件（用 BAGITEM_INFO+0x08 那個
// unknow2 旗標在點擊瞬間會變動這個現象當判斷依據，見計畫文件 2.2 節）。
// 要在 WM_LBUTTONDOWN 當下、盡量第一時間呼叫，這個旗標似乎只在按下瞬間短暫
// 非 0。找不到就回傳 false（沒點到任何道具，例如點到背包空格）。
bool InventoryDebug_FindJustClickedItem(ClickedItemInfo *out);
