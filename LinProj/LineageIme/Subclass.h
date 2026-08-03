// Subclass.h: LUnicodeEdit + 遊戲主視窗 subclass —— 對照 Rust 版
// ime_overlay/src/subclass.rs（Tier 1 範圍：拿掉 DDraw capture 雙緩衝相關的
// WM_ERASEBKGND/WM_PAINT 覆寫與 repaint timer，其餘 IME 通知路由邏輯照移植）。
#pragma once
#include "Common.h"

// 遊戲 UI thread 的 TSF init 觸發訊息（從 worker thread PostMessage 過來）。
extern const UINT WM_TSF_INIT_ON_UI_THREAD;

// 給 CreateHook 用（目前未使用，保留介面對齊 Rust 版）：確認 hwnd 是否為已
// subclass 過的 LUnicodeEdit。
bool IsSubclassedLunicodeEdit(HWND hwnd);

// 對遊戲主視窗 subclass（觸發 TSF init + WM_CTLCOLOREDIT 背景色處理）。
void SubclassGameWindow(HWND hwnd);

// 安裝 SetWinEventHook 監看 OBJECT_CREATE..OBJECT_FOCUS，抓新建/既有的
// LUnicodeEdit 並自動 subclass。
void InstallCreateWatcher();
