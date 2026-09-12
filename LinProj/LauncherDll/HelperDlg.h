// HelperDlg.h: 舊 DMA 輔助視窗（已停用，實作包在 HelperDlg.cpp 的 #if 0）。
// 自動喝水改走 PssOverlay（HOME）+ PssConfig。
#pragma once
#include <windows.h>

extern HWND hWndHelper;
bool CreateHelperDialog();
void DestroyHelperDialog();
void ShowOrHideHelperDialog();
