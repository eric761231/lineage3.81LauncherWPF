// Overlay.h: 自繪候選字 overlay window（Win11 風格，UpdateLayeredWindow 路徑）。
// 對照 Rust 版 ime_overlay/src/overlay.rs。
#pragma once
#include "Common.h"
#include "Candidates.h"

// 等待遊戲主視窗出現（class "Lineage" 或標題 "Lineage Windows Client (13081901)"）。
// timeoutMs 內找不到回 NULL。
HWND WaitForGameWindow(DWORD timeoutMs);

// 建立 overlay 視窗（WS_POPUP + WS_EX_LAYERED，一開始隱藏）。
HWND CreateOverlayWindow();

void SetOverlayHwnd(HWND h);
bool IsOverlayVisible();

// LUnicodeEdit 收到 IMN_OPENCANDIDATE 時叫。
void ShowOverlayFor(HWND inputHwnd, const ImeState &state);
// IMN_CHANGECANDIDATE 或 WM_IME_COMPOSITION 觸發時更新狀態。
void UpdateOverlay(const ImeState &state);
// IMN_CLOSECANDIDATE / WM_KILLFOCUS / WM_DESTROY 隱藏。
void HideOverlay();
