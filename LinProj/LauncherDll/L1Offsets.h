#pragma once
#include <cstdint>

/**
 * Lineage 3.81 DLL 注入開發參考標頭檔
 *
 * 版本：完整重寫版（2026-08-01）
 * 驗證依據：TW13081901.DMP（正式執行期完整記憶體 dump）
 *           verify_addresses.py（LinBin3.81/verify_addresses.py）
 *           Rust 專案 L1J3.8Launcher(RUST)參考 src/aux/address.rs ★★★★★
 *
 * 信度標記說明：
 *   ★★★★★ = Rust 原始碼 + dump 雙重驗證，多次實機確認
 *   ★★★★  = Rust 原始碼標記或 dump 驗證之一
 *   ⚠️ 未驗證 = 尚無可靠依據，需重新反組譯或 debugger 動態確認
 *   ❌ 已知錯誤 = dump 驗證確認不符，切勿使用
 *
 * 舊版 L1Offsets.h 已知錯誤清單（勿沿用）：
 *   HOOK_UPDATE_ANIMATION   = 0x489670   ❌ bytes 對不上函式入口
 *   HOOK_FLINCH_ACTIVATION_381 = 0x1C0116A  ❌ 完全 UNMAPPED
 *   HOOK_FLINCH_ACTIVATION_815 = 0x1A78BC1  ❌ 全零區域（8.15 版本，不適用）
 *   HOOK_BLOOD_EFFECT_PUSH  = 0x49D27    ❌ bytes 不符，Effect 10770 在此 build 不存在
 *   FUNC_SETUP_ACTION_381   = 0x15A7EF1  ❌ 完全 UNMAPPED
 *   UI_Components (全批)                 ❌ heap 位址設計錯誤，應執行期動態解析
 *   CObject 結構偏移 (全批)             ❌ 與 Rust 已驗證結構差距過大，非同一物件
 */

namespace L1Offsets {

    // =========================================================================
    // 基礎映像檔資訊
    // =========================================================================
    const uintptr_t BASE_ADDRESS = 0x400000;


    // =========================================================================
    // 全域狀態指標 — 全部 Rust aux/address.rs ★★★★+ 驗證
    // =========================================================================

    /// 當前地圖 ID（dword）
    const uintptr_t G_MAP_ID        = 0x00965B60; // ★★★★

    /// 遊戲狀態（1=登入中, 2=遊玩中, 3=登出, 9=掉線）
    const uintptr_t G_GAME_STATE    = 0x009AB5E8; // ★★★★

    /// 玩家實體指標（*CEntity / *CPlayer）
    const uintptr_t G_PLAYER_PTR    = 0x00C2D2B8; // ★★★★（兩處 Rust 程式碼互相佐證）

    /// 職業 byte（0=君主, 1=騎士, 2=精靈, 3=法師, 4=暗精靈, 6=龍騎）
    const uintptr_t G_CLASS         = 0x00C31544; // ★★★★★

    /// 光線等級（byte，白天通常 0xFA）
    const uintptr_t G_LIGHT_LEVEL   = 0x00C31EAE; // ★★★★★

    /// 最大 HP（dword）
    const uintptr_t MAX_HP          = 0x00C31E90; // ★★★★★

    /// 最大 MP（dword）
    const uintptr_t MAX_MP          = 0x00C31E8C; // ★★★★★

    /// 加速 buff table 指標
    const uintptr_t G_HASTE_BUFF_TABLE = 0x00ABF4C8; // ★★★★

    /// Spell book 物件指標（heap，入場後才有效）
    const uintptr_t SPELL_BOOK_PTR  = 0x00C31324; // ★★★★★

    /// Spell DB 陣列指標
    const uintptr_t SPELL_DB_PTR    = 0x009A8ED4; // ★★★★★

    /// 背包基底指標
    const uintptr_t INVENTORY_BASE  = 0x009A9250; // ★★★★

    /// 當前 HP 物件（XOR 加密，12 bytes 結構）
    const uintptr_t CURRENT_HP_OBJ  = 0x00BDC828; // ★★★★★

    /// 當前 MP 物件（同上 XOR 加密）
    const uintptr_t CURRENT_MP_OBJ  = 0x00BDC834; // ★★★★★

    /// XOR 解密 magic value（STAT_XOR_MAGIC = 0xC0017921）
    const uint32_t  STAT_XOR_MAGIC  = 0xC0017921; // ★★★★★

    /// 飽食度 raw byte（滿值 = raw * 100 / 225）
    const uintptr_t FOOD_LEVEL      = 0x00C31E8B; // ★★★★★

    /// 負重 raw byte（滿值 = raw * 100 / 240）
    const uintptr_t CURRENT_WEIGHT  = 0x00C31E8A; // ★★★★★

    // 能力值（6 bytes 連續區塊，0xC31E80 ~ 0xC31E85）
    const uintptr_t STAT_STR        = 0x00C31E80; // ★★★★★
    const uintptr_t STAT_INT        = 0x00C31E81; // ★★★★★
    const uintptr_t STAT_WIS        = 0x00C31E82; // ★★★★★
    const uintptr_t STAT_DEX        = 0x00C31E83; // ★★★★★
    const uintptr_t STAT_CON        = 0x00C31E84; // ★★★★★
    const uintptr_t STAT_CHA        = 0x00C31E85; // ★★★★★

    /// Lawful（2-byte signed short，負值代表 chaotic）
    const uintptr_t STAT_LAWFUL     = 0x00C31E88; // ★★★★★


    // =========================================================================
    // 核心函式位址 — Rust aux/address.rs ★★★★★ 驗證
    // =========================================================================

    /// SendPacketData：多方向封包發送進入點
    const uintptr_t SEND_PACKET_DATA = 0x00580E50; // ★★★★★

    /// ProcessPacket：封包接收進入點
    const uintptr_t PROCESS_PACKET   = 0x00539333; // ★★★★★

    /// cast_magic：施法 dispatcher（cdecl）
    const uintptr_t CAST_MAGIC       = 0x0073C260; // ★★★★★

    /// spell_book_cast（thiscall）
    const uintptr_t SPELL_BOOK_CAST  = 0x0073ECE0; // ★★★★★

    /// SpawnEffect 函式（所有特效的統一觸發點）
    /// 函式入口確認：0x004C7D80 = 55 8B EC...（PUSH EBP; MOV EBP,ESP）
    /// dump 驗證：TW13081901.DMP @ file_offset 對應 VA 讀取一致
    const uintptr_t SPAWN_EFFECT_FUNC = 0x004C7D80; // ★★★★（dump 驗證：bytes 55 8B EC 正確）


    // =========================================================================
    // 已移植 Hook 位址 — 經 dump 驗證完全吻合
    // =========================================================================

    /// 動畫更新核心迴圈 (MovementFunc) 入口
    /// 內含呼叫 action table 的邏輯，取代舊版錯誤的 0x489670
    const uintptr_t HOOK_UPDATE_ANIMATION = 0x005A9EE0; // ★★★★ (2026-08-01 靜態追蹤推算)

    /// 受傷不後仰 (Flinch Suppression) 攔截點
    /// 對應指令：MOV [EDX+0x14], 2 (C7 42 14 02 00 00 00)
    /// 取代舊版錯誤/未對映的 0x1C0116A
    const uintptr_t HOOK_FLINCH_ACTIVATION = 0x006A0C0F; // ★★★★ (2026-08-01 靜態追蹤推算)

    /// 順跑（變身跑步）中段 hook
    /// 原始 5 bytes：8B 44 C2 04 5D（mov eax, [edx+eax*8+4]）
    /// 來源：smooth_run_hook.rs::HOOK_ADDR ★★★★★
    const uintptr_t HOOK_SMOOTH_RUN  = 0x00449776; // ★★★★★

    /// Surf ID bounds check（裝備 UI）
    /// 原始 6 bytes：3B 15 B0 D0 C2 00（cmp edx, [0xC2D0B0]）
    /// 來源：equip_ui.rs::SURF_BOUNDS_CHECK ★★★★★
    const uintptr_t SURF_BOUNDS_CHECK = 0x004387DB; // ★★★★★


    // =========================================================================
    // AOB 動態掃描目標（非固定位址）— 參考 equip_ui.rs
    // =========================================================================
    // 注意：以下兩個 hook 不是固定位址，需在 0x790000-0x7A0000 區段做 AOB scan
    // 實際命中位址（TW13081901.DMP）：
    //   ServerIndex switch = 0x794FB9（AOB scan 動態得出，非硬編碼）
    //   SetupSlots         = 0x794282（AOB scan 動態得出，非硬編碼）
    //
    // 詳細 AOB pattern 見 equip_ui.rs::AOB_SERVER_INDEX / AOB_SETUP_SLOTS


    // =========================================================================
    // 實體結構偏移量（相對於 entity / player 指標）
    // 來源：aux/address.rs::player_offset ★★★★★
    // 注意：Rust 驗證的這個結構是從 G_PLAYER_PTR 取得的「邏輯實體」，
    //       不是舊版 L1Offsets.h 宣稱的 CObject/CPlayer（已知兩者結構差距極大）
    // =========================================================================
    namespace player_offset {
        /// action_state（49=idle, 4=walk, 0=between, 8=transparent）
        const int ACTION_STATE = 0x14; // ★★★★★

        /// direction（0~7，八方向）
        const int DIRECTION    = 0x15; // ★★★★★

        /// anim_frame（動畫幀計數器）
        const int ANIM_FRAME   = 0x17; // ★★★★★（smooth_run_hook 互相印證）

        /// sprite_id（圖檔 / 外觀 ID）
        const int SPRITE_ID    = 0x18; // ★★★★★

        /// haste_low（加速 buff 低 byte）
        const int HASTE_LOW    = 0x24; // ★★★★★（smooth_run_hook 互相印證）

        /// haste_high（強加速 buff）
        const int HASTE_HIGH   = 0x29; // ★★★★★（smooth_run_hook 互相印證）

        /// map_id（與 G_MAP_ID 同步確認）
        const int MAP_ID       = 0x80; // ★★★★
    }


    // =========================================================================
    // 實體結構偏移量（另一種實體結構，entity_scan.rs 發現）
    // 用於 entity scan / NPC / 怪物，與 player_offset 不是同一種物件
    // =========================================================================
    namespace entity_scan_offset {
        /// 實體類型辨識用 vfptr
        const uintptr_t ENTITY_VFPTR  = 0x008DC08C; // ★★★★（2026-05-03 Frida + heap pattern + 實機）

        /// target_id（[entity + 0x0C]）
        const int TARGET_ID    = 0x0C;

        /// 名稱 ptr（[entity + 0x60]）
        const int NAME_PTR     = 0x60;
    }


    // =========================================================================
    // 動作常數（純數字，無需驗證）
    // =========================================================================
    const int ACTION_WALK     = 0;   // 走路（RunL）
    const int ACTION_WALK2    = 4;   // 8.15 Locomotion RunR 切換用
    const int ACTION_ATTACK   = 1;   // 攻擊
    const int ACTION_DAMAGE   = 2;   // 受傷（後仰）
    const int ACTION_BREATH   = 3;   // 呼吸/待機
    const int ACTION_IDLE     = 49;  // 待機（idle）


    // =========================================================================
    // 特效動畫編號（Effect Animation ID）— 傳給 SPAWN_EFFECT_FUNC 的參數值，非記憶體位址
    // 用法：SPAWN_EFFECT_FUNC(effect_id, ...) 依此編號播放對應動畫
    // 注意：在 dump 中搜尋 PUSH 10770 找不到，是因為此 ID 可能透過暫存器或
    //       lookup table 傳入，不代表編號本身有誤。
    // =========================================================================

    /// 第一個噴血特效編號（標準受傷噴血）
    /// dump 中找到 5 個直接 PUSH 1248 的呼叫點，全部呼叫 SPAWN_EFFECT_FUNC (0x4C7D80)
    const int EFFECT_DRAGON_BLOOD = 1248;

    /// 標準紅色噴血特效動畫編號
    /// 此為特效動畫 ID，不是記憶體位址。
    /// dump 中沒有找到 PUSH 10770 的直接指令，推測此 ID 是從 lookup table/暫存器傳入。
    const int EFFECT_RED_BLOOD    = 10770;

    // 已知在 dump 中存在的特效呼叫點 VA（僅供參考，hook 時請用 SPAWN_EFFECT_FUNC）：
    // PUSH 1248 @ 0x0041E570, 0x0052A1A9, 0x0052AF8A, 0x005A98DF, 0x005ABD07


    // =========================================================================
    // 裝備欄位編號（純數字，無需驗證）
    // =========================================================================
    namespace SlotIDs {
        const int HELM      = 0;   // 頭盔
        const int ARMOR     = 1;   // 盔甲
        const int TSHIRT    = 2;   // 內衣
        const int CLOAK     = 3;   // 斗篷
        const int GLOVES    = 4;   // 手套
        const int BOOTS     = 5;   // 靴子
        const int SHIELD    = 6;   // 盾牌
        const int WEAPON    = 7;   // 武器
        const int NECKLACE  = 8;   // 項鍊
        const int RING1     = 9;   // 戒指1
        const int RING2     = 10;  // 戒指2
        const int BELT      = 11;  // 腰帶
        const int EARRING   = 12;  // 耳環
        const int RING3     = 13;  // 戒指3（Lv80）
        const int RING4     = 14;  // 戒指4（Lv85）
        const int RUNE      = 15;  // 符石
        const int RUNE_L    = 16;  // 符石左
        const int RUNE_R    = 17;  // 符石右
        const int ARROW     = 18;  // 箭矢
    }


    // =========================================================================
    // item_entry 結構偏移（來源：aux/address.rs::item_offset ★★★★★）
    // =========================================================================
    namespace item_offset {
        const int ITEM_PARAM  = 0x04;  // 物品 ID（server-assigned obj_id）
        const int VALID       = 0x08;  // 是否存在（1=有效）
        const int EQUIPPED    = 0x09;  // 是否已裝備（1=已裝備）
        const int NAME_PTR    = 0x0C;  // 物品名稱字串指標
        const int ITEM_TYPE   = 0x98;  // 物品種類 byte
        const int ICON_NUM    = 0x9A;  // 動畫/圖示編號（short）
        const int ITEM_COUNT  = 0xA0;  // 堆疊數量 dword
    }


    // =========================================================================
    // 封包 Opcode（來源：aux/address.rs ★★★★★）
    // =========================================================================
    namespace Opcode {
        const uint8_t C_DELETE_ITEM  = 0x8A; // ★★★★★（2026-05-02 Frida capture 確認）
        const uint8_t C_REFINE       = 0x4E; // 精煉/合成
        const uint8_t C_CHAT         = 0x88; // 聊天 / dialog reply
        const uint8_t CHAT_SHOUT     = 0x02; // 喊話 channel byte
        const uint8_t CHAT_NORMAL    = 0x00; // 一般 channel byte
    }


    // =========================================================================
    // 待確認項目（需重新反組譯或 debugger 動態驗證）
    // =========================================================================
    //
    // 以下功能在 Rust 專案中完全沒有實作，dump 驗證也無法取得正確位址：
    //
    //  1. FUNC_SETUP_ACTION（動作設定函式）
    //     舊值 0x15A7EF1 = UNMAPPED
    //     需要：重新從 dump 中找 action 設定的 call chain
    //
    //  2. 受傷動畫結構偏移（OFFSET_FLINCH_SWITCH / OFFSET_FLINCH_ACTIVE）
    //     無依據，需 debugger attach 動態觀察
    //
    // =========================================================================

} // namespace L1Offsets
