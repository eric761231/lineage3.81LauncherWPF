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

// 剛被點擊那個技能的資訊。
struct ClickedSkillInfo {
  DWORD packedSkillId = 0;  // spell_book entry +0x04：玩家實際學會的 packed
                            // skill id（送 C_SKILL 用這個，不是技能編號本身）
  char nameBig5[128] = {};  // 技能名稱原始位元組（Big5），含 "(mp/range)" 字尾
};

// 2026-09-15：點技能欄比照背包點選抓資料，位址結構抄自 RUST 參考專案
// C:\python_training\L1J3.8Launcher(RUST)參考\src\aux\spell_book.rs
// （2026-05-01 該專案實機驗證過，同一支 TW13081901.bin，位址直接沿用）：
//   [0x00C31324]（SPELL_BOOK_PTR）→ spell_book 物件
//     +0x2C：技能數量
//     +0x58：技能 entry 指標陣列
//       entry+0x04：packed_skill_id
//       entry+0x0C：名稱字串指標
// entry+0x08（本專案暫稱 unknow2）**已實機驗證**：跟背包 BAGITEM_INFO 同樣
// 排列，點擊技能瞬間會短暫非 0，可以用來判斷「剛被點的是哪個技能」。
// 注意：技能圖示（icon）不在這個物件裡——entry 物件實際只有前 16 bytes
// 有資料，後面全是 0；另外查過的 spell_db（0x009A8ED4）記錄、以及
// CSpellListXML::parse（0x745670）解析 XML `icon` 屬性寫入的 configObj+0x14
// 也都排除了（前者數值對不上已知 castgfx，後者反組譯出來的路徑實測在正常
// 遊玩流程中完全沒被呼叫到）。伺服器端 S_SkillList 封包也確認沒有 icon
// 欄位。icon 目前還沒找到，下次要查建議改找「技能欄視窗實際畫圖示」那個
// 原生繪製函式，看它被呼叫時傳進去的 icon 參數，而不是再找資料結構。
bool InventoryDebug_FindJustClickedSkill(ClickedSkillInfo *out);
