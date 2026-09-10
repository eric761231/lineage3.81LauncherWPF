// AutoPotionConfig.h: 自動喝水 + A1 吃肉／修武——本機設定檔 + 借位 opcode 75 送伺服器。
// 2026-09-10：喝水改成每一槽各自獨立的百分比門檻。伺服器多槽同時達標時，
// PotionTimer 會挑門檻數值最低（最危急）那槽。
// 權威在本機 cfg；開面板／儲存時灌進伺服器 L1PlaySupportState（不落地 DB）。
#pragma once

enum AutoPotionSlotKind {
  AutoPotionSlot_None = 0,
  AutoPotionSlot_Item = 1,
  AutoPotionSlot_Skill = 2,
};

struct AutoPotionSlot {
  int kind = AutoPotionSlot_None; // AutoPotionSlotKind
  int id = 0;                     // itemId 或 skillId
  // 畫格子圖示用（L1ItemInstance.get_gfxid()）。只存本機 cfg＋畫面，不進封包。
  // 圖示檔名：item_<gfxid>.png
  int gfxid = 0;
  // 這一槽自己的觸發百分比，0 = 此槽不觸發。
  int thresholdPercent = 0;
  // 顯示用名稱／數量快照，跟著 cfg 存讀；不進網路封包。
  wchar_t name[64] = L"";
  int count = 0;
};

struct AutoPotionSection {
  AutoPotionSlot slots[5];
};

struct AutoPotionConfig {
  bool enabled = false;
  AutoPotionSection heal;
  AutoPotionSection mana;

  // ----- A1：自動吃肉／修武（對應 C_PlaySupport length==12、magic 0x57）-----
  // flags bit0 = eatMeat，bit1 = whetstone。
  // 飽食觸發由後端 pc.get_food()<225 判斷，不需要前端再傳 threshold。
  // itemId：0 = 後端白名單自動選；非 0 = 只准該樣板 id（不是背包 objId）。
  bool eatMeat = false;
  bool whetstone = false;
  int eatMeatItemId = 0;
  int whetstoneItemId = 0;

  // 僅本機：顯示傷害（不進伺服器封包）
  bool showDamage = false;
};

constexpr int kAutoPotionSlotsPerSection = 5;

// 從 <遊戲根目錄>\Core\auto_potion.cfg 讀取（key=value）。
AutoPotionConfig AutoPotionConfig_Load();

bool AutoPotionConfig_Save(const AutoPotionConfig &cfg);

// 送喝水設定。必須在遊戲主執行緒呼叫（SendPacketData @ 0x580E50）。
// 格式（含 opcode）62 bytes：enabled(c) + 5×(thr%+kind+id) + 5×(thr%+kind+id)
void AutoPotionConfig_SendToServer(const AutoPotionConfig &cfg);

// 送吃肉／修武設定。同樣必須在遊戲主執行緒。
// 固定 12 bytes（含 opcode），對齊伺服器 C_PlaySupport length==12：
//   c opcode=75
//   c magic=0x57
//   c flags   (bit0=eatMeat, bit1=whetstone)
//   c pad=0   （保留；不再傳 foodThreshold）
//   d meatItemId
//   d whetstoneItemId
void AutoPotionConfig_SendStatusToServer(const AutoPotionConfig &cfg);

// 任一槽有內容則視為應啟用（儲存時可呼叫）。
bool AutoPotionConfig_HasAnySlot(const AutoPotionConfig &cfg);

// 點格子→點背包：解析樣板 id。固定長度（含 opcode）：
// opcode(c)+section(c)+slotIndex(c)+objId(d)+pad(c) —— 以實作 SendPacketData 為準。
void AutoPotionConfig_SendResolveItemRequest(int section, int slotIndex, DWORD objId);

// 通知伺服器 Overlay 開／關。固定 4 bytes：
// opcode(c) + magic(c=0x56) + visible(c:0/1) + pad(c=0)。
void AutoPotionConfig_SendUiVisible(bool visible);
