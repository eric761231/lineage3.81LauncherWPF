// HitFlinchPatch.h: decides, per monster sprite ID, whether a hit reaction
// skips the real flinch/stagger animation in favor of just showing a blood
// effect - instead of the old blanket "all monsters skip, all players don't"
// rule. Player characters (any PC, including PK opponents) always keep their
// real flinch reaction regardless of the table.
//
// 2026-09-02: 依需求方要求簡化語意 - 拿掉 suppressFlinch 開關，改成純粹「有列在
// NpcFlinch.xml 裡就跳過受身（噴血特效），沒列到就維持原本受身動畫」，presence
// 本身就是旗標，不需要另外一個布林值。
#pragma once
#include <map>

/**
 * @struct SpriteConfig
 * @brief 精靈戰鬥組態設定（例如受身與血液特效 ID）。
 */
struct SpriteConfig {
  int bloodEffect; // 噴血特效 ID
};

extern std::map<int, SpriteConfig> g_SpriteConfigs;

/**
 * @brief 從 ui.pak 中讀取 NpcFlinch.xml 設定至 g_SpriteConfigs。
 */
void LoadCombatConfig();

/**
 * @brief 載入精靈設定表並 Hook SHOULD_SKIP_FLINCH 函式。
 */
void InstallHitFlinchPatch();
