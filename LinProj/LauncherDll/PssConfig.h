// PssConfig.h: 遊玩輔助本機設定 + 送伺服器封包。
// 權威在 Core\auto_potion.cfg（檔名維持舊的）；進世界／開面板／儲存時灌進 L1PlaySupportState，不落地 DB。
//
// C 包分工（數值對齊 OpcodesClient，不可改）：
//   opcode 75  C_PlaySupport     — 點背包解析 8-byte、面板 4-byte(0x56)。
//                                  2026-09-16：原本挤在同一包的治療／補魔 62-byte
//                                  設定包（含 enabled 旗標）實測會在送出後被吃掉
//                                  2 bytes、伺服器端一律收成 64 bytes，導致啟動
//                                  鈕／恢復槽設定完全失效——原因不明（懷疑跟這個
//                                  33 個參數的單一 SendPacketData 呼叫或原生封包
//                                  長度表有關，未能反解出真正原因）。已改拆成三個
//                                  opcode 128 小封包（見下方 0x5F/0x60/0x61），跟
//                                  BUFF-固定／自訂九格同一套已驗證可靠的作法一致；
//                                  opcode 75 的舊 62-byte 分支保留在伺服器端純粹
//                                  相容舊 DLL，client 端不再送。
//   opcode 128 C_SecurityStatus  — 原生城堡剛好 5 bytes；其餘看 magic：
//                                  0x58 名單覆寫、0x59 請回推、0x5A 其他頁 flags、
//                                  0x5B 名單續段（追加）、0x5C 提煉黑魔石四勾、
//                                  0x5D BUFF-固定九格 itemId、0x5E BUFF-自訂九格、
//                                  0x5F 啟動／停止旗標、0x60 治療六槽、0x61 補魔六槽
// 75 與 128 的 magic 互不相通。SendPacketData 必須在遊戲主執行緒（0x580E50）。
#pragma once

#include <windows.h>

constexpr int kPssSlotsPerSection = 6;
// 2026-09-17：9→10——新增 index 9＝慎重藥水，畫面位置在「設定變身」面板
// 右下角小格（不在 3x3 網格內，見 PssOverlay.cpp 的 ComputeFixedBuffSlots／
// kFixedBuffWisdomSlotIndex）。對齊伺服器 C_PlaySupport.FIXED_BUFF_SLOTS。
constexpr int kPssFixedBuffSlots = 10;
constexpr int kPssCustomBuffSlots = 9;
// BUFF-固定第8格（解毒）在陣列裡的索引，唯一允許「技能」的格子
// （解毒術/聖潔之光，見 C_PlaySupport.FIXED_BUFF_SLOT_DETOX）；其餘格永遠是道具。
constexpr int kFixedBuffDetoxSlotIndex = 7;
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
  // 2026-09-17：第二個門檻，目前只有補魔第 6 格（MP恢復技能）在用，當「HP%
  // 安全下限」——0 = 不限制，其餘 slot 一律是 0、沒有意義。跟 thresholdPercent
  // 一樣會進封包（見 PssConfig_SendToServer 內的 SendSectionSlots）。
  int thresholdPercent2 = 0;
  wchar_t name[64] = L"";
  int count = 0;
};

struct PssSection {
  PssSlot slots[kPssSlotsPerSection];
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

struct PssFixedBuff {
  PssSlot slots[kPssFixedBuffSlots];
};

// BUFF-自訂：每格可為道具或技能（PssSlot.kind 決定），不像固定格鎖死每格的道具種類。
struct PssCustomBuff {
  PssSlot slots[kPssCustomBuffSlots];
};

struct PssConfig {
  bool enabled = false;
  PssSection heal;
  PssSection mana;
  PssFixedBuff fixedBuff;
  PssCustomBuff customBuff;

  // 其他頁打勾。送 opcode 128 / magic 0x5A / flags（bit0 吃肉、bit1 修武）。
  // eatMeatItemId／whetstoneItemId 只留 cfg 相容舊檔，不再上傳（後端固定白名單自選）。
  bool eatMeat = false;
  bool whetstone = false;
  int eatMeatItemId = 0;
  int whetstoneItemId = 0;

  // 提煉黑魔石勾選：0=一級…3=四級。送 opcode 128 / magic 0x5C（與 0x5A 分開）。
  bool darkStone[4] = {false, false, false, false};

  bool showDamage = false; // 僅本機 AttackDamageHook，不進任何 C 包
  bool underwaterPump = false; // 僅本機 UnderwaterPumpHook，不進任何 C 包
  bool allDay = false; // 僅本機 AllDayPatch，不進任何 C 包

  ItemFilterList autoDelete;
  ItemFilterList autoDissolve;
};

// 從 <遊戲根目錄>\Core\auto_potion.cfg 讀取（檔名維持舊的，避免既有設定失效）。
PssConfig PssConfig_Load();

bool PssConfig_Save(const PssConfig &cfg);

// 送啟動／停止＋治療／補魔槽設定。內部已拆成三個 opcode 128 小封包（0x5F 啟動旗標、
// 0x60 治療六槽、0x61 補魔六槽），取代原本會被吃掉 2 bytes 的單一 62-byte opcode 75
// 封包（見上方檔頭說明）。必須在遊戲主執行緒。
void PssConfig_SendToServer(const PssConfig &cfg);

// 送其他頁打勾。opcode 128、magic 0x5A、剛好 4 bytes（避開原生城堡 5）。
void PssConfig_SendStatusToServer(const PssConfig &cfg);

// 送提煉黑魔石四勾。opcode 128、magic 0x5C、剛好 4 bytes。
void PssConfig_SendCraftToServer(const PssConfig &cfg);

// 送 BUFF-固定九格 itemId。opcode 128、magic 0x5D（cccc + 9×d，空槽 0）。
void PssConfig_SendFixedBuffToServer(const PssConfig &cfg);

// 送 BUFF-自訂九格 kind+id。opcode 128、magic 0x5E（ccc + n=9 + 9×kind(c) + 9×id(d)，空槽 0）。
void PssConfig_SendCustomBuffToServer(const PssConfig &cfg);

bool PssConfig_HasAnySlot(const PssConfig &cfg);

// section：0=治療 1=補魔 2=BUFF-固定(道具) 3=BUFF-自訂(道具) 4=BUFF-自訂(技能)
// 5=補魔第6格(MP恢復技能，只收技能，見 SECTION_MANA_SKILL)。
// 6=BUFF-固定第8格解毒技能。7=治療第5格傳送技能。
// section=4/5/6/7 時 objId 直接當 skillId 用（技能沒有背包實例）。
void PssConfig_SendResolveItemRequest(int section, int slotIndex, DWORD objId);

void PssConfig_SendUiVisible(bool visible);

void PssConfig_SendItemFilterList(int listType, const ItemFilterList &list);

void PssConfig_RequestItemFilterList(int listType);
