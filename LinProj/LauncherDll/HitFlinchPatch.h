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

// 精靈戰鬥組態設定（Combat Config）。Owned here since HitFlinchPatch.cpp is
// now the only real consumer; GetSuppressFlinch/GetBloodEffect in
// LauncherDll.cpp (still linked into NakedFlinchHook.cpp/NakedBloodHook.cpp,
// which install-time confirm their own target addresses are stale for this
// build and never actually attach) read the same map through this header.
struct SpriteConfig {
  int bloodEffect;
};

extern std::map<int, SpriteConfig> g_SpriteConfigs;

// Reads NpcFlinch.xml (packed into the shared ui.pak, see Pack-UiAssets.ps1)
// into g_SpriteConfigs. Safe to call even when the pak/entry doesn't exist
// (logs and leaves the map empty).
void LoadCombatConfig();

// Loads the sprite config table, then hooks SHOULD_SKIP_FLINCH (VA 0x5ABE70)
// to consult it: player characters always keep their real flinch reaction;
// monsters skip flinch (blood effect only) exactly when their sprite ID is
// listed in the table - not listed means keep the real flinch/stagger
// animation. Call from a background thread after the game's code section is
// decrypted (see DelayedDetourThread in LauncherDll.cpp).
void InstallHitFlinchPatch();
