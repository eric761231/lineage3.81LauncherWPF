#pragma once
#include <cstdint>

/**
 * Lineage 3.81/8.15 DLL 注入開發參考標頭檔 (完整整合版)
 * 已根據 0x1C0116A (3.81) 反組譯結果重新校對暫存器與偏移量。
 */

namespace L1Offsets {
    // --- 基礎映像檔資訊 (Basic Image Information) ---
    const uintptr_t BASE_ADDRESS = 0x400000;

    // --- CObject / CPlayer 結構偏移量 (Struct Offsets) ---
    // 這些偏移量是相對於物件指標 (ESI/ECX) 的距離
    const int OFFSET_LINKED_DATA  = 0x04;   // [ESI + 0x04]  - pLinkedData (動畫數據關聯指標)
    const int OFFSET_RENDER_INFO  = 0x08;   // [ESI + 0x08]  - pRenderInfo (彩現資訊指標)
    
    const int OFFSET_DIRECTION     = 0xA8;   // [ESI + 0xA8]  - 目前面向的方向 (0-7)
    const int OFFSET_ACTION_ID     = 0xAC;   // [ESI + 0xAC]  - 目前動作編號 (0:走路, 1:攻擊, 2:後仰, 3:呼吸, 4:步進)
    const int OFFSET_FRAME_ID      = 0xB0;   // [ESI + 0xB0]  - 目前動畫播放的影格索引 (FrameID)
    
    const int OFFSET_SPRITE_ID     = 0x120;  // [ESI + 0x120] - 怪物/人物圖檔編號 (Graphic ID)
    
    // --- 反組譯新增：細節控制偏移量 ---
    const int OFFSET_ACTION_SEG_INDEX = 0x68; // [ESI + 0x68]  - 動畫段索引 (Action Segment Index)
    const int OFFSET_START_FRAME      = 0x6C; // [ESI + 0x6C]  - 動作起始影格
    const int OFFSET_END_FRAME        = 0x70; // [ESI + 0x70]  - 動作結束影格
    const int OFFSET_FLINCH_SWITCH    = 0x17C; // [ESI + 0x17C] - 受傷動作啟動開關 (0=關閉, 1=開啟)
    const int OFFSET_FLINCH_ACTIVE    = 0x17D; // [ESI + 0x17D] - 受傷動畫播放中狀態 Flag

    // --- Hook 目標位址 (Virtual Addresses) ---
    // 註：這些是從內存 Dump 中提取的絕對虛擬位址 (VA)。

    // 1. 動畫更新入口 (Locomotion Hook 用)
    const uintptr_t HOOK_UPDATE_ANIMATION = 0x489670; 

    // 2. 3.81 受傷後仰觸發點 (Flinch Suppression 用)
    // 彙編指令：MOV [ESI+0xAC], 2
    const uintptr_t HOOK_FLINCH_ACTIVATION_381 = 0x1C0116A; 
    
    // 3. 8.15 受傷後仰觸發點 (備用)
    const uintptr_t HOOK_FLINCH_ACTIVATION_815 = 0x1A78BC1;

    // 4. 噴血特效觸發點 (Blood Redirection 用)
    // 彙編指令：PUSH 10770
    const uintptr_t HOOK_BLOOD_EFFECT_PUSH = 0x49D27;

    // 5. 核心動畫設定函式 (內部 Call，反組譯確認位址)
    const uintptr_t FUNC_SETUP_ACTION_381 = 0x15A7EF1;

    // --- 動畫、音效與特效常用常量 (Constants) ---
    const int ACTION_WALK     = 0;  // 原始走路
    const int ACTION_ATTACK   = 1;  // 攻擊
    const int ACTION_DAMAGE   = 2;  // 受傷 (後仰)
    const int ACTION_BREATH   = 3;  // 呼吸/待機
    const int ACTION_WALK2    = 4;  // 8.15 Locomotion 動畫 (左右腳切換用)
    
    // 腳步聲事件 (用於精準判斷何時切換左右腳)
    const int EVENT_FOOT_RIGHT    = 4087; // 右腳踩地音效 ID
    const int EVENT_FOOT_LEFT     = 4088; // 左腳踩地音效 ID

    const int EFFECT_RED_BLOOD    = 10770; // 標準紅色噴血
    const int EFFECT_DRAGON_BLOOD = 1248;  // 龍類大型噴血 (建議 XML 用於指定怪物)

    // --- 裝備欄位編號 (Equipment Slot IDs) ---
    namespace SlotIDs {
        const int HELM      = 0;  // 頭盔 (Helm_Icon)
        const int ARMOR     = 1;  // 盔甲 (Armor_Icon)
        const int TSHIRT    = 2;  // 內衣 (Shirt_Icon)
        const int CLOAK     = 3;  // 斗篷 (Cloak_Icon)
        const int GLOVES    = 4;  // 手套 (Glove_Icon)
        const int BOOTS     = 5;  // 靴子 (Boots_Icon)
        const int SHIELD    = 6;  // 盾牌 (Shield_Icon)
        const int WEAPON    = 7;  // 武器 (Weapon_Icon)
        const int NECKLACE  = 8;  // 項鍊 (Necklace_Icon)
        const int RING1     = 9;  // 戒指1 (Ring1_Icon)
        const int RING2     = 10; // 戒指2 (Ring2_Icon)
        const int BELT      = 11; // 腰帶 (Waist_Icon)
        const int EARRING   = 12; // 耳環 (Earring_Icon)
        const int RING3     = 13; // 戒指3 (RingLv80_Icon)
        const int RING4     = 14; // 戒指4 (RingLv85_Icon)
        const int RUNE      = 15; // 符石 (Rune_Icon)
        const int RUNE_L    = 16; // 符石左 (RuneL_Icon)
        const int RUNE_R    = 17; // 符石右 (RuneR_Icon)
        const int ARROW     = 18; // 箭矢 (Arrow_Icon)
    }

    // --- UI 元件實體位址 (UI Component Instances) ---
    // 註：這些是 CItemIcon 物件在記憶體中的固定/分析位址 (DMP 參考)
    namespace UI_Components {
        const uintptr_t ARMOR_ICON    = 0x1068aac;
        const uintptr_t GLOVE_ICON    = 0x1718c07;
        const uintptr_t SHIELD_ICON   = 0x1d47d08;
        const uintptr_t BOOTS_ICON    = 0x1ec8b1a;
        const uintptr_t RING2_ICON    = 0xff911c;
        const uintptr_t RING80_ICON   = 0x10686cc;
        const uintptr_t ARROW_ICON    = 0x134993;
        const uintptr_t RUNE_R_ICON   = 0x19e7d1;
        const uintptr_t RUNE_ICON     = 0x19e705; // 符石核心位址
        const uintptr_t RUNE_L_ICON   = 0x19e745;
    }

    // --- 關鍵補丁位址 (Patch Targets) ---
    // 這些位址包含裝備欄位限制 (Slot Logic Limits)
    namespace PatchTargets {
        // [1] UI 渲染/更新邊界檢查 (原為 0x0E / 14)
        const uintptr_t UI_SLOT_LIMIT_CHECK_1 = 0x39f1;   
        const uintptr_t UI_SLOT_LIMIT_CHECK_2 = 0x5e4d;
        
        // [2] 特殊 Slot ID 處理點 (15 與 18 的跳轉邏輯)
        const uintptr_t SLOT_SPECIAL_HANDLING_1 = 0x165b9; 
        const uintptr_t SLOT_SPECIAL_HANDLING_2 = 0x169d9;
    }
}

/**
 * 實作指南：
 * 
 * 1. 擴充裝備欄位至符石：
 *    搜尋關鍵字 83 F8 0E (CMP EAX, 14)，將其修改為 83 F8 12 (CMP EAX, 18)。
 *    重點位址見 L1Offsets::PatchTargets。
 * 
 * 2. 左右腳步進：
 *    在 Hook UpdateAnimation 時，讀取 [ESI+OFFSET_ACTION_ID]。
 *    若是 0，可依據內部邏輯翻轉為 4。
 * 
 * 3. XML 不後仰過濾：
 *    在 HOOK_FLINCH_ACTIVATION_381 (0x1C0116A) 處攔截。
 */
