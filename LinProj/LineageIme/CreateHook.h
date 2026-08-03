// CreateHook.h: inline hook user32!CreateWindowExA/W —— 對照 Rust 版
// ime_overlay/src/create_hook.rs（Tier 1 範圍：只保留目前有實際作用的部分，
// 即遊戲 UI thread 第一次建窗時嘗試 ImmDisableTextFrameService；ex-style/
// class-style patch 邏輯在 Rust 端已經是 no-op，這裡連同 RegisterClassEx
// hook 一起省略，純診斷用途、不影響功能）。
#pragma once
#include "Common.h"
#include <string>

// 安裝 CreateWindowExA/W hot-patch trampoline hook。
// 失敗（prologue 不是預期的 5-byte hot-patch 形式）回傳非空錯誤字串。
std::string InstallCreateWindowHook();
