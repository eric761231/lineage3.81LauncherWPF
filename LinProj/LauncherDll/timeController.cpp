#include "stdafx.h"
#include "timeController.h"

// 撖阡???摰儔
void (WINAPI *real_GetLocalTime)(LPSYSTEMTIME) = GetLocalTime;
void (WINAPI *real_GetSystemTime)(LPSYSTEMTIME) = GetSystemTime;
void (WINAPI *real_GetSystemTimeAsFileTime)(LPFILETIME) = GetSystemTimeAsFileTime;
DWORD (WINAPI *real_GetTickCount)() = GetTickCount;
DWORD (WINAPI *real_timeGetTime)() = NULL; // 從 winmm.dll 動態取得（SetupTimeController 初始化）

// 1. 強制 LocalTime 回傳 2013/8/1（避免 RSA 時間效期過期）
void WINAPI my_GetLocalTime(LPSYSTEMTIME lpSystemTime) {
  real_GetLocalTime(lpSystemTime);
  lpSystemTime->wYear = 2013;
  lpSystemTime->wMonth = 8;
  lpSystemTime->wDay = 1;
}

// 2. 強制 SystemTime，以繞過 RSA 時間效期驗證
void WINAPI my_GetSystemTime(LPSYSTEMTIME lpSystemTime) {
  real_GetSystemTime(lpSystemTime);
  lpSystemTime->wYear = 2013;
  lpSystemTime->wMonth = 8;
  lpSystemTime->wDay = 1;
}

// 3. 蝟餌絞瑼???甈粹?
void WINAPI my_GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime) {
  SYSTEMTIME st = {2013, 8, 4, 1, 12, 0, 0, 0};
  SystemTimeToFileTime(&st, lpSystemTimeAsFileTime);
}

// 4. ?箏? GetTickCount ?脫迫?冽蝡臬????賢榆?蝺?
DWORD WINAPI my_GetTickCount() {
  return 3600000; // ?箏???1 撠?銋?
}

// 5. 強制 winmm.dll 的 timeGetTime 也回傳固定值
DWORD WINAPI my_timeGetTime() {
  return 3600000;
}

// ????Setup
void SetupTimeController() {
  HMODULE hWinmm = GetModuleHandleA("winmm.dll");
  if (!hWinmm) hWinmm = LoadLibraryA("winmm.dll");
  if (hWinmm) {
    real_timeGetTime = (DWORD(WINAPI*)())GetProcAddress(hWinmm, "timeGetTime");
  }
}
