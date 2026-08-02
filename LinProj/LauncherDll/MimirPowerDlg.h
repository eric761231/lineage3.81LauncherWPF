// MimirPowerDlg.h: 祕米爾之泉能力選擇視窗（自訂 C/S 封包驅動，取代原生 S_Html 版本）。
#pragma once
#include <windows.h>

struct MimirOption {
  BYTE index;       // 0/1/2
  char name[128];   // 完整顯示文字（Java 端已組好，例如「短距離傷害+5,魔法命中+2」）
  DWORD value;      // 對應的 mimirwell id（不顯示，僅供除錯/未來擴充用）
};

// 顯示祕米爾之泉選擇視窗；options 必須剛好 3 筆（依 index 0/1/2 排序）。
void ShowMimirPowerDialog(DWORD objid, const MimirOption options[3]);

// 用最近一次從伺服器收到的資料顯示視窗（給行動視窗按鈕手動觸發用，不用重新跟
// 伺服器要）。如果從沒收過資料（objid 還是 0），就靜靜不做事，不彈空視窗。
void ShowMimirPowerDialogFromCache();
