// AllDayPatch.h: PSS「其他」分頁「全白天」——見
// docs/PSS_全白天_AllDayHook計畫.md。位址/位元組抄自 RUST 參考專案
// src/aux/toggle/all_day.rs（同一支 TW13081901.bin）。
#pragma once

// DelayedDetour／PatchThread 後呼叫：只驗證全部 14 個 patch 點的原生特徵位元組，
// 不會真的下 patch。驗證通過才允許之後呼叫 AllDayHook_SetEnabled(true)。
void InstallAllDayHook();

// 開/關全白天。開：套用全部 code patch + 清天氣/cave_dark 狀態 + 立即刷新調色盤。
// 關：還原全部 code patch + 立即依當下地圖/時間重新計算並刷新調色盤。
void AllDayHook_SetEnabled(bool enabled);

bool AllDayHook_IsEnabled();
