#pragma once
#include <windows.h>

/**
 * timeController: 專門負責遊戲時間欺騙與 GetTickCount 攔截。
 * 目的：防止 RSA 密鑰過期導致的亂碼，以及解決用戶端版本檢查問題。
 */

// 原始函式指標宣告 (可用於 Detours 掛載)
extern void (WINAPI *real_GetLocalTime)(LPSYSTEMTIME);
extern void (WINAPI *real_GetSystemTime)(LPSYSTEMTIME);
extern void (WINAPI *real_GetSystemTimeAsFileTime)(LPFILETIME);
extern DWORD (WINAPI *real_GetTickCount)();
extern DWORD (WINAPI *real_timeGetTime)();

// 攔截後的自定義函式
void WINAPI my_GetLocalTime(LPSYSTEMTIME lpSystemTime);
void WINAPI my_GetSystemTime(LPSYSTEMTIME lpSystemTime);
void WINAPI my_GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime);
DWORD WINAPI my_GetTickCount();
DWORD WINAPI my_timeGetTime();

// 初始化 Winmm.dll 與相關位址
void SetupTimeController();
