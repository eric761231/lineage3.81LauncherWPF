// InventoryDebugHook.cpp: see InventoryDebugHook.h.
//
// 位址/結構沿用舊 HelperDlg.cpp（已 #if 0 停用）裡查過的背包指標鏈，這次已經
// 用 InvDbg log 實測驗證過（見 docs/AutoPotionOverlay_點選道具計畫.md 第 0
// 節）：unknow1 是全部道具共用的 vtable 指標、id(+0x04) 是實體 id（不是樣板
// id）、name(+0x0C) 可靠。原本每次點擊印全部 49 筆的除錯 log 已經拿掉（造成
// 明顯卡頓），改成只找「剛被點的那一件」。
#include "stdafx.h"
#include "InventoryDebugHook.h"
#include "LauncherDll.h"
#include <cstring>

namespace {

#pragma pack(push, 1)
struct BAGITEM_INFO {
  int unknow1;  // +0x00：所有道具共用同一個值，vtable/類別指標，非道具資料
  DWORD id;     // +0x04：實體 id（instance objId）
  int unknow2;  // +0x08：目前只觀察到在「剛被點擊」的那一件短暫非 0
  char *name;   // +0x0C：顯示名稱字串指標（Big5）
};
#pragma pack(pop)

int GetItemCount() {
  int count = 0;
  __try {
    int a = *(int *)0x009A9250;
    count = *(int *)(a + 0x2C);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    count = 0;
  }
  return count;
}

BAGITEM_INFO *GetItem(int index) {
  BAGITEM_INFO *pBagItem = NULL;
  __try {
    DWORD a = *(DWORD *)0x009A9250;
    a = *(DWORD *)(a + 0x58);
    a = *(DWORD *)(a + index * 4);
    pBagItem = (BAGITEM_INFO *)a;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    pBagItem = NULL;
  }
  return pBagItem;
}

} // namespace

bool InventoryDebug_FindJustClickedItem(ClickedItemInfo *out) {
  if (!out)
    return false;
  __try {
    int count = GetItemCount();
    if (count <= 0 || count > 200) {
      // count 異常（背包指標還沒建立好，或算出來的值明顯不合理）就不繼續往
      // 下讀，避免用一個垃圾數字去跑迴圈存取無效記憶體。
      return false;
    }
    for (int i = 0; i < count; i++) {
      BAGITEM_INFO *item = GetItem(i);
      if (item == NULL)
        continue;
      __try {
        if (item->unknow2 == 0)
          continue;
        out->objId = item->id;
        out->nameBig5[0] = 0;
        if (item->name) {
          strncpy_s(out->nameBig5, sizeof(out->nameBig5), item->name,
                    sizeof(out->nameBig5) - 1);
        }
        launcherdll_hook_log(
            "[AutoPotionPick] found clicked item index=%d objId=%u name=%s",
            i, (unsigned)out->objId, out->nameBig5);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        continue;
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return false;
}
