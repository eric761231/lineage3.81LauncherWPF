// PssConfig.h: 遊玩輔助本機設定 + 送伺服器封包。
// 權威在 Core\auto_potion.cfg（檔名維持舊的）；進世界／開面板／儲存時灌進 L1PlaySupportState，不落地 DB。
//
// C 包分工（數值對齊 OpcodesClient，不可改）：
//   opcode 75  C_PlaySupport     — 治療／補魔 62-byte、點背包解析 8-byte、面板 4-byte(0x56)
//   opcode 128 C_SecurityStatus  — 原生城堡剛好 5 bytes；其餘看 magic：
//                                  0x58 名單覆寫、0x59 請回推、0x5A 其他頁 flags、
//                                  0x5B 名單續段（追加）、0x5C 提煉黑魔石四勾
// 75 與 128 的 magic 互不相通。SendPacketData 必須在遊戲主執行緒（0x580E50）。
#pragma once

#include <windows.h>

constexpr int kPssSlotsPerSection = 5;
constexpr int kItemFilterMax = 200;
constexpr int kItemFilterMinCells = 40;
constexpr int kItemFilterListDelete = 0;
constexpr int kItemFilterListDissolve = 1;

enum PssSlotKind {
  PssSlot_None = 0,
  PssSlot_Item = 1,
  PssSlot_Skill = 2,
};

struct PssSlot {
  int kind = PssSlot_None; // PssSlotKind
  int id = 0;              // itemId 或 skillId
  // 畫格子圖示用（L1ItemInstance.get_gfxid()）。只存本機 cfg＋畫面，不進封包。
  int gfxid = 0;
  int thresholdPercent = 0; // 0 = 此槽不觸發
  wchar_t name[64] = L"";
  int count = 0;
};

struct PssSection {
  PssSlot slots[5];
};

struct ItemFilterEntry {
  int itemId = 0;
  int gfxid = 0;
  wchar_t name[64] = L"";
};

struct ItemFilterList {
  int count = 0;
  ItemFilterEntry items[kItemFilterMax];
};

struct PssConfig {
  bool enabled = false;
  PssSection heal;
  PssSection mana;

  // 其他頁打勾。送 opcode 128 / magic 0x5A / flags（bit0 吃肉、bit1 修武）。
  // eatMeatItemId／whetstoneItemId 只留 cfg 相容舊檔，不再上傳（後端固定白名單自選）。
  bool eatMeat = false;
  bool whetstone = false;
  int eatMeatItemId = 0;
  int whetstoneItemId = 0;

  // 提煉黑魔石勾選：0=一級…3=四級。送 opcode 128 / magic 0x5C（與 0x5A 分開）。
  bool darkStone[4] = {false, false, false, false};

  bool showDamage = false; // 僅本機 AttackDamageHook，不進任何 C 包

  ItemFilterList autoDelete;
  ItemFilterList autoDissolve;
};

// 從 <遊戲根目錄>\Core\auto_potion.cfg 讀取（檔名維持舊的，避免既有設定失效）。
PssConfig PssConfig_Load();

bool PssConfig_Save(const PssConfig &cfg);

// 送喝水設定（opcode 75、剛好 62 bytes）。必須在遊戲主執行緒。
void PssConfig_SendToServer(const PssConfig &cfg);

// 送其他頁打勾。opcode 128、magic 0x5A、剛好 4 bytes（避開原生城堡 5）。
void PssConfig_SendStatusToServer(const PssConfig &cfg);

// 送提煉黑魔石四勾。opcode 128、magic 0x5C、剛好 4 bytes。
void PssConfig_SendCraftToServer(const PssConfig &cfg);

bool PssConfig_HasAnySlot(const PssConfig &cfg);

void PssConfig_SendResolveItemRequest(int section, int slotIndex, DWORD objId);

void PssConfig_SendUiVisible(bool visible);

void PssConfig_SendItemFilterList(int listType, const ItemFilterList &list);

void PssConfig_RequestItemFilterList(int listType);
