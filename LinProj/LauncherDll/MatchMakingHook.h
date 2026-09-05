// MatchMakingHook.h: 血盟推薦登錄視窗（MatchMakingUI -> MatchRegister_Window）
// 選類別（Killer/Hunter/Talker）只更新預覽用的 Intro_Label，從來不寫進
// Register 真正讀取的 Intro_Edit，導致介紹欄位永遠是空字串、登錄一律靜默失敗
// （見 LinBin3.81 專案 docs/hooks/MATCHMAKING_PATCH_BRIEF.md 完整反組譯分析）。
#pragma once

// Detour SetTypeLabel（VA 0x64F700）：選類別後照官方同一套字串表查詢，把
// 對應的字串（Killer=3285/Hunter=3256/Talker=3284）也寫進 Intro_Edit，
// 讓 Register（0x64FC50）讀得到非空字串。call 自 DelayedDetourThread。
void InstallMatchMakingHook();
