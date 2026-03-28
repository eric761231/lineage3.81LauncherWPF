// HelperDlg.h: 遊戲內輔助視窗的邏輯宣告。
#pragma once
#include <windows.h>

// 共用的輔助視窗句柄。
extern HWND hWndHelper;

// 建立輔助視窗（含保護/自動刪除分頁）。
bool CreateHelperDialog();

// 銷毀輔助視窗。
void DestroyHelperDialog();

// 切換輔助視窗顯示狀態。
void ShowOrHideHelperDialog();