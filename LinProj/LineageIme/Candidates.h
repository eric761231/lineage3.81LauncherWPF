// Candidates.h: IMM32 候選字/組字字串讀取 — 對照 Rust 版 ime_overlay/src/candidates.rs。
#pragma once
#include "Common.h"
#include <string>
#include <vector>

struct ImeState {
  std::wstring composition; // 組字字串（注音/拼音，尚未 commit）
  UINT total = 0;           // 候選字總數（可能跨多頁）
  UINT selection = 0;       // 目前選到第幾個（0-based，絕對 index）
  UINT pageStart = 0;       // 目前頁起始 index（0-based）
  UINT pageSize = 0;        // 每頁多少個（通常 9）
  std::vector<std::wstring> items; // 完整候選 list（最多前 64 個）

  // 目前頁要顯示的候選 slice
  std::vector<std::wstring> PageItems() const;
  // 頁內被選到的 index（0-based），沒選到回 -1
  int PageSelection() const;
};

// 拉一份完整 IME 狀態（組字 + 候選 list）；HIMC 依序嘗試 hwnd → parent → 根視窗。
// 成功回 true 並填 out；失敗（無 HIMC 或候選清單讀取失敗）回 false。
bool FetchImeState(HWND hwnd, ImeState &out);
